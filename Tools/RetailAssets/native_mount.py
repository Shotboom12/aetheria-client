"""Stage a uniquely allocated native Retail mount from resolved, pinned metadata."""
from __future__ import annotations
import argparse
from contextlib import closing
import json
import re
import shutil
import sqlite3
import struct
import subprocess
from pathlib import Path
import native_import as n
import localenv
from server import KEYS, query, snapshot

SQL_DBC = {"CreatureModelData": "creaturemodeldata_dbc", "CreatureDisplayInfo": "creaturedisplayinfo_dbc",
           "Spell": "spell_dbc", "SkillLineAbility": "skilllineability_dbc"}
SERVER_DATA = "/azerothcore/env/dist/data"
if localenv.use_local():
    KEYS.update({table: "ID" for table in SQL_DBC.values()})


def docker_bytes(*args):
    return subprocess.run(["docker", "exec", "ac-worldserver", *args], capture_output=True, check=True).stdout


def server_file(relative):
    """Read the native server or the upstream container through one interface."""
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Server path must stay inside the data directory")
    if localenv.use_local():
        return (localenv.setting("ESTERIA_SERVER_DATA") / path).read_bytes()
    return docker_bytes("cat", f"{SERVER_DATA}/{path.as_posix()}")


def allocate_mount(path, key, used):
    starts = {"Spell": 300000, "SpellIcon": 600000, "SkillLineAbility": 400000,
              "CreatureModelData": 200000, "CreatureDisplayInfo": 200000, "Creature": 4500000}
    with closing(sqlite3.connect(path, timeout=30)) as db, db:
        db.execute("CREATE TABLE IF NOT EXISTS mount_allocations (source_key TEXT, kind TEXT, id INTEGER, "
                   "PRIMARY KEY(source_key,kind), UNIQUE(kind,id))")
        db.execute("BEGIN IMMEDIATE")
        result = {}
        for kind, start in starts.items():
            previous = db.execute("SELECT id FROM mount_allocations WHERE source_key=? AND kind=?", (key, kind)).fetchone()
            if previous:
                result[kind] = previous[0]
                continue
            occupied = used[kind] | {r[0] for r in db.execute("SELECT id FROM mount_allocations WHERE kind=?", (kind,))}
            while start in occupied: start += 1
            if start > 5000000: raise ValueError("Mount ID budget exhausted")
            db.execute("INSERT INTO mount_allocations VALUES (?,?,?)", (key, kind, start))
            result[kind] = start
        return result


def publish_server_tables(pack, manifest):
    """Only publish new, hash-matching continuation files. Never touch base DBCs."""
    slug = manifest["slug"]
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", slug): raise ValueError("Invalid slug")
    if manifest["target"].get("server_dbc_backend") == "local-sql":
        n.verify_server(manifest)
        return # This server's DBC stores load their existing SQL overlays.
    if localenv.use_local():
        root = localenv.setting("ESTERIA_SERVER_DATA") / "dbc-continuations"
        root.mkdir(parents=True, exist_ok=True)
        for table in SQL_DBC:
            source = pack / "tables" / f"{table.lower()}.dbc"
            destination = root / f"{table}.dbc1-native-retail-{slug}"
            data = source.read_bytes()
            if destination.exists():
                if destination.read_bytes() != data:
                    raise ValueError("Server continuation path collision")
            else:
                # Exclusive creation refuses a competing deployment's file.
                with destination.open("xb") as output:
                    output.write(data)
            if destination.read_bytes() != data:
                raise ValueError("Server continuation readback differs")
        return
    root = f"{SERVER_DATA}/dbc-continuations"
    subprocess.run(["docker", "exec", "ac-worldserver", "mkdir", "-p", root], check=True)
    for table in SQL_DBC: # SpellIcon has no server store; it is client UI metadata.
        local = pack / "tables" / f"{table.lower()}.dbc"
        destination = f"{root}/{table}.dbc1-native-retail-{slug}"
        exists = subprocess.run(["docker", "exec", "ac-worldserver", "test", "-e", destination]).returncode == 0
        if exists:
            if docker_bytes("cat", destination) != local.read_bytes(): raise ValueError("Server continuation path collision")
        else:
            subprocess.run(["docker", "cp", str(local.resolve()), f"ac-worldserver:{destination}"], check=True, capture_output=True)
        if docker_bytes("cat", destination) != local.read_bytes(): raise ValueError("Server continuation readback differs")


def f32(value): return struct.unpack("<I", struct.pack("<f", value))[0]


def stage(args):
    recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
    slug, name = recipe["slug"], recipe["name"]
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,63}", slug) or args.stage.exists(): raise ValueError("Invalid slug or existing stage")
    config = dict(line.split("=", 1) for line in args.source_config.read_text().splitlines() if "=" in line)
    if config.get("build") != recipe["build"] or config.get("product") != recipe["product"]:
        raise ValueError("Recipe and pinned CASC source differ")
    identity_kind = recipe.get("source_identity_kind", "mount")
    identity = recipe.get("source_identity", recipe["retail_mount_id"])
    source_key = f"{recipe['product']}:{recipe['build']}:{identity_kind}:{identity}"
    client, providers = n.client_ids(args.client, args.server_root)
    server_ids, server_providers = n.server_table_ids()
    schema = snapshot(KEYS)
    used = {table: client[table] | server_ids[table] for table in n.TABLES}
    for table, sql_table in SQL_DBC.items():
        used[table] |= {int(v) for v in query(f"SELECT ID FROM `{sql_table}`;").splitlines()}
    used["Creature"] = set(schema["creature_template"]["ids"]) | set(schema["creature_template_model"]["ids"])
    used["CreatureDisplayInfo"] |= set(schema["creature_model_info"]["ids"])
    allocations = args.client / "Data/Retail/allocations.sqlite"
    item, display = n.allocate(allocations, source_key,
        used["Item"] | set(schema["item_template"]["ids"]) | set(schema["item_dbc"]["ids"]),
        used["ItemDisplayInfo"] | set(schema["itemdisplayinfo_dbc"]["ids"]))
    ids = allocate_mount(allocations, source_key, used)
    # Read the actual running server's flying mount donor; refuse an ambiguous SQL overlay.
    donor = recipe["summon_donor"]
    if query(f"SELECT COUNT(*) FROM spell_dbc WHERE ID={donor};") != "0":
        raise ValueError("Flying mount donor has a SQL override; resolve it before staging")
    donor_bytes = server_file("dbc/Spell.dbc")
    rows, _ = n.wdbc(donor_bytes, 234)
    spell = next(row.copy() for row in rows if row[0] == donor)
    if spell[95] != 78 or spell[71] != 6: raise ValueError("Donor is not a mounted aura")
    spell[0], spell[110], spell[133] = ids["Spell"], ids["Creature"], ids["SpellIcon"]
    # The carpet donor's state visual forces the rider into StealthStand; a mount without one seats the rider with Mount (91).
    spell[131], spell[132] = 15809, 0
    for field in n.TABLES["Spell"][1]: spell[field] = 0
    spell[152], spell[169], spell[186], spell[203] = 16712190, 0, 16712190, 16712188
    model_id, creature_display, creature = ids["CreatureModelData"], ids["CreatureDisplayInfo"], ids["Creature"]
    model = [model_id, 1027, 0, 0, f32(1), 11, 4, f32(18), f32(12), f32(1), 0, 0, 0, 0,
             f32(recipe["collision_width"]), f32(recipe["collision_height"]), f32(recipe["mount_height"]),
             *(f32(v) for v in recipe["bounds"]), f32(1), f32(1), 0, 0, 0]
    creature_row = [creature_display, model_id, 0, 0, f32(recipe.get("display_scale", 1)), 255, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0]
    icon_name = "INV_Esteria_Retail_" + slug.replace("-", "_")
    icon_path = "Interface\\Icons\\" + icon_name
    item_row = [item, 15, 5, 0xffffffff, 4, display, 0, 0]
    tables = {
        "Item": ([item_row], {}),
        "ItemDisplayInfo": ([[display] + [0] * 24], {(0, 5): icon_name}),
        "CreatureModelData": ([model], {(0, 2): f"retail\\{recipe['model_file_id']}.m2"}),
        "CreatureDisplayInfo": ([creature_row], {(0, 6 + i): f"{fdid}.blp" for i, fdid in enumerate(recipe["texture_file_ids"][:3])}),
        "SpellIcon": ([[ids["SpellIcon"], 0]], {(0, 1): icon_path}),
        "SkillLineAbility": ([[ids["SkillLineAbility"], 777, ids["Spell"], 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]], {}),
        "Spell": ([spell], {(0, 136): name, (0, 137): name, (0, 170): "Summons and dismisses this mount.",
                            (0, 171): "Summons and dismisses this mount.", (0, 187): "Increases flight speed by $s2%.",
                            (0, 188): "Increases flight speed by $s2%."}),
    }
    records = {
        "item_template": {"entry": item, "class": 15, "subclass": 5, "SoundOverrideSubclass": -1,
            "name": "Reins of the " + name, "displayid": display, "Quality": 4, "ItemLevel": 1, "RequiredLevel": 1,
            "AllowableClass": -1, "AllowableRace": -1, "maxcount": 1, "stackable": 1, "bonding": 1,
            "Material": 4, "BagFamily": 128, "description": "Teaches you how to summon the " + name + ".",
            "spellid_1": 483, "spelltrigger_1": 0, "spellcharges_1": -1, "spellcooldown_1": -1,
            "spellcategory_1": 330, "spellcategorycooldown_1": 3000, "spellid_2": ids["Spell"], "spelltrigger_2": 6},
        "item_dbc": dict(zip(("ID", "ClassID", "SubclassID", "Sound_Override_Subclassid", "Material", "DisplayInfoID", "InventoryType", "SheatheType"),
                             [item, 15, 5, -1, 4, display, 0, 0])),
        "itemdisplayinfo_dbc": {"ID": display, "InventoryIcon_1": icon_name},
        "creature_template": {"entry": creature, "name": name, "minlevel": 1, "maxlevel": 1, "faction": 35,
            "unit_class": 1, "type": 1, "unit_flags2": 2048, "HoverHeight": 1, "flags_extra": 2, "movementId": 140},
        "creature_template_model": {"CreatureID": creature, "Idx": 0, "CreatureDisplayID": creature_display,
            "DisplayScale": 1, "Probability": 1, "VerifiedBuild": 12340},
        "creature_model_info": {"DisplayID": creature_display, "BoundingRadius": 1, "CombatReach": 2,
            "Gender": 2, "DisplayID_Other_Gender": 0, "VerifiedBuild": 12340},
    }
    if localenv.use_local():
        # The native worldserver has SQL DBC overlays, but no continuation-file
        # loader. Preserve base DBC bytes and install equivalent guarded rows.
        for table, sql_table in SQL_DBC.items():
            columns = [line.split("\t") for line in schema[sql_table]["columns"]]
            field_count = n.TABLES[table][0]
            if len(columns) != field_count:
                raise ValueError(f"Native {sql_table} layout differs from {table}.dbc")
            table_rows, strings = tables[table]
            if len(table_rows) != 1: raise ValueError("One mount row per table required")
            record = {}
            for field, (column, datatype, *_) in enumerate(columns):
                raw = table_rows[0][field]
                if field in n.TABLES[table][1]:
                    if not any(t in datatype for t in ("char", "text")):
                        raise ValueError("SQL string field does not match DBC")
                    value = strings.get((0, field), "")
                elif datatype == "float":
                    value = struct.unpack("<f", struct.pack("<I", raw))[0]
                elif "int" in datatype:
                    value = raw if "unsigned" in datatype or raw < 0x80000000 else raw - 0x100000000
                else: raise ValueError(f"Unsupported native column type: {datatype}")
                record[column] = value
            records[sql_table] = record
    args.stage.mkdir(parents=True)
    texture_ids = [*recipe["texture_file_ids"], recipe["icon_file_id"], *recipe.get("dissolve_texture_ids", [])]
    acquisition_config = args.source_config.resolve()
    if args.originals:
        acquisition_config = (args.stage / "acquire.ini").resolve()
        acquisition_config.write_text(f"mode=files\npath={args.originals.resolve().as_posix()}\n"
            f"product={recipe['product']}\nbuild={recipe['build']}\nlocale=2\n", encoding="utf-8")
    subprocess.run([str(args.acquire.resolve()), str(acquisition_config), str(recipe["model_file_id"]),
                    ",".join(map(str, texture_ids)), str((args.stage / "originals").resolve())], check=True)
    if args.originals: acquisition_config.unlink()
    (args.stage / "tables").mkdir()
    for table, (table_rows, strings) in tables.items():
        (args.stage / "tables" / f"{table.lower()}.dbc").write_bytes(n.make_wdbc(n.TABLES[table][0], table_rows, strings))
    (args.stage / "equipment.csv").write_text("", encoding="ascii")
    fourth = recipe["texture_file_ids"][3] if len(recipe["texture_file_ids"]) > 3 else 0
    profile = recipe.get("dissolve_profile", 0)
    if fourth or profile:
        noise = recipe.get("dissolve_texture_ids", [0, 0, 0])
        if len(noise) != 3: raise ValueError("Three dissolve texture slots required")
        (args.stage / "creatures.csv").write_text(";".join(map(str, [creature_display, fourth, profile, *noise])) + "\n", encoding="ascii")
    (args.stage / "aliases.csv").write_text(f"{recipe['icon_file_id']};{icon_path}.blp\n", encoding="ascii")
    n.save_json(args.stage / "resolved-source.json", recipe)
    donor_row = next(row for row in rows if row[0] == donor)
    _, donor_pool = n.wdbc(donor_bytes, 234)
    donor_strings = {(0, field): donor_pool[donor_row[field]:donor_pool.index(0, donor_row[field])].decode("utf-8")
                     for field in n.TABLES["Spell"][1] if donor_row[field]}
    (args.stage / "donor-spell.dbc").write_bytes(n.make_wdbc(234, [donor_row], donor_strings))
    manifest = {"schema": 1, "slug": slug, "source_key": source_key,
        "source": {**recipe, "asset_bytes": "original-decoded-CASC", "identity_kind": "resolved-retail-mount"},
        "target": {"kind": "mount", "name": name, "item_id": item, "display_id": display, "ids": ids, "records": records},
        "baselines": {"client_providers": providers, "server_providers": server_providers,
                      "server_schema": {table: value["columns"] for table, value in schema.items()}},
        "limitations": ["Original large bone palettes need the D3D11 or Vulkan retail shader set (see retail-native-mounts.md).",
                        f"WotLK summon mechanics use donor spell {donor}; its speed auras apply, not Retail mount speeds.",
                        "Level-one learning item for testing; no Retail covenant restriction.",
                        "Retail original art bytes retained; only runtime memory views and target metadata are authored.",
                        "Summon, riding, flight, collection persistence await live acceptance."]}
    if localenv.use_local():
        manifest["target"]["server_dbc_backend"] = "local-sql"
    manifest["definition_sha256"] = n.digest(json.dumps({"source": manifest["source"], "target": manifest["target"]}, sort_keys=True).encode())
    manifest["files"] = {p.relative_to(args.stage).as_posix(): {"sha256": n.digest(p.read_bytes()), "size": p.stat().st_size}
                         for p in sorted(args.stage.rglob("*")) if p.is_file()}
    n.save_json(args.stage / "manifest.json", manifest)
    (args.stage / "server.sql").write_text(n.server_sql(manifest), encoding="utf-8")
    print(json.dumps({"stage": str(args.stage.resolve()), "item": item, "item_display": display, **ids}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ("client", "server-root", "source-config", "acquire", "stage", "recipe"):
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--originals", type=Path)
    stage(parser.parse_args())
