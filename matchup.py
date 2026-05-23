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


def _prompt_fighter_found(db: UfcDb, label: str) -> str:
    while True:
        name = _prompt_fighter(label)
        if db.get_fighter_by_name(name):
            return name
        print(f"Fighter not found: {name}", file=sys.stderr)


def _resolve_names(db: UfcDb, fighters: list[str]) -> tuple[str, str] | None:
    if len(fighters) == 2:
        name_a, name_b = fighters
        if not db.get_fighter_by_name(name_a):
            print(f"Fighter not found: {name_a}", file=sys.stderr)
            return None
        return name_a, name_b

    if len(fighters) == 0:
        print("Enter two fighter names (exact spelling).\n")
        name_a = _prompt_fighter_found(db, "Fighter A")
        name_b = _prompt_fighter_found(db, "Fighter B")
        return name_a, name_b

    print("Provide exactly two fighter names, or none for interactive mode.", file=sys.stderr)
    return None


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

    if len(args.fighters) not in (0, 2):
        print("Provide exactly two fighter names, or none for interactive mode.", file=sys.stderr)
        return 2

    if not args.db.is_file():
        print(f"Database not found: {args.db}", file=sys.stderr)
        return 1

    try:
        with UfcDb(args.db, lib_path=args.lib) as db:
            names = _resolve_names(db, args.fighters)
            if names is None:
                return 1
            name_a, name_b = names
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
