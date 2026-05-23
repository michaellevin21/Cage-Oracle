#!/usr/bin/env python3
"""
Tale-of-the-tape CLI — compare two UFC fighters using the C++ matchup engine.

Examples:
    python matchup.py "Jon Jones" "Daniel Cormier"
    python matchup.py
    python matchup.py --db ufc.db "Israel Adesanya" "Robert Whittaker"
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(_ROOT / "python"))

from tale_of_the_tape import render_matchup  # noqa: E402
from ufc_db_ffi import UfcDb  # noqa: E402


def _default_db_path() -> Path:
    return _ROOT / "ufc.db"


def _prompt_fighter(label: str) -> str:
    while True:
        name = input(f"{label}: ").strip()
        if name:
            return name
        print("Name cannot be empty.", file=sys.stderr)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Print a side-by-side tale of the tape for two UFC fighters."
    )
    parser.add_argument(
        "fighters",
        nargs="*",
        metavar="NAME",
        help="Exactly two fighter names (must match the database exactly).",
    )
    parser.add_argument(
        "--db",
        type=Path,
        default=_default_db_path(),
        help=f"SQLite database path (default: {_default_db_path()})",
    )
    parser.add_argument(
        "--lib",
        type=Path,
        default=None,
        help="Path to ufc_db shared library (auto-detected under build/ by default)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    if len(args.fighters) == 2:
        name_a, name_b = args.fighters
    elif len(args.fighters) == 0:
        print("Enter two fighter names (exact spelling as in ufc.db).\n")
        name_a = _prompt_fighter("Fighter A")
        name_b = _prompt_fighter("Fighter B")
    else:
        print("Provide exactly two fighter names, or none for interactive mode.", file=sys.stderr)
        return 2

    if not args.db.is_file():
        print(f"Database not found: {args.db}", file=sys.stderr)
        return 1

    try:
        with UfcDb(args.db, lib_path=args.lib) as db:
            matchup = db.get_matchup_by_names(name_a, name_b)
    except FileNotFoundError as exc:
        print(exc, file=sys.stderr)
        return 1
    except LookupError as exc:
        print(exc, file=sys.stderr)
        return 1
    except RuntimeError as exc:
        print(exc, file=sys.stderr)
        return 1

    print(render_matchup(matchup))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
