#!/usr/bin/env python3
"""
Compute recency-weighted momentum scores and write them to the database.

Usage:
    python python/compute_momentum.py
    python python/compute_momentum.py --db ufc.db
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
    parser = argparse.ArgumentParser(description="Compute and store fighter momentum scores.")
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
    computed = 0
    cleared = 0

    try:
        with UfcDb(args.db, lib_path=args.lib) as store:
            for fighter_id in fighter_ids:
                score = store.compute_momentum_by_fighter_id(fighter_id)
                if score is None:
                    conn.execute(
                        "UPDATE fighters SET momentum_score = NULL WHERE id = ?",
                        (fighter_id,),
                    )
                    cleared += 1
                else:
                    conn.execute(
                        "UPDATE fighters SET momentum_score = ? WHERE id = ?",
                        (score, fighter_id),
                    )
                    computed += 1
        conn.commit()
    finally:
        conn.close()

    print(f"Computed momentum for {computed} fighters; cleared {cleared} (insufficient fight data).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
