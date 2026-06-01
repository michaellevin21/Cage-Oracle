"""ctypes bindings for include/ufc_db.h (shared library ufc_db)."""

from __future__ import annotations

import json
import os
import sys
from ctypes import CDLL, POINTER, Structure, byref, c_char_p, c_double, c_int, c_longlong, c_void_p, string_at
from pathlib import Path


class UfcCareerTotals(Structure):
    _fields_ = [
        ("rounds", c_int),
        ("sig_strikes_landed", c_int),
        ("sig_strikes_attempted", c_int),
        ("total_strikes_landed", c_int),
        ("total_strikes_attempted", c_int),
        ("takedowns_landed", c_int),
        ("takedowns_attempted", c_int),
        ("opponent_sig_strikes_landed", c_int),
        ("opponent_sig_strikes_attempted", c_int),
        ("opponent_takedowns_landed", c_int),
        ("opponent_takedowns_attempted", c_int),
        ("sub_attempts", c_int),
        ("reversals", c_int),
        ("knockdowns", c_int),
        ("control_time_seconds", c_double),
        ("head_strikes_landed", c_int),
        ("body_strikes_landed", c_int),
        ("leg_strikes_landed", c_int),
        ("distance_strikes_landed", c_int),
        ("clinch_strikes_landed", c_int),
        ("ground_strikes_landed", c_int),
    ]


def _default_library_name() -> str:
    if sys.platform == "win32":
        return "ufc_db.dll"
    if sys.platform == "darwin":
        return "libufc_db.dylib"
    return "libufc_db.so"


def _resolve_library_path(explicit: str | Path | None) -> Path:
    if explicit is not None:
        return Path(explicit)

    root = Path(__file__).resolve().parents[1]
    name = _default_library_name()
    candidates = [
        root / "build" / name,
        root / "build" / "Release" / name,
        root / "build" / "Debug" / name,
    ]
    for path in candidates:
        if path.is_file():
            return path
    return candidates[0]


class UfcDb:
    def __init__(self, db_path: str | Path, lib_path: str | Path | None = None) -> None:
        lib_path = _resolve_library_path(lib_path)
        if not lib_path.is_file():
            raise FileNotFoundError(
                f"ufc_db shared library not found at {lib_path}. "
                "Build the C++ project first (cmake --build build --config Release)."
            )

        if sys.platform == "win32" and hasattr(os, "add_dll_directory"):
            os.add_dll_directory(str(lib_path.parent))

        self._lib = CDLL(str(lib_path))
        self._configure_signatures()

        self._lib.ufc_db_open.restype = c_void_p
        self._handle = self._lib.ufc_db_open(str(db_path).encode("utf-8"))
        if not self._handle:
            raise RuntimeError(self.last_error())

    def _configure_signatures(self) -> None:
        lib = self._lib
        lib.ufc_db_close.argtypes = [c_void_p]
        lib.ufc_free_string.argtypes = [c_void_p]
        lib.ufc_last_error.restype = c_char_p

        for name in (
            "ufc_get_fighter_by_name",
            "ufc_get_matchup_by_names",
            "ufc_get_matchup_by_ids",
        ):
            fn = getattr(lib, name)
            fn.restype = c_void_p
        lib.ufc_get_fighter_by_name.argtypes = [c_void_p, c_char_p]
        lib.ufc_get_matchup_by_names.argtypes = [c_void_p, c_char_p, c_char_p]
        lib.ufc_get_matchup_by_ids.argtypes = [c_void_p, c_longlong, c_longlong]
        lib.ufc_classify_archetype_by_fighter_id.restype = c_void_p
        lib.ufc_classify_archetype_by_fighter_id.argtypes = [c_void_p, c_longlong]
        lib.ufc_classify_archetype_from_totals.restype = c_void_p
        lib.ufc_classify_archetype_from_totals.argtypes = [c_void_p]
        lib.ufc_compute_momentum_by_fighter_id_out.argtypes = [
            c_void_p,
            c_longlong,
            POINTER(c_double),
        ]
        lib.ufc_compute_momentum_by_fighter_id_out.restype = c_int

    def last_error(self) -> str:
        raw = self._lib.ufc_last_error()
        return raw.decode("utf-8") if raw else ""

    def _take_json(self, ptr: int | None):
        if not ptr:
            return None
        try:
            return json.loads(string_at(ptr).decode("utf-8"))
        finally:
            self._lib.ufc_free_string(ptr)

    def get_fighter_by_name(self, name: str) -> dict | None:
        ptr = self._lib.ufc_get_fighter_by_name(self._handle, name.encode("utf-8"))
        return self._take_json(ptr)

    def get_matchup_by_names(self, fighter_a: str, fighter_b: str) -> dict | None:
        ptr = self._lib.ufc_get_matchup_by_names(
            self._handle, fighter_a.encode("utf-8"), fighter_b.encode("utf-8")
        )
        if not ptr:
            raise LookupError(self.last_error() or "matchup lookup failed")
        data = self._take_json(ptr)
        if data is None:
            raise LookupError(self.last_error() or "matchup lookup failed")
        return data

    def get_matchup_by_ids(self, fighter_a_id: int, fighter_b_id: int) -> dict | None:
        ptr = self._lib.ufc_get_matchup_by_ids(
            self._handle, fighter_a_id, fighter_b_id
        )
        if not ptr:
            raise LookupError(self.last_error() or "matchup lookup failed")
        data = self._take_json(ptr)
        if data is None:
            raise LookupError(self.last_error() or "matchup lookup failed")
        return data

    def classify_archetype_by_fighter_id(self, fighter_id: int) -> str | None:
        ptr = self._lib.ufc_classify_archetype_by_fighter_id(self._handle, fighter_id)
        if not ptr:
            return None
        try:
            return string_at(ptr).decode("utf-8")
        finally:
            self._lib.ufc_free_string(ptr)

    def classify_archetype_from_totals(self, totals: UfcCareerTotals) -> str | None:
        ptr = self._lib.ufc_classify_archetype_from_totals(byref(totals))
        if not ptr:
            return None
        try:
            return string_at(ptr).decode("utf-8")
        finally:
            self._lib.ufc_free_string(ptr)

    def compute_momentum_by_fighter_id(self, fighter_id: int) -> float | None:
        score = c_double()
        ok = self._lib.ufc_compute_momentum_by_fighter_id_out(
            self._handle, fighter_id, byref(score)
        )
        if not ok:
            return None
        return float(score.value)

    def close(self) -> None:
        if self._handle:
            self._lib.ufc_db_close(self._handle)
            self._handle = None

    def __enter__(self) -> "UfcDb":
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    def __del__(self) -> None:
        self.close()
