#!/usr/bin/env python3
"""
Classify fighter archetypes from career round stats and write them to the database.

Archetypes: Pressure Striker, Ground Control Specialist, Ground Finisher, All-Around Fighter, Counter Striker

Usage:
    python python/classify_archetypes.py
    python python/classify_archetypes.py --db ufc.db
"""

from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "python"))

from ufc_db_ffi import UfcDb  # noqa: E402


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Classify and store fighter archetypes.")
    parser.add_argument("--db", type=Path, default=_ROOT / "ufc.db", help="SQLite database path")
    parser.add_argument("--lib", type=Path, default=None, help="Path to ufc_db shared library")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.db.is_file():
        print(f"Database not found: {args.db}", file=sys.stderr)
        return 1

    conn = sqlite3.connect(args.db)
    fighter_ids = [row[0] for row in conn.execute("SELECT id FROM fighters ORDER BY id")]
    classified = 0
    skipped = 0

    try:
        with UfcDb(args.db, lib_path=args.lib) as store:
            for fighter_id in fighter_ids:
                label = store.classify_archetype_by_fighter_id(fighter_id)
                if not label:
                    skipped += 1
                    continue
                conn.execute("UPDATE fighters SET archetype = ? WHERE id = ?", (label, fighter_id))
                classified += 1
        conn.commit()
    finally:
        conn.close()

    print(f"Classified {classified} fighters; skipped {skipped} (insufficient round data).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
