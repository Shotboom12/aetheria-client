"""Build paired CoA spell-charge DBCs alongside the prepared Hero DBC overrides.

The server metadata header is pinned to Kalibros' CoASpellChargesData.h at
bdad58028fb6847b9d9a4fd8cf990bd662c9fc62. This script preserves the supplied
Hero DBCs and adds the matching client category/link tables in a new directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path


UPSTREAM_COMMIT = "bdad58028fb6847b9d9a4fd8cf990bd662c9fc62"
EXPECTED_ENTRIES = 162
ENTRY = re.compile(r"\{(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}")


def read_catalog(path: Path) -> list[tuple[int, int, int, int, int]]:
    text = path.read_text(encoding="utf-8")
    rows = [tuple(map(int, match)) for match in ENTRY.findall(text)]
    if len(rows) != EXPECTED_ENTRIES:
        raise ValueError(f"Expected {EXPECTED_ENTRIES} pinned charge rows; found {len(rows)}")
    if any(left[0] >= right[0] for left, right in zip(rows, rows[1:])):
        raise ValueError("Charge spell IDs must be strictly sorted and unique")

    pools: dict[int, tuple[int, int]] = {}
    for spell, maximum, recovery, root, category in rows:
        if not all((spell, maximum, recovery, root, category)):
            raise ValueError(f"Charge row for spell {spell} contains a zero field")
        if maximum > 20 or recovery > 86_400_000:
            raise ValueError(f"Charge row for spell {spell} exceeds client limits")
        definition = (maximum, recovery)
        if root in pools and pools[root] != definition:
            raise ValueError(f"Recovery root {root} has conflicting max/recovery definitions")
        pools[root] = definition
    return rows


def make_dbc(rows: list[tuple[int, ...]], fields: int) -> bytes:
    records = b"".join(struct.pack("<" + "I" * fields, *row) for row in rows)
    strings = b"\0"
    header = struct.pack("<4s4I", b"WDBC", len(rows), fields, fields * 4, len(strings))
    return header + records + strings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--metadata", type=Path, required=True, help="pinned server CoASpellChargesData.h")
    parser.add_argument("--base-dbc-dir", type=Path, required=True, help="prepared Hero DBFilesClient directory")
    parser.add_argument("--output-dir", type=Path, required=True, help="new combined DBFilesClient directory")
    args = parser.parse_args()

    if not args.base_dbc_dir.is_dir():
        raise SystemExit(f"Prepared Hero DBC directory not found: {args.base_dbc_dir}")
    if args.output_dir.exists():
        raise SystemExit(f"Refusing to overwrite output directory: {args.output_dir}")

    rows = read_catalog(args.metadata)
    pool_definitions = {root: (maximum, recovery) for _spell, maximum, recovery, root, _category in rows}
    categories = sorted((root, maximum, recovery) for root, (maximum, recovery) in pool_definitions.items())
    links = [(spell, root) for spell, _maximum, _recovery, root, _category in rows]

    base_files = sorted(path for path in args.base_dbc_dir.iterdir()
                        if path.is_file() and path.suffix.lower() == ".dbc")
    names = {path.name.lower() for path in base_files}
    if {"spellcharges.dbc", "spellchargescategory.dbc"} & names:
        raise SystemExit("Base Hero DBC directory already contains charge tables")

    args.output_dir.mkdir(parents=True)
    for path in base_files:
        shutil.copy2(path, args.output_dir / path.name)

    payloads = {
        "SpellCharges.dbc": make_dbc(links, 2),
        "SpellChargesCategory.dbc": make_dbc(categories, 3),
    }
    for name, payload in payloads.items():
        (args.output_dir / name).write_bytes(payload)

    receipt = {
        "upstream": f"STRHercules/EsteriaWoW@{UPSTREAM_COMMIT}/modules/mod-classless-wildcard/src/coa/CoASpellChargesData.h",
        "metadata_sha256": hashlib.sha256(args.metadata.read_bytes()).hexdigest(),
        "spell_links": len(links),
        "charge_pools": len(categories),
        "dbc_sha256": {name: hashlib.sha256(data).hexdigest() for name, data in payloads.items()},
    }
    (args.output_dir / "spell-charge-dbc-receipt.json").write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
