"""Create additive WotLK DBC overrides for Aetheria's Hero class (class ID 10).

Input must be the effective client DBFilesClient tables extracted from the exact
client build being prepared. Existing records are preserved except the reserved
class-10 rows in class-indexed game tables, which are initialized from Paladin.
Outputs are written to a new directory and never overwrite existing files.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path


HERO_CLASS = 10
DONOR_CLASS = 2
HERO_SPELL_CLASS_SET = 16
HERO_CLASS_FLAGS = 58
EXPECTED_CLASS_DBC_SHA256 = "5b2bfc7dab577f48cafe86db1d6148515569507b273f0c95b5ae86713ff5b3e2"


class Wdbc:
    def __init__(self, data: bytes, name: str):
        if len(data) < 20:
            raise ValueError(f"{name}: truncated WDBC header")
        self.name = name
        self.magic, self.count, self.fields, self.row_size, self.string_size = struct.unpack_from(
            "<4s4I", data
        )
        if self.magic != b"WDBC":
            raise ValueError(f"{name}: expected WDBC")
        self.records_end = 20 + self.count * self.row_size
        if self.records_end + self.string_size != len(data):
            raise ValueError(f"{name}: header length does not match file length")
        self.records = [
            data[20 + i * self.row_size : 20 + (i + 1) * self.row_size]
            for i in range(self.count)
        ]
        self.strings = data[self.records_end :]

    def value(self, record: bytes, field: int) -> int:
        return struct.unpack_from("<I", record, field * 4)[0]

    @staticmethod
    def replace_u32(record: bytes, field: int, value: int) -> bytes:
        result = bytearray(record)
        struct.pack_into("<I", result, field * 4, value)
        return bytes(result)

    def build(self, records: list[bytes], strings: bytes | None = None) -> bytes:
        pool = self.strings if strings is None else strings
        header = struct.pack("<4s4I", self.magic, len(records), self.fields, self.row_size, len(pool))
        return header + b"".join(records) + pool


def hero_classes(data: bytes) -> bytes:
    table = Wdbc(data, "ChrClasses.dbc")
    if (table.fields, table.row_size) != (60, 240):
        raise ValueError("ChrClasses.dbc must use the 60-column Wrath layout")
    if any(table.value(row, 0) == HERO_CLASS for row in table.records):
        raise ValueError("ChrClasses.dbc already contains class ID 10")
    donor = next((row for row in table.records if table.value(row, 0) == DONOR_CLASS), None)
    if donor is None:
        raise ValueError("ChrClasses.dbc has no Paladin donor row")

    strings = bytearray(table.strings)
    token_offset = len(strings)
    strings.extend(b"HERO\0")
    name_offset = len(strings)
    strings.extend(b"Hero\0")
    row = Wdbc.replace_u32(donor, 0, HERO_CLASS)
    row = Wdbc.replace_u32(row, 2, 0)  # mana, matching the Aetheria classless chassis
    for field in (3, 55):
        row = Wdbc.replace_u32(row, field, token_offset)
    for field in (*range(4, 20), *range(21, 37), *range(38, 54)):
        row = Wdbc.replace_u32(row, field, name_offset)
    row = Wdbc.replace_u32(row, 56, HERO_SPELL_CLASS_SET)
    row = Wdbc.replace_u32(row, 57, HERO_CLASS_FLAGS)
    row = Wdbc.replace_u32(row, 58, 0)
    row = Wdbc.replace_u32(row, 59, 0)
    return table.build([*table.records, row], bytes(strings))


def hero_race_pairs(data: bytes) -> bytes:
    table = Wdbc(data, "CharBaseInfo.dbc")
    if (table.fields, table.row_size) != (2, 2):
        raise ValueError("CharBaseInfo.dbc must use the two-byte Wrath layout")
    pairs = {(row[0], row[1]) for row in table.records}
    races = sorted({race for race, _ in pairs})
    if not races:
        raise ValueError("CharBaseInfo.dbc has no race/class pairs")
    missing = [(race, HERO_CLASS) for race in races if (race, HERO_CLASS) not in pairs]
    return table.build([*table.records, *(bytes(pair) for pair in missing)])


def replace_class_rows(data: bytes, name: str, rows_per_class: int) -> bytes:
    table = Wdbc(data, name)
    if (table.fields, table.row_size, table.count) != (1, 4, 11 * rows_per_class):
        raise ValueError(f"{name}: unexpected class-indexed game-table layout")
    records = list(table.records)
    target_start = (HERO_CLASS - 1) * rows_per_class
    donor_start = (DONOR_CLASS - 1) * rows_per_class
    records[target_start : target_start + rows_per_class] = records[
        donor_start : donor_start + rows_per_class
    ]
    return table.build(records)


def replace_class_base(data: bytes, name: str) -> bytes:
    table = Wdbc(data, name)
    if (table.fields, table.row_size, table.count) != (1, 4, 11):
        raise ValueError(f"{name}: expected one scalar per class")
    records = list(table.records)
    records[HERO_CLASS - 1] = records[DONOR_CLASS - 1]
    return table.build(records)


def replace_rating_rows(data: bytes) -> bytes:
    table = Wdbc(data, "gtOCTClassCombatRatingScalar.dbc")
    if (table.fields, table.row_size, table.count) != (2, 8, 352):
        raise ValueError("gtOCTClassCombatRatingScalar.dbc: unexpected layout")
    records = list(table.records)
    by_id = {table.value(row, 0): index for index, row in enumerate(records)}
    for rating in range(32):
        donor_id = (DONOR_CLASS - 1) * 32 + rating + 1
        hero_id = (HERO_CLASS - 1) * 32 + rating + 1
        if donor_id not in by_id or hero_id not in by_id:
            raise ValueError("rating table is missing a donor or reserved Hero row")
        source = records[by_id[donor_id]]
        records[by_id[hero_id]] = Wdbc.replace_u32(source, 0, hero_id)
    return table.build(records)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True, help="effective client DBFilesClient directory")
    parser.add_argument("--output", type=Path, required=True, help="new output directory; must not exist")
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit(f"Refusing to overwrite output directory: {args.output}")

    required = {
        "ChrClasses.dbc": lambda b: hero_classes(b),
        "CharBaseInfo.dbc": lambda b: hero_race_pairs(b),
        "gtChanceToMeleeCrit.dbc": lambda b: replace_class_rows(b, "gtChanceToMeleeCrit.dbc", 100),
        "gtChanceToMeleeCritBase.dbc": lambda b: replace_class_base(b, "gtChanceToMeleeCritBase.dbc"),
        "gtChanceToSpellCrit.dbc": lambda b: replace_class_rows(b, "gtChanceToSpellCrit.dbc", 100),
        "gtChanceToSpellCritBase.dbc": lambda b: replace_class_base(b, "gtChanceToSpellCritBase.dbc"),
        "gtOCTClassCombatRatingScalar.dbc": replace_rating_rows,
        "gtOCTRegenHP.dbc": lambda b: replace_class_rows(b, "gtOCTRegenHP.dbc", 100),
        "gtRegenHPPerSpt.dbc": lambda b: replace_class_rows(b, "gtRegenHPPerSpt.dbc", 100),
        "gtRegenMPPerSpt.dbc": lambda b: replace_class_rows(b, "gtRegenMPPerSpt.dbc", 100),
    }
    input_hashes = {name: sha256(args.input / name) for name in required}
    if input_hashes["ChrClasses.dbc"] != EXPECTED_CLASS_DBC_SHA256:
        raise SystemExit("Input ChrClasses.dbc does not match the Acore2/test-client paired baseline")

    args.output.mkdir(parents=True)
    outputs = args.output / "DBFilesClient"
    outputs.mkdir()
    output_hashes = {}
    for name, transform in required.items():
        result = transform((args.input / name).read_bytes())
        target = outputs / name
        target.write_bytes(result)
        output_hashes[name] = hashlib.sha256(result).hexdigest()

    optional_name = "gtOCTRegenMP.dbc"
    optional_path = args.input / optional_name
    if optional_path.exists():
        result = replace_class_rows(optional_path.read_bytes(), optional_name, 100)
        (outputs / optional_name).write_bytes(result)
        input_hashes[optional_name] = sha256(optional_path)
        output_hashes[optional_name] = hashlib.sha256(result).hexdigest()

    receipt = {
        "class_id": HERO_CLASS,
        "donor_class_id": DONOR_CLASS,
        "spell_class_set": HERO_SPELL_CLASS_SET,
        "input_directory": str(args.input.resolve()),
        "output_directory": str(outputs.resolve()),
        "input_sha256": input_hashes,
        "output_sha256": output_hashes,
    }
    (args.output / "hero-dbc-receipt.json").write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
