"""ctypes bindings for include/ufc_db.h (shared library ufc_db)."""

from __future__ import annotations

import json
import sys
from ctypes import CDLL, c_char_p, c_int, c_longlong, c_void_p
from pathlib import Path


def _default_library_name() -> str:
    if sys.platform == "win32":
        return "ufc_db.dll"
    if sys.platform == "darwin":
        return "libufc_db.dylib"
    return "libufc_db.so"


class UfcDb:
    def __init__(self, db_path: str | Path, lib_path: str | Path | None = None) -> None:
        if lib_path is None:
            lib_path = Path(__file__).resolve().parents[1] / "build" / _default_library_name()
        self._lib = CDLL(str(lib_path))
        self._configure_signatures()

        self._lib.ufc_db_open.restype = c_void_p
        self._handle = self._lib.ufc_db_open(str(db_path).encode("utf-8"))
        if not self._handle:
            raise RuntimeError(self.last_error())

    def _configure_signatures(self) -> None:
        lib = self._lib
        lib.ufc_db_close.argtypes = [c_void_p]
        lib.ufc_free_string.argtypes = [c_char_p]
        lib.ufc_last_error.restype = c_char_p

        lib.ufc_get_fighter_by_name.argtypes = [c_void_p, c_char_p]
        lib.ufc_get_fighter_by_name.restype = c_char_p

    def last_error(self) -> str:
        raw = self._lib.ufc_last_error()
        return raw.decode("utf-8") if raw else ""

    def _take_json(self, ptr: c_char_p):
        if not ptr:
            return None
        try:
            return json.loads(ptr.decode("utf-8"))
        finally:
            self._lib.ufc_free_string(ptr)

    def get_fighter_by_name(self, name: str) -> dict | None:
        ptr = self._lib.ufc_get_fighter_by_name(self._handle, name.encode("utf-8"))
        return self._take_json(ptr)

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
