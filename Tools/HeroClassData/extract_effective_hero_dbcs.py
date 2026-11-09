"""Extract the active test client's effective class-related DBC inputs read-only."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


TABLES = (
    "ChrClasses.dbc",
    "CharBaseInfo.dbc",
    "gtChanceToMeleeCrit.dbc",
    "gtChanceToMeleeCritBase.dbc",
    "gtChanceToSpellCrit.dbc",
    "gtChanceToSpellCritBase.dbc",
    "gtOCTClassCombatRatingScalar.dbc",
    "gtOCTRegenHP.dbc",
    "gtRegenHPPerSpt.dbc",
    "gtRegenMPPerSpt.dbc",
    "gtOCTRegenMP.dbc",
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True, help="test client folder containing Data")
    parser.add_argument("--stormlib", type=Path, required=True, help="approved StormLib.dll")
    parser.add_argument("--output", type=Path, required=True, help="new output directory; must not exist")
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit(f"Refusing to overwrite output directory: {args.output}")
    data_dir = args.client / "Data"
    if not data_dir.is_dir():
        raise SystemExit(f"Client Data directory not found: {data_dir}")

    approved_tools = Path(__file__).resolve().parents[3] / "work" / "approved-zip-20261102" / "Kyrian-Venthyr-Approved-20261021" / "tools"
    sys.path.insert(0, str(approved_tools))
    from portable_mpq import MPQ

    root_archives = [
        path for path in data_dir.iterdir()
        if path.is_file() and path.suffix.lower() == ".mpq"
    ]
    locale_dir = data_dir / "enUS"
    locale_archives = [
        path for path in locale_dir.iterdir()
        if path.is_file() and path.suffix.lower() == ".mpq"
    ] if locale_dir.is_dir() else []
    base_archives = sorted(
        (path for path in (*root_archives, *locale_archives) if not path.name.lower().startswith("patch") and path.name.lower() != "custom.mpq"),
        key=lambda path: str(path.relative_to(data_dir)).lower(),
    )
    patch_archives = sorted(
        (path for path in (*root_archives, *locale_archives) if path.name.lower().startswith("patch")),
        key=lambda path: str(path.relative_to(data_dir)).lower(),
    )
    custom_archives = [path for path in root_archives if path.name.lower() == "custom.mpq"]
    archives = [*base_archives, *patch_archives, *custom_archives]
    if not archives:
        raise SystemExit("No root or enUS patch MPQs found")

    mpq = MPQ(str(args.stormlib))
    latest: dict[str, tuple[Path, bytes]] = {}
    for archive in archives:
        try:
            handle = mpq.open(archive)
        except OSError:
            continue
        try:
            for table in TABLES:
                try:
                    content = mpq.read(handle, "DBFilesClient\\" + table)
                except KeyError:
                    continue
                latest[table] = (archive, content)
        finally:
            mpq.close(handle)

    missing = [table for table in TABLES[:-1] if table not in latest]
    if missing:
        raise SystemExit("Required DBCs are absent from patch MPQs: " + ", ".join(missing))

    args.output.mkdir(parents=True)
    output = args.output / "DBFilesClient"
    output.mkdir()
    inputs = {}
    for table, (archive, content) in latest.items():
        (output / table).write_bytes(content)
        inputs[table] = {
            "archive": str(archive.relative_to(args.client)),
            "sha256": sha256(content),
            "bytes": len(content),
        }
    (args.output / "effective-client-dbc-inputs.json").write_text(
        json.dumps({"client": str(args.client.resolve()), "tables": inputs}, indent=2) + "\n",
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
