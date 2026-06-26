"""Shared matchup analysis logic for CLI and HTTP API."""

from __future__ import annotations

import sqlite3
from pathlib import Path
from typing import Any

from archetype_matchup_history import attach_archetype_history, get_archetype_index
from score_breakdown import (
    build_momentum_breakdown,
    build_resume_breakdown,
    momentum_breakdown_to_structured,
    resume_breakdown_to_structured,
)
from tale_of_the_tape import history_to_structured, matchup_to_structured
from ufc_db_ffi import UfcDb
from win_probability import attach_win_probability, both_fighters_have_fight_history

_ROOT = Path(__file__).resolve().parents[1]


def default_db_path() -> Path:
    return _ROOT / "ufc.db"


def search_fighters(db_path: Path, query: str, *, limit: int = 20) -> list[dict[str, Any]]:
    q = query.strip()
    if not q:
        return []
    pattern = f"%{q}%"
    with sqlite3.connect(db_path) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            """
            SELECT name, weight_class
            FROM fighters
            WHERE lower(name) LIKE lower(?)
            ORDER BY name
            LIMIT ?
            """,
            (pattern, limit),
        ).fetchall()
    return [{"name": row["name"], "weight_class": row["weight_class"]} for row in rows]


def analyze_matchup(
    fighter_a: str,
    fighter_b: str,
    *,
    db_path: Path | None = None,
    lib_path: Path | None = None,
) -> dict[str, Any]:
    db_path = db_path or default_db_path()
    if not db_path.is_file():
        raise FileNotFoundError(f"Database not found: {db_path}")

    with UfcDb(db_path, lib_path=lib_path) as db:
        if not db.get_fighter_by_name(fighter_a):
            raise LookupError(f"Fighter not found: {fighter_a}")
        if not db.get_fighter_by_name(fighter_b):
            raise LookupError(f"Fighter not found: {fighter_b}")

        matchup = db.get_matchup_by_names(fighter_a, fighter_b)
        index = get_archetype_index(db_path, lib_path=lib_path)
        attach_archetype_history(matchup, index)
        similar_matchups = db.find_similar_matchups_by_names(fighter_a, fighter_b)
        attach_win_probability(
            matchup,
            similar_matchups,
            archetype_index=index,
            db=db,
        )

        no_prediction_reason: str | None = None
        if "win_probability" not in matchup:
            id_a = int(matchup["fighter_a"]["id"])
            id_b = int(matchup["fighter_b"]["id"])
            if not both_fighters_have_fight_history(db_path, id_a, id_b):
                no_prediction_reason = (
                    "Both fighters need at least 1 fight in the database."
                )

        id_a = int(matchup["fighter_a"]["id"])
        id_b = int(matchup["fighter_b"]["id"])

    with sqlite3.connect(db_path) as conn:
        resume_a = resume_breakdown_to_structured(build_resume_breakdown(conn, id_a))
        resume_b = resume_breakdown_to_structured(build_resume_breakdown(conn, id_b))
        momentum_a = momentum_breakdown_to_structured(
            build_momentum_breakdown(conn, id_a)
        )
        momentum_b = momentum_breakdown_to_structured(
            build_momentum_breakdown(conn, id_b)
        )

    return {
        "tape": matchup_to_structured(matchup),
        "history": history_to_structured(similar_matchups),
        "no_prediction_reason": no_prediction_reason,
        "resume_breakdown": {
            "fighter_a": resume_a,
            "fighter_b": resume_b,
        },
        "momentum_breakdown": {
            "fighter_a": momentum_a,
            "fighter_b": momentum_b,
        },
    }
