#!/usr/bin/env python3
"""
Visual breakdown of a fighter's momentum and resume scores.

Usage:
    python python/score_breakdown.py "Jon Jones"
    python python/score_breakdown.py
    python python/score_breakdown.py --db ufc.db "Alexander Volkanovski"
"""

from __future__ import annotations

import argparse
import sqlite3
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(_ROOT / "python"))

from ufc_db_ffi import UfcDb  # noqa: E402

# ── Momentum constants (match src/FighterMomentum.cpp) ────────────────────────
MAX_RECENT_FIGHTS = 5
MIN_DECISIVE_FIGHTS = 3
SECONDS_PER_DAY = 86400
RECENCY_FULL_WEIGHT_DAYS = 365
INACTIVITY_DAYS = 730
RECENCY_HALF_LIFE_DAYS = 365.0
MIN_RECENCY_WEIGHT = 0.12
UNRANKED_OPPONENT_QUALITY = 0.35
TITLE_FIGHT_QUALITY_BONUS = 0.12
FINISH_WIN_MULTIPLIER = 1.30
FINISH_RATE_BOOST_WEIGHT = 0.20
LOSS_CONTRIBUTION_BASE = 1.0
NEUTRAL_SCORE = 50.0
MAX_FIGHT_CONTRIBUTION = 1.30

# ── Resume constants (match src/FighterResume.cpp) ──────────────────────────
MAX_RANKED_SLOT = 15
CHAMPION_POINTS = 16

NON_DECISIVE_METHODS = frozenset({"CNC", "Overturned", "Other", "DRAW"})


def _default_db_path() -> Path:
    return _ROOT / "ufc.db"


def is_finish_method(method: str) -> bool:
    return "KO" in method or "TKO" in method or "SUB" in method


def is_non_decisive(method: str | None) -> bool:
    return method in NON_DECISIVE_METHODS


def recency_weight(event_date: int, now: int) -> float:
    days_since = (now - event_date) / SECONDS_PER_DAY
    if days_since <= RECENCY_FULL_WEIGHT_DAYS:
        return 1.0
    days_past_grace = days_since - RECENCY_FULL_WEIGHT_DAYS
    weight = 0.5 ** (days_past_grace / RECENCY_HALF_LIFE_DAYS)
    return max(MIN_RECENCY_WEIGHT, weight)


def best_opponent_rank(conn: sqlite3.Connection, opponent_id: int) -> int | None:
    row = conn.execute(
        "SELECT MIN(rank) FROM fighter_rankings WHERE fighter_id = ?",
        (opponent_id,),
    ).fetchone()
    return None if row[0] is None else int(row[0])


def best_weight_class_rank(conn: sqlite3.Connection, opponent_id: int) -> int | None:
    row = conn.execute(
        """
        SELECT MIN(rank) FROM fighter_rankings
        WHERE fighter_id = ?
          AND LOWER(weight_class) NOT LIKE '%pound-for-pound%'
        """,
        (opponent_id,),
    ).fetchone()
    return None if row[0] is None else int(row[0])


def rank_label(rank: int | None) -> str:
    if rank is None:
        return "Unranked"
    if rank == 0:
        return "Champion"
    return f"#{rank}"


def opposition_quality(
    conn: sqlite3.Connection, opponent_id: int, is_title_fight: bool
) -> tuple[float, int | None]:
    rank = best_opponent_rank(conn, opponent_id)
    quality = UNRANKED_OPPONENT_QUALITY
    if rank is not None:
        if rank <= 0:
            quality = 1.0
        else:
            quality = 1.0 - (rank / 16.0) * 0.45
    if is_title_fight:
        quality = min(1.0, quality + TITLE_FIGHT_QUALITY_BONUS)
    return quality, rank


def fight_contribution(won: bool, quality: float, finish_mult: float) -> float:
    if won:
        return quality * finish_mult
    return -LOSS_CONTRIBUTION_BASE * finish_mult


def points_for_rank(rank: int) -> int:
    if rank < 0 or rank > MAX_RANKED_SLOT:
        return 0
    return CHAMPION_POINTS - rank


def format_date(ts: int | None) -> str:
    if ts is None:
        return "?"
    return datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%d")


def bar(value: float, maximum: float, width: int = 30, fill: str = "#", empty: str = ".") -> str:
    if maximum <= 0:
        return empty * width
    ratio = max(0.0, min(1.0, value / maximum))
    filled = int(round(ratio * width))
    return fill * filled + empty * (width - filled)


@dataclass
class MomentumFightRow:
    event_date: int
    event_name: str
    opponent_name: str
    won: bool
    result_method: str
    is_title_fight: bool
    is_finish_win: bool
    recency: float
    opp_quality: float
    opp_rank: int | None
    finish_mult: float
    contribution: float
    weighted_contribution: float


@dataclass
class MomentumBreakdown:
    status: str
    score: float | None = None
    stored_score: float | None = None
    days_since_last_fight: float | None = None
    fights: list[MomentumFightRow] = field(default_factory=list)
    weighted_sum: float = 0.0
    weight_total: float = 0.0
    weighted_average: float = 0.0
    finish_rate: float = 0.0
    finish_boost: float = 1.0
    adjusted: float = 0.0


@dataclass
class ResumeWinRow:
    event_date: int
    event_name: str
    opponent_name: str
    result_method: str
    opp_rank: int | None
    points: int
    running_total: int


@dataclass
class ResumeBreakdown:
    score: float
    stored_score: float | None = None
    ranked_wins: list[ResumeWinRow] = field(default_factory=list)
    unranked_win_count: int = 0
    skipped_non_decisive: int = 0


def load_fighter(conn: sqlite3.Connection, name: str) -> dict[str, Any] | None:
    row = conn.execute(
        """
        SELECT id, name, weight_class, momentum_score, resume_score
        FROM fighters WHERE name = ? LIMIT 1
        """,
        (name,),
    ).fetchone()
    if not row:
        return None
    return {
        "id": row[0],
        "name": row[1],
        "weight_class": row[2] or "",
        "momentum_score": row[3],
        "resume_score": row[4],
    }


def build_momentum_breakdown(conn: sqlite3.Connection, fighter_id: int) -> MomentumBreakdown:
    now = int(time.time())
    last_fight_row = conn.execute(
        """
        SELECT MAX(e.event_date)
        FROM fights f
        JOIN events e ON e.id = f.event_id
        WHERE f.fighter1_id = ? OR f.fighter2_id = ?
        """,
        (fighter_id, fighter_id),
    ).fetchone()

    if last_fight_row[0] is None:
        return MomentumBreakdown(status="no_fights")

    days_since = (now - int(last_fight_row[0])) / SECONDS_PER_DAY
    if days_since > INACTIVITY_DAYS:
        return MomentumBreakdown(
            status="inactive",
            score=0.0,
            days_since_last_fight=days_since,
        )

    fight_rows = conn.execute(
        """
        SELECT e.event_date, e.name,
               CASE WHEN f.fighter1_id = ? THEN f.fighter2_id ELSE f.fighter1_id END,
               f.winner_id, f.result_method, f.is_title_fight
        FROM fights f
        JOIN events e ON e.id = f.event_id
        WHERE f.fighter1_id = ? OR f.fighter2_id = ?
        ORDER BY e.event_date DESC
        """,
        (fighter_id, fighter_id, fighter_id),
    ).fetchall()

    fights: list[MomentumFightRow] = []
    for event_date, event_name, opponent_id, winner_id, method, is_title in fight_rows:
        if len(fights) >= MAX_RECENT_FIGHTS:
            break
        if is_non_decisive(method) or winner_id is None:
            continue

        opponent = conn.execute(
            "SELECT name FROM fighters WHERE id = ?", (opponent_id,)
        ).fetchone()
        opponent_name = opponent[0] if opponent else f"id {opponent_id}"
        won = int(winner_id) == fighter_id
        finish_win = won and is_finish_method(method or "")
        finish_loss = (not won) and is_finish_method(method or "")
        finish_mult = (
            FINISH_WIN_MULTIPLIER if (finish_win or finish_loss) else 1.0
        )
        rec = recency_weight(int(event_date), now)
        quality, opp_rank = opposition_quality(conn, int(opponent_id), bool(is_title))
        contribution = fight_contribution(won, quality, finish_mult)
        weighted = contribution * rec

        fights.append(
            MomentumFightRow(
                event_date=int(event_date),
                event_name=event_name or "",
                opponent_name=opponent_name,
                won=won,
                result_method=method or "",
                is_title_fight=bool(is_title),
                is_finish_win=finish_win,
                recency=rec,
                opp_quality=quality,
                opp_rank=opp_rank,
                finish_mult=finish_mult,
                contribution=contribution,
                weighted_contribution=weighted,
            )
        )

    if len(fights) < MIN_DECISIVE_FIGHTS:
        return MomentumBreakdown(
            status="insufficient_fights",
            days_since_last_fight=days_since,
            fights=fights,
        )

    weighted_sum = sum(f.weighted_contribution for f in fights)
    weight_total = sum(f.recency for f in fights)
    weighted_average = weighted_sum / weight_total if weight_total else 0.0
    wins = sum(1 for f in fights if f.won)
    finish_wins = sum(1 for f in fights if f.is_finish_win)
    finish_rate = finish_wins / wins if wins else 0.0
    finish_boost = 1.0 + FINISH_RATE_BOOST_WEIGHT * finish_rate
    adjusted = weighted_average * finish_boost
    score = NEUTRAL_SCORE + (adjusted / MAX_FIGHT_CONTRIBUTION) * NEUTRAL_SCORE
    score = max(0.0, min(100.0, score))

    return MomentumBreakdown(
        status="ok",
        score=score,
        days_since_last_fight=days_since,
        fights=fights,
        weighted_sum=weighted_sum,
        weight_total=weight_total,
        weighted_average=weighted_average,
        finish_rate=finish_rate,
        finish_boost=finish_boost,
        adjusted=adjusted,
    )


def build_resume_breakdown(conn: sqlite3.Connection, fighter_id: int) -> ResumeBreakdown:
    win_rows = conn.execute(
        """
        SELECT e.event_date, e.name,
               CASE WHEN f.fighter1_id = ? THEN f.fighter2_id ELSE f.fighter1_id END,
               f.result_method
        FROM fights f
        JOIN events e ON e.id = f.event_id
        WHERE (f.fighter1_id = ? OR f.fighter2_id = ?)
          AND f.winner_id = ?
        ORDER BY e.event_date ASC
        """,
        (fighter_id, fighter_id, fighter_id, fighter_id),
    ).fetchall()

    ranked_wins: list[ResumeWinRow] = []
    unranked_win_count = 0
    skipped = 0
    running = 0

    for event_date, event_name, opponent_id, method in win_rows:
        if is_non_decisive(method):
            skipped += 1
            continue

        opponent = conn.execute(
            "SELECT name FROM fighters WHERE id = ?", (opponent_id,)
        ).fetchone()
        opponent_name = opponent[0] if opponent else f"id {opponent_id}"
        opp_rank = best_weight_class_rank(conn, int(opponent_id))
        pts = points_for_rank(opp_rank) if opp_rank is not None else 0

        if pts == 0:
            unranked_win_count += 1
            continue

        running += pts
        ranked_wins.append(
            ResumeWinRow(
                event_date=int(event_date),
                event_name=event_name or "",
                opponent_name=opponent_name,
                result_method=method or "",
                opp_rank=opp_rank,
                points=pts,
                running_total=running,
            )
        )

    ranked_wins.sort(key=lambda w: (w.opp_rank if w.opp_rank is not None else 999, -w.event_date))

    return ResumeBreakdown(
        score=float(running),
        ranked_wins=ranked_wins,
        unranked_win_count=unranked_win_count,
        skipped_non_decisive=skipped,
    )


def momentum_breakdown_to_structured(breakdown: MomentumBreakdown) -> dict[str, Any]:
    result: dict[str, Any] = {
        "status": breakdown.status,
        "score": round(breakdown.score, 2) if breakdown.score is not None else None,
        "days_since_last_fight": (
            round(breakdown.days_since_last_fight)
            if breakdown.days_since_last_fight is not None
            else None
        ),
        "min_decisive_fights": MIN_DECISIVE_FIGHTS,
        "inactivity_days": INACTIVITY_DAYS,
        "fights": [
            {
                "event_date": format_date(f.event_date),
                "event_name": f.event_name,
                "opponent_name": f.opponent_name,
                "result": "W" if f.won else "L",
                "result_method": f.result_method,
                "opponent_rank_label": rank_label(f.opp_rank),
                "recency": round(f.recency, 2),
                "opp_quality": round(f.opp_quality, 2),
                "finish_mult": round(f.finish_mult, 2),
                "contribution": round(f.contribution, 3),
                "weighted_contribution": round(f.weighted_contribution, 3),
            }
            for f in breakdown.fights
        ],
    }
    if breakdown.status == "ok":
        result.update(
            {
                "weighted_average": round(breakdown.weighted_average, 3),
                "finish_rate": round(breakdown.finish_rate, 2),
                "finish_boost": round(breakdown.finish_boost, 3),
                "adjusted": round(breakdown.adjusted, 3),
                "neutral_score": NEUTRAL_SCORE,
                "max_fight_contribution": MAX_FIGHT_CONTRIBUTION,
                "loss_contribution_base": LOSS_CONTRIBUTION_BASE,
            }
        )
    return result


def resume_breakdown_to_structured(breakdown: ResumeBreakdown) -> dict[str, Any]:
    return {
        "score": int(breakdown.score),
        "ranked_wins": [
            {
                "event_date": format_date(w.event_date),
                "event_name": w.event_name,
                "opponent_name": w.opponent_name,
                "result_method": w.result_method,
                "opponent_rank_label": rank_label(w.opp_rank),
                "points": w.points,
                "running_total": w.running_total,
            }
            for w in breakdown.ranked_wins
        ],
        "unranked_win_count": breakdown.unranked_win_count,
        "skipped_non_decisive": breakdown.skipped_non_decisive,
    }


def render(width: int, fighter: dict[str, Any], momentum: MomentumBreakdown, resume: ResumeBreakdown) -> str:
    lines: list[str] = []

    def rule(char: str = "-") -> None:
        lines.append(char * width)

    def heading(text: str) -> None:
        lines.append("")
        lines.append(text)
        rule()

    lines.append("FIGHTER SCORE BREAKDOWN".center(width))
    rule("=")
    lines.append(fighter["name"].center(width))
    if fighter.get("weight_class"):
        lines.append(fighter["weight_class"].center(width))
    rule("=")

    # ── Momentum ──────────────────────────────────────────────────────────────
    heading("MOMENTUM (recency-weighted recent form, 0-100)")

    if momentum.status == "no_fights":
        lines.append("No fights on record - momentum cannot be computed.")
    elif momentum.status == "inactive":
        lines.append(
            f"Score: 0.00  (inactive - last fight {momentum.days_since_last_fight:.0f} days ago, "
            f"limit {INACTIVITY_DAYS})"
        )
    elif momentum.status == "insufficient_fights":
        lines.append(
            f"Not enough decisive fights ({len(momentum.fights)}/{MIN_DECISIVE_FIGHTS} required)."
        )
        if momentum.fights:
            lines.append("")
            lines.append("Recent decisive fights found:")
            for f in momentum.fights:
                result = "W" if f.won else "L"
                lines.append(
                    f"  {format_date(f.event_date)}  {result} vs {f.opponent_name}  ({f.result_method})"
                )
    else:
        assert momentum.score is not None
        stored = fighter.get("momentum_score")
        stored_note = f"  (stored: {stored:.2f})" if stored is not None else ""
        lines.append(f"Score: {momentum.score:.2f}{stored_note}")
        lines.append(
            f"{bar(momentum.score, 100, width=40)}  {momentum.score:.1f}/100"
        )
        lines.append(
            f"Last fight: {momentum.days_since_last_fight:.0f} days ago  "
            f"(inactivity cutoff: {INACTIVITY_DAYS} days)"
        )
        lines.append("")
        lines.append("Last 5 decisive fights (most recent first):")
        rule()

        col_date, col_opp, col_res = 11, 22, 6
        lines.append(
            f"{'Date':<{col_date}}  {'Opponent':<{col_opp}}  {'Result':<{col_res}}  "
            f"{'Recency':>7}  {'Opp Q':>5}  {'Rank':>8}  {'FinM':>4}  {'Contrib':>8}  {'Weighted':>8}"
        )
        rule()

        for f in momentum.fights:
            result = "Win" if f.won else "Loss"
            title = " *" if f.is_title_fight else ""
            finish = " F" if f.is_finish_win else ""
            lines.append(
                f"{format_date(f.event_date):<{col_date}}  "
                f"{(f.opponent_name + title)[:col_opp]:<{col_opp}}  "
                f"{(result + finish)[:col_res]:<{col_res}}  "
                f"{f.recency:>7.2f}  {f.opp_quality:>5.2f}  {rank_label(f.opp_rank):>8}  "
                f"{f.finish_mult:>4.2f}  {f.contribution:>+8.3f}  {f.weighted_contribution:>+8.3f}"
            )

        lines.append("")
        lines.append("Calculation:")
        lines.append(
            f"  weighted avg = sum(contribution x recency) / sum(recency)"
            f" = {momentum.weighted_sum:+.3f} / {momentum.weight_total:.3f}"
            f" = {momentum.weighted_average:+.3f}"
        )
        lines.append(
            f"  finish rate  = {momentum.finish_rate:.0%} wins by finish"
            f"  ->  boost = 1 + {FINISH_RATE_BOOST_WEIGHT} x {momentum.finish_rate:.2f}"
            f" = {momentum.finish_boost:.3f}"
        )
        lines.append(
            f"  adjusted     = {momentum.weighted_average:+.3f} x {momentum.finish_boost:.3f}"
            f" = {momentum.adjusted:+.3f}"
        )
        lines.append(
            f"  momentum     = {NEUTRAL_SCORE} + ({momentum.adjusted:+.3f} / {MAX_FIGHT_CONTRIBUTION})"
            f" x {NEUTRAL_SCORE} = {momentum.score:.2f}"
        )
        lines.append("")
        lines.append(
            "  Opp Q: opponent quality (ranked higher = better). "
            "Wins scale with quality; losses use a fixed penalty regardless of opponent. "
            "Recency: 1.0 for first year, then decays. Fin x: 1.30 on finish wins and losses."
        )

    # ── Resume ────────────────────────────────────────────────────────────────
    heading("RESUME (career wins over currently ranked opponents)")

    stored = fighter.get("resume_score")
    stored_note = f"  (stored: {int(stored)})" if stored is not None else ""
    lines.append(f"Score: {int(resume.score)}{stored_note}")

    resume_max = max(100, int(resume.score), max((w.running_total for w in resume.ranked_wins), default=0))
    lines.append(f"{bar(resume.score, resume_max, width=40)}  {int(resume.score)} pts")
    lines.append("")
    lines.append(
        "Points per win: Champion=16, #1=15, ..., #15=1. "
        "Uses opponent's best current weight-class rank (P4P ignored). "
        "Repeat wins count each time; unranked wins = 0."
    )

    if resume.ranked_wins:
        lines.append("")
        lines.append("Ranked wins (best rank first):")
        rule()
        col_date, col_opp, col_pts = 11, 26, 5
        lines.append(
            f"{'Date':<{col_date}}  {'Opponent':<{col_opp}}  {'Rank':>8}  {'Pts':>{col_pts}}  {'Total':>6}"
        )
        rule()
        running = 0
        for w in resume.ranked_wins:
            running += w.points
            lines.append(
                f"{format_date(w.event_date):<{col_date}}  "
                f"{w.opponent_name[:col_opp]:<{col_opp}}  "
                f"{rank_label(w.opp_rank):>8}  {w.points:>{col_pts}}  {running:>6}"
            )
    else:
        lines.append("")
        lines.append("No wins over currently ranked opponents.")

    extras: list[str] = []
    if resume.unranked_win_count:
        extras.append(f"{resume.unranked_win_count} win(s) over unranked opponents (0 pts each)")
    if resume.skipped_non_decisive:
        extras.append(f"{resume.skipped_non_decisive} non-decisive result(s) excluded")
    if extras:
        lines.append("")
        lines.append("Also: " + "; ".join(extras))

    lines.append("")
    return "\n".join(lines)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Show a visual breakdown of a fighter's momentum and resume scores."
    )
    parser.add_argument(
        "fighter",
        nargs="?",
        help="Fighter name (exact match). Prompts interactively if omitted.",
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
        help="Path to ufc_db shared library (for score verification)",
    )
    parser.add_argument(
        "--width",
        type=int,
        default=96,
        help="Output width in characters (default: 96)",
    )
    return parser.parse_args(argv)


def _prompt_fighter(conn: sqlite3.Connection) -> str:
    while True:
        name = input("Fighter name: ").strip()
        if not name:
            print("Name cannot be empty.", file=sys.stderr)
            continue
        if load_fighter(conn, name):
            return name
        print(f"Fighter not found: {name}", file=sys.stderr)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    if not args.db.is_file():
        print(f"Database not found: {args.db}", file=sys.stderr)
        return 1

    conn = sqlite3.connect(args.db)
    try:
        name = args.fighter or _prompt_fighter(conn)
        fighter = load_fighter(conn, name)
        if not fighter:
            print(f"Fighter not found: {name}", file=sys.stderr)
            return 1

        momentum = build_momentum_breakdown(conn, fighter["id"])
        momentum.stored_score = fighter.get("momentum_score")

        resume = build_resume_breakdown(conn, fighter["id"])
        resume.stored_score = fighter.get("resume_score")

        # Cross-check against C++ engine when available.
        try:
            with UfcDb(args.db, lib_path=args.lib) as store:
                cpp_momentum = store.compute_momentum_by_fighter_id(fighter["id"])
                cpp_resume = store.compute_resume_by_fighter_id(fighter["id"])
            if momentum.score is not None and cpp_momentum is not None:
                if abs(momentum.score - cpp_momentum) > 0.05:
                    print(
                        f"Warning: momentum mismatch (display {momentum.score:.2f} vs engine {cpp_momentum:.2f})",
                        file=sys.stderr,
                    )
            elif momentum.score is None and cpp_momentum is None:
                pass
            elif momentum.status == "inactive" and cpp_momentum == 0.0:
                pass
            else:
                print(
                    f"Warning: momentum status mismatch (display={momentum.status}, engine={cpp_momentum})",
                    file=sys.stderr,
                )
            if abs(resume.score - cpp_resume) > 0.5:
                print(
                    f"Warning: resume mismatch (display {resume.score:.0f} vs engine {cpp_resume:.0f})",
                    file=sys.stderr,
                )
        except FileNotFoundError:
            pass

        print(render(args.width, fighter, momentum, resume))
    finally:
        conn.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
