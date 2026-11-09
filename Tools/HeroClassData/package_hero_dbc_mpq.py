"""Package generated Hero DBC overrides and optional client UI payload in a new MPQ."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


ARCHIVE_NAME = "Patch-" + ("z" * 40) + "-AetheriaHeroIntegration.MPQ"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dbc-dir", type=Path, required=True, help="generated DBFilesClient directory")
    parser.add_argument("--stormlib", type=Path, required=True, help="approved StormLib.dll")
    parser.add_argument("--output-dir", type=Path, required=True, help="new directory for the MPQ and receipt")
    parser.add_argument("--payload-dir", type=Path, help="optional root of additional Interface/ asset payloads")
    args = parser.parse_args()
    if args.output_dir.exists():
        raise SystemExit(f"Refusing to overwrite output directory: {args.output_dir}")
    files = sorted(path for path in args.dbc_dir.iterdir() if path.is_file() and path.suffix.lower() == ".dbc")
    if not files:
        raise SystemExit("No generated DBC files found")

    approved_tools = Path(__file__).resolve().parents[3] / "work" / "approved-zip-20261102" / "Kyrian-Venthyr-Approved-20261021" / "tools"
    sys.path.insert(0, str(approved_tools))
    from portable_mpq import MPQ

    entries = {"DBFilesClient\\" + path.name: path.read_bytes() for path in files}
    if args.payload_dir:
        if not args.payload_dir.is_dir():
            raise SystemExit(f"Payload directory not found: {args.payload_dir}")
        for path in sorted(item for item in args.payload_dir.rglob("*") if item.is_file()):
            name = str(path.relative_to(args.payload_dir)).replace("/", "\\")
            if name in entries:
                raise SystemExit(f"Duplicate MPQ entry: {name}")
            entries[name] = path.read_bytes()
    args.output_dir.mkdir(parents=True)
    archive = args.output_dir / ARCHIVE_NAME
    mpq = MPQ(str(args.stormlib))
    mpq.create(archive, entries)
    receipt = {
        "archive": str(archive.resolve()),
        "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "entry_sha256": {name: hashlib.sha256(content).hexdigest() for name, content in entries.items()},
        "note": "Standalone client data/UI patch; the active client was not modified.",
    }
    (args.output_dir / "hero-dbc-mpq-receipt.json").write_text(
        json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
