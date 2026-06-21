#!/usr/bin/env python3
"""
Compute resume scores and write them to the database.

Resume score = sum of points from career wins over opponents who are currently
ranked in a UFC weight class (pound-for-pound listings ignored; unranked wins
contribute nothing). Opponents listed in multiple weight classes use only their
best rank (champion = 16 pts, #1 = 15, …, #15 = 1).

Usage:
    python python/compute_resume.py
    python python/compute_resume.py --db ufc.db
"""

from __future__ import annotations

import argparse
import sqlite3
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "python"))

from ufc_db_ffi import UfcDb  # noqa: E402


def ensure_resume_score_column(conn: sqlite3.Connection) -> None:
    columns = {row[1] for row in conn.execute("PRAGMA table_info(fighters)")}
    if "resume_score" not in columns:
        conn.execute("ALTER TABLE fighters ADD COLUMN resume_score REAL")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compute and store fighter resume scores.")
    parser.add_argument("--db", type=Path, default=_ROOT / "ufc.db", help="SQLite database path")
    parser.add_argument("--lib", type=Path, default=None, help="Path to ufc_db shared library")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if not args.db.is_file():
        print(f"Database not found: {args.db}", file=sys.stderr)
        return 1

    conn = sqlite3.connect(args.db)
    ensure_resume_score_column(conn)
    conn.commit()

    fighter_ids = [row[0] for row in conn.execute("SELECT id FROM fighters ORDER BY id")]

    try:
        with UfcDb(args.db, lib_path=args.lib) as store:
            for fighter_id in fighter_ids:
                score = store.compute_resume_by_fighter_id(fighter_id)
                conn.execute(
                    "UPDATE fighters SET resume_score = ? WHERE id = ?",
                    (score, fighter_id),
                )
        conn.commit()
    finally:
        conn.close()

    print(f"Computed resume scores for {len(fighter_ids)} fighters.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
