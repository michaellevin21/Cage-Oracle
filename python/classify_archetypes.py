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
from ctypes import CDLL, c_char_p, c_longlong, c_void_p, string_at
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "python"))

from ufc_db_ffi import _resolve_library_path  # noqa: E402


def _load_lib(lib_path: Path | None) -> CDLL:
    path = _resolve_library_path(lib_path)
    if not path.is_file():
        raise FileNotFoundError(
            f"ufc_db shared library not found at {path}. "
            "Build the C++ project first (cmake --build build --config Release)."
        )
    lib = CDLL(str(path))
    lib.ufc_db_open.restype = c_void_p
    lib.ufc_db_close.argtypes = [c_void_p]
    lib.ufc_free_string.argtypes = [c_void_p]
    lib.ufc_classify_archetype_by_fighter_id.restype = c_void_p
    lib.ufc_classify_archetype_by_fighter_id.argtypes = [c_void_p, c_longlong]
    return lib


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

    lib = _load_lib(args.lib)
    handle = lib.ufc_db_open(str(args.db).encode("utf-8"))
    if not handle:
        print("Failed to open database via ufc_db.", file=sys.stderr)
        return 1

    conn = sqlite3.connect(args.db)
    fighter_ids = [row[0] for row in conn.execute("SELECT id FROM fighters ORDER BY id")]
    classified = 0
    skipped = 0

    try:
        for fighter_id in fighter_ids:
            ptr = lib.ufc_classify_archetype_by_fighter_id(handle, fighter_id)
            if not ptr:
                skipped += 1
                continue
            label = string_at(ptr).decode("utf-8")
            lib.ufc_free_string(ptr)
            conn.execute("UPDATE fighters SET archetype = ? WHERE id = ?", (label, fighter_id))
            classified += 1
        conn.commit()
    finally:
        lib.ufc_db_close(handle)
        conn.close()

    print(f"Classified {classified} fighters; skipped {skipped} (insufficient round data).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
