#!/usr/bin/env python3
"""
Find comparable historical matchups via cosine similarity.

Example:
    python python/find_similar.py "Jon Jones" "Daniel Cormier"
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "python"))

from tale_of_the_tape import render_matchup_history  # noqa: E402
from ufc_db_ffi import UfcDb  # noqa: E402


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Find comparable historical matchups for a fighter pair."
    )
    parser.add_argument(
        "fighters",
        nargs=2,
        metavar="NAME",
        help="Two fighter names (must match the database exactly).",
    )
    parser.add_argument("--db", type=Path, default=_ROOT / "ufc.db", help="SQLite database path")
    parser.add_argument("--lib", type=Path, default=None, help="Path to ufc_db shared library")
    parser.add_argument("--top", type=int, default=5, help="Number of results (default: 5)")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if not args.db.is_file():
        print(f"Database not found: {args.db}", file=sys.stderr)
        return 1

    name_a, name_b = args.fighters
    try:
        with UfcDb(args.db, lib_path=args.lib) as db:
            if not db.get_fighter_by_name(name_a):
                print(f"Fighter not found: {name_a}", file=sys.stderr)
                return 1
            if not db.get_fighter_by_name(name_b):
                print(f"Fighter not found: {name_b}", file=sys.stderr)
                return 1
            result = db.find_similar_matchups_by_names(name_a, name_b, top_k=args.top)
            print(render_matchup_history(result), end="")
    except FileNotFoundError as exc:
        print(exc, file=sys.stderr)
        return 1
    except LookupError as exc:
        print(exc, file=sys.stderr)
        return 1
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
