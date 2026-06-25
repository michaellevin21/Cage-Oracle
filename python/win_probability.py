"""Combine matchup signals into a weighted win probability estimate."""

from __future__ import annotations

import math
import sqlite3
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from archetype_matchup_history import (
    DEFAULT_MIN_SAMPLE,
    ArchetypeMatchupIndex,
    fighter_archetype,
    resolve_matchup_weight_classes,
)

# Component blend weights (when a component is active at full strength)
WEIGHT_CAREER = 0.28
WEIGHT_PHYSICAL = 0.10
WEIGHT_STYLE = 0.20
WEIGHT_RESUME = 0.18
WEIGHT_MOMENTUM = 0.18
WEIGHT_SIMILAR = 0.14
WEIGHT_H2H_ONE_MEETING = 0.17
WEIGHT_H2H_MULTI_MEETING = 0.25

# Head-to-head recency decay (matches momentum scoring in FighterMomentum.cpp).
H2H_SECONDS_PER_DAY = 86400
H2H_RECENCY_FULL_WEIGHT_DAYS = 365
H2H_RECENCY_HALF_LIFE_DAYS = 365.0
H2H_MIN_RECENCY_WEIGHT = 0.12

# Full-strength similar component when avg cosine similarity reaches this level.
SIMILAR_CONFIDENCE_REFERENCE = 0.75
# Minimum cosine similarity for comparable historical fights (see SimilaritySearch.cpp).
SIMILAR_MIN_SIMILARITY = 0.50

PROFILE_METRICS = frozenset(
    {
        "height_cm",
        "reach_cm",
        "momentum_score",
        "resume_score",
        "stance",
        "weight_class",
        "archetype",
        "age",
    }
)

PHYSICAL_MEASURES = {
    "height_cm": {"weight": 2.5, "scale": 10.0},
    "reach_cm": {"weight": 3.5, "scale": 10.0},
}

CAREER_METRICS: dict[str, dict[str, float | bool]] = {
    "sig_strikes_landed_per_round": {"higher_is_better": True, "weight": 1.0},
    "sig_strike_accuracy": {"higher_is_better": True, "weight": 1.1},
    "striking_defense": {"higher_is_better": True, "weight": 1.1},
    "strikes_taken_per_round": {"higher_is_better": False, "weight": 0.9},
    "total_strike_accuracy": {"higher_is_better": True, "weight": 0.8},
    "takedown_accuracy": {"higher_is_better": True, "weight": 1.0},
    "takedown_defense": {"higher_is_better": True, "weight": 1.0},
    "takedowns_landed_per_round": {"higher_is_better": True, "weight": 1.0},
    "sub_attempts_per_round": {"higher_is_better": True, "weight": 0.9},
    "reversals_per_round": {"higher_is_better": True, "weight": 0.7},
    "knockdowns_per_round": {"higher_is_better": True, "weight": 1.0},
    "control_time_seconds_per_round": {"higher_is_better": True, "weight": 1.0},
    "head_strikes_landed_per_round": {"higher_is_better": True, "weight": 0.6},
    "body_strikes_landed_per_round": {"higher_is_better": True, "weight": 0.5},
    "leg_strikes_landed_per_round": {"higher_is_better": True, "weight": 0.5},
    "distance_strikes_landed_per_round": {"higher_is_better": True, "weight": 0.6},
    "clinch_strikes_landed_per_round": {"higher_is_better": True, "weight": 0.6},
    "ground_strikes_landed_per_round": {"higher_is_better": True, "weight": 0.7},
}

H2H_STAT_METRICS: dict[str, bool] = {
    "sig_strikes_landed": True,
    "total_strikes_landed": True,
    "takedowns_landed": True,
    "sub_attempts": True,
    "knockdowns": True,
    "control_time_seconds": True,
}

_FIGHT_COUNT_SQL = (
    "SELECT COUNT(*) FROM fights WHERE fighter1_id = ? OR fighter2_id = ?"
)


def fighter_fight_count(
    db: sqlite3.Connection | str | Path,
    fighter_id: int,
) -> int:
    """Number of fights in the database for a fighter (as fighter1 or fighter2)."""
    if isinstance(db, sqlite3.Connection):
        row = db.execute(_FIGHT_COUNT_SQL, (fighter_id, fighter_id)).fetchone()
        return int(row[0]) if row else 0

    conn = sqlite3.connect(str(db))
    try:
        row = conn.execute(_FIGHT_COUNT_SQL, (fighter_id, fighter_id)).fetchone()
        return int(row[0]) if row else 0
    finally:
        conn.close()


def both_fighters_have_fight_history(
    db: sqlite3.Connection | str | Path,
    fighter_a_id: int,
    fighter_b_id: int,
) -> bool:
    """True when each fighter appears in at least one fight row."""
    return (
        fighter_fight_count(db, fighter_a_id) >= 1
        and fighter_fight_count(db, fighter_b_id) >= 1
    )


@dataclass
class ComponentEstimate:
    name: str
    label: str
    p_fighter_a: float
    blend_weight: float
    available: bool
    detail: str = ""


@dataclass
class WinProbabilityEstimate:
    p_fighter_a: float
    p_fighter_b: float
    components: list[ComponentEstimate] = field(default_factory=list)


def _sigmoid(x: float) -> float:
    if x >= 0:
        z = math.exp(-x)
        return 1.0 / (1.0 + z)
    z = math.exp(x)
    return z / (1.0 + z)


def _clamp_prob(p: float) -> float:
    return min(0.97, max(0.03, p))


def _relative_edge(a: float, b: float, *, higher_is_better: bool) -> float:
    scale = max(abs(a), abs(b), 1e-9)
    delta = (a - b) / scale
    return delta if higher_is_better else -delta


def _comparison_metric(
    matchup: dict[str, Any], metric: str
) -> tuple[float, float] | None:
    for row in matchup.get("comparisons", []):
        if row.get("metric") != metric:
            continue
        a = row.get("fighter_a")
        b = row.get("fighter_b")
        if a is not None and b is not None:
            return float(a), float(b)

    fighter_a = matchup["fighter_a"]
    fighter_b = matchup["fighter_b"]
    val_a = fighter_a.get(metric)
    val_b = fighter_b.get(metric)
    if val_a is not None and val_b is not None:
        return float(val_a), float(val_b)
    return None


def _physical_component(matchup: dict[str, Any]) -> ComponentEstimate:
    score = 0.0
    total_weight = 0.0
    details: list[str] = []

    for metric, spec in PHYSICAL_MEASURES.items():
        values = _comparison_metric(matchup, metric)
        if values is None:
            continue
        a, b = values
        edge = (a - b) / float(spec["scale"])
        weight = float(spec["weight"])
        score += weight * edge
        total_weight += weight
        label = "height" if metric == "height_cm" else "reach"
        details.append(f"{label} {a:.0f} vs {b:.0f} cm")

    if total_weight <= 0:
        return ComponentEstimate(
            name="physical",
            label="Height & reach",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="Height/reach unavailable",
        )

    normalized = score / total_weight
    p_a = _clamp_prob(_sigmoid(3.0 * normalized))
    return ComponentEstimate(
        name="physical",
        label="Height & reach",
        p_fighter_a=p_a,
        blend_weight=WEIGHT_PHYSICAL,
        available=True,
        detail="; ".join(details),
    )


def _career_component(matchup: dict[str, Any]) -> ComponentEstimate:
    score = 0.0
    total_weight = 0.0
    edges = 0

    for row in matchup.get("comparisons", []):
        metric = row.get("metric")
        if metric in PROFILE_METRICS or metric not in CAREER_METRICS:
            continue
        a = row.get("fighter_a")
        b = row.get("fighter_b")
        if a is None or b is None:
            continue

        spec = CAREER_METRICS[metric]
        edge = _relative_edge(float(a), float(b), higher_is_better=bool(spec["higher_is_better"]))
        weight = float(spec["weight"])
        score += weight * edge
        total_weight += weight
        edges += 1

    if total_weight <= 0 or edges == 0:
        return ComponentEstimate(
            name="career",
            label="Career stats",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="Insufficient career comparison data",
        )

    normalized = score / total_weight
    p_a = _clamp_prob(_sigmoid(3.0 * normalized))
    return ComponentEstimate(
        name="career",
        label="Career stats",
        p_fighter_a=p_a,
        blend_weight=WEIGHT_CAREER,
        available=True,
        detail=f"{edges} metrics compared",
    )


def _style_component(
    matchup: dict[str, Any],
    index: ArchetypeMatchupIndex | None,
    *,
    min_sample: int = DEFAULT_MIN_SAMPLE,
) -> ComponentEstimate:
    fighter_a = matchup["fighter_a"]
    fighter_b = matchup["fighter_b"]
    arch_a = fighter_archetype(fighter_a)
    arch_b = fighter_archetype(fighter_b)

    if not arch_a or not arch_b or index is None:
        return ComponentEstimate(
            name="style",
            label="Style clash",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="Archetype data unavailable",
        )

    if arch_a == arch_b:
        return ComponentEstimate(
            name="style",
            label="Style clash",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail=f"Both are {arch_a}; no style edge",
        )

    weight_classes = resolve_matchup_weight_classes(
        fighter_a, fighter_b, index.ranked_weight_classes
    )
    if not weight_classes:
        return ComponentEstimate(
            name="style",
            label="Style clash",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="No ranked division for style history",
        )

    best: tuple[float, int, str] | None = None
    for wc in weight_classes:
        rate = index.win_rate(arch_a, arch_b, wc)
        if not rate or rate.decisive < min_sample:
            continue
        if best is None or rate.decisive > best[1]:
            best = (rate.win_pct / 100.0, rate.decisive, wc)

    if best is None:
        return ComponentEstimate(
            name="style",
            label="Style clash",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail=f"Insufficient style history (need {min_sample}+ fights)",
        )

    p_a, sample, wc = best
    weight_scale = min(sample / 20.0, 1.0)
    return ComponentEstimate(
        name="style",
        label="Style clash",
        p_fighter_a=_clamp_prob(p_a),
        blend_weight=WEIGHT_STYLE * weight_scale,
        available=True,
        detail=f"{arch_a} vs {arch_b} at {wc} ({sample} fights)",
    )


def _momentum_component(matchup: dict[str, Any]) -> ComponentEstimate:
    fighter_a = matchup["fighter_a"]
    fighter_b = matchup["fighter_b"]
    mom_a = fighter_a.get("momentum_score")
    mom_b = fighter_b.get("momentum_score")

    if mom_a is None or mom_b is None:
        for row in matchup.get("comparisons", []):
            if row.get("metric") != "momentum_score":
                continue
            if mom_a is None:
                mom_a = row.get("fighter_a")
            if mom_b is None:
                mom_b = row.get("fighter_b")

    if mom_a is None or mom_b is None:
        return ComponentEstimate(
            name="momentum",
            label="Momentum",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="Momentum scores unavailable",
        )

    mom_a = float(mom_a)
    mom_b = float(mom_b)
    diff = (mom_a - mom_b) / 20.0
    p_a = _clamp_prob(_sigmoid(diff))
    return ComponentEstimate(
        name="momentum",
        label="Momentum",
        p_fighter_a=p_a,
        blend_weight=WEIGHT_MOMENTUM,
        available=True,
        detail=f"{mom_a:.1f} vs {mom_b:.1f}",
    )


def _resume_component(matchup: dict[str, Any]) -> ComponentEstimate:
    fighter_a = matchup["fighter_a"]
    fighter_b = matchup["fighter_b"]
    res_a = fighter_a.get("resume_score")
    res_b = fighter_b.get("resume_score")

    if res_a is None or res_b is None:
        for row in matchup.get("comparisons", []):
            if row.get("metric") != "resume_score":
                continue
            if res_a is None:
                res_a = row.get("fighter_a")
            if res_b is None:
                res_b = row.get("fighter_b")

    if res_a is None or res_b is None:
        return ComponentEstimate(
            name="resume",
            label="Resume",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="Resume scores unavailable",
        )

    res_a = float(res_a)
    res_b = float(res_b)
    if res_a <= 0 and res_b <= 0:
        return ComponentEstimate(
            name="resume",
            label="Resume",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="Neither fighter has ranked wins on resume",
        )

    diff = (res_a - res_b) / 25.0
    p_a = _clamp_prob(_sigmoid(diff))
    return ComponentEstimate(
        name="resume",
        label="Resume",
        p_fighter_a=p_a,
        blend_weight=WEIGHT_RESUME,
        available=True,
        detail=f"{res_a:.0f} vs {res_b:.0f} pts",
    )


def _winner_is_fighter_a(
    hit: dict[str, Any], fighter_a_id: int, fighter_b_id: int
) -> bool | None:
    winner_id = hit.get("winner_id")
    if winner_id is None:
        return None
    winner_id = int(winner_id)
    if winner_id <= 0:
        return None
    if winner_id == fighter_a_id:
        return True
    if winner_id == fighter_b_id:
        return False
    return None


def _similar_hit_favors_fighter_a(
    hit: dict[str, Any], fighter_a_id: int, fighter_b_id: int
) -> bool | None:
    """Whether a comparable fight's outcome favors fighter A in the query matchup."""
    f1 = int(hit.get("fighter1_id") or 0)
    f2 = int(hit.get("fighter2_id") or 0)
    winner_id = hit.get("winner_id")
    if winner_id is None:
        return None
    winner_id = int(winner_id)
    if winner_id <= 0:
        return None

    a_in = fighter_a_id in (f1, f2)
    b_in = fighter_b_id in (f1, f2)
    if a_in and b_in:
        return _winner_is_fighter_a(hit, fighter_a_id, fighter_b_id)
    if a_in:
        return winner_id == fighter_a_id
    if b_in:
        return winner_id != fighter_b_id
    return None


def _similar_confidence(avg_similarity: float) -> float:
    """How much to trust similar-matchup outcomes (0 = ignore, 1 = full strength)."""
    if SIMILAR_CONFIDENCE_REFERENCE <= 0:
        return 1.0
    return min(1.0, max(0.0, avg_similarity / SIMILAR_CONFIDENCE_REFERENCE))


def _similar_matchups_component(
    similar: dict[str, Any] | None,
) -> ComponentEstimate:
    if not similar:
        return ComponentEstimate(
            name="similar",
            label="Similar matchups",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="No similar matchup data",
        )

    fighter_a_id = int(similar["fighter_a_id"])
    fighter_b_id = int(similar["fighter_b_id"])
    hits = similar.get("similar_matchups") or []
    weighted_a = 0.0
    weighted_total = 0.0
    used = 0

    for hit in hits:
        sim = hit.get("similarity")
        if sim is None or float(sim) <= 0:
            continue
        sim = float(sim)
        favor_a = _similar_hit_favors_fighter_a(hit, fighter_a_id, fighter_b_id)
        if favor_a is None:
            continue
        weighted_a += sim if favor_a else 0.0
        weighted_total += sim
        used += 1

    if weighted_total <= 0 or used == 0:
        return ComponentEstimate(
            name="similar",
            label="Similar matchups",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="No decisive similar matchups",
        )

    raw_p = weighted_a / weighted_total
    avg_sim = weighted_total / used
    confidence = _similar_confidence(avg_sim)
    p_a = 0.5 + (raw_p - 0.5) * confidence
    return ComponentEstimate(
        name="similar",
        label="Similar matchups",
        p_fighter_a=_clamp_prob(p_a),
        blend_weight=WEIGHT_SIMILAR * confidence,
        available=True,
        detail=(
            f"{used} comparable fights (>= {SIMILAR_MIN_SIMILARITY:.0%} sim); "
            f"avg {avg_sim:.0%} (confidence {confidence:.0%})"
        ),
    )


def _aggregate_round_stats(rows: list[dict[str, Any]], fighter_id: int) -> dict[str, float]:
    totals = {key: 0.0 for key in H2H_STAT_METRICS}
    rounds = 0
    for row in rows:
        if int(row.get("fighter_id") or 0) != fighter_id:
            continue
        rounds += 1
        for key in H2H_STAT_METRICS:
            totals[key] += float(row.get(key) or 0)
    if rounds == 0:
        return {}
    return {key: totals[key] / rounds for key, _ in H2H_STAT_METRICS.items()}


def _h2h_stats_edge(
    stats_a: dict[str, float], stats_b: dict[str, float]
) -> float | None:
    score = 0.0
    weight = 0.0
    for metric, higher_is_better in H2H_STAT_METRICS.items():
        a = stats_a.get(metric)
        b = stats_b.get(metric)
        if a is None or b is None:
            continue
        edge = _relative_edge(a, b, higher_is_better=higher_is_better)
        score += edge
        weight += 1.0
    if weight <= 0:
        return None
    return score / weight


def _h2h_recency_weight(event_date: int, now: int) -> float:
    """Decay older prior meetings; full weight for the first year after the bout."""
    days_since = (now - event_date) / H2H_SECONDS_PER_DAY
    if days_since <= H2H_RECENCY_FULL_WEIGHT_DAYS:
        return 1.0
    days_past_grace = days_since - H2H_RECENCY_FULL_WEIGHT_DAYS
    weight = 0.5 ** (days_past_grace / H2H_RECENCY_HALF_LIFE_DAYS)
    return max(H2H_MIN_RECENCY_WEIGHT, weight)


def _h2h_meeting_recency(hit: dict[str, Any], now: int) -> float:
    event_date = hit.get("event_date")
    if event_date is None:
        return 1.0
    return _h2h_recency_weight(int(event_date), now)


def _h2h_blend_weight(*, decisive: int, stat_fights: int) -> float:
    """Scale H2H influence by how much prior meeting data exists."""
    evidence = decisive if decisive > 0 else stat_fights
    if evidence <= 0:
        return 0.0
    if evidence == 1:
        return WEIGHT_H2H_ONE_MEETING
    return WEIGHT_H2H_MULTI_MEETING


def _h2h_component(
    similar: dict[str, Any] | None,
    db: Any | None,
) -> ComponentEstimate:
    if not similar:
        return ComponentEstimate(
            name="h2h",
            label="Head-to-head",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="No prior meetings",
        )

    fighter_a_id = int(similar["fighter_a_id"])
    fighter_b_id = int(similar["fighter_b_id"])
    meetings = similar.get("prior_meetings") or []
    now = int(time.time())

    wins_a = 0
    wins_b = 0
    weighted_wins_a = 0.0
    record_weight = 0.0
    for hit in meetings:
        recency = _h2h_meeting_recency(hit, now)
        outcome = _winner_is_fighter_a(hit, fighter_a_id, fighter_b_id)
        if outcome is True:
            wins_a += 1
            weighted_wins_a += recency
            record_weight += recency
        elif outcome is False:
            wins_b += 1
            record_weight += recency

    record_p: float | None = None
    decisive = wins_a + wins_b
    if record_weight > 0:
        record_p = weighted_wins_a / record_weight

    stats_a_totals: dict[str, float] = {k: 0.0 for k in H2H_STAT_METRICS}
    stats_b_totals: dict[str, float] = {k: 0.0 for k in H2H_STAT_METRICS}
    stats_weight = 0.0
    stat_fights = 0

    if db is not None and meetings:
        for hit in meetings:
            fight_id = hit.get("fight_id")
            if fight_id is None:
                continue
            recency = _h2h_meeting_recency(hit, now)
            rows = db.list_round_stats_for_fight(int(fight_id))
            per_a = _aggregate_round_stats(rows, fighter_a_id)
            per_b = _aggregate_round_stats(rows, fighter_b_id)
            if not per_a or not per_b:
                continue
            for key in H2H_STAT_METRICS:
                stats_a_totals[key] += per_a.get(key, 0.0) * recency
                stats_b_totals[key] += per_b.get(key, 0.0) * recency
            stats_weight += recency
            stat_fights += 1

    stats_p: float | None = None
    if stats_weight > 0:
        avg_a = {k: stats_a_totals[k] / stats_weight for k in H2H_STAT_METRICS}
        avg_b = {k: stats_b_totals[k] / stats_weight for k in H2H_STAT_METRICS}
        edge = _h2h_stats_edge(avg_a, avg_b)
        if edge is not None:
            stats_p = _clamp_prob(_sigmoid(2.5 * edge))

    if record_p is None and stats_p is None:
        return ComponentEstimate(
            name="h2h",
            label="Head-to-head",
            p_fighter_a=0.5,
            blend_weight=0.0,
            available=False,
            detail="No prior meetings",
        )

    recency_scale = (
        sum(_h2h_meeting_recency(hit, now) for hit in meetings) / len(meetings)
        if meetings
        else 1.0
    )

    if record_p is not None and stats_p is not None:
        p_a = 0.6 * record_p + 0.4 * stats_p
        detail = (
            f"{wins_a}-{wins_b} record (recency-weighted); "
            f"stats from {stat_fights} fight(s)"
        )
    elif record_p is not None:
        p_a = record_p
        detail = f"{wins_a}-{wins_b} record across {decisive} meeting(s) (recency-weighted)"
    else:
        p_a = stats_p or 0.5
        detail = f"Stat edge from {stat_fights} prior meeting(s) (recency-weighted)"

    blend_weight = _h2h_blend_weight(decisive=decisive, stat_fights=stat_fights) * recency_scale
    return ComponentEstimate(
        name="h2h",
        label="Head-to-head",
        p_fighter_a=_clamp_prob(p_a),
        blend_weight=blend_weight,
        available=True,
        detail=detail,
    )


def _blend_components(components: list[ComponentEstimate]) -> float:
    active = [c for c in components if c.available and c.blend_weight > 0]
    if not active:
        return 0.5

    total_weight = sum(c.blend_weight for c in active)
    p_a = sum(c.p_fighter_a * c.blend_weight for c in active) / total_weight
    return _clamp_prob(p_a)


def estimate_win_probability(
    matchup: dict[str, Any],
    similar_matchups: dict[str, Any] | None = None,
    *,
    archetype_index: ArchetypeMatchupIndex | None = None,
    db: Any | None = None,
    min_style_sample: int = DEFAULT_MIN_SAMPLE,
) -> WinProbabilityEstimate:
    """Estimate fighter A win probability from all available signals."""
    components = [
        _career_component(matchup),
        _physical_component(matchup),
        _style_component(matchup, archetype_index, min_sample=min_style_sample),
        _resume_component(matchup),
        _momentum_component(matchup),
        _similar_matchups_component(similar_matchups),
        _h2h_component(similar_matchups, db),
    ]
    p_a = _blend_components(components)
    return WinProbabilityEstimate(
        p_fighter_a=p_a,
        p_fighter_b=1.0 - p_a,
        components=components,
    )


def attach_win_probability(
    matchup: dict[str, Any],
    similar_matchups: dict[str, Any] | None = None,
    *,
    archetype_index: ArchetypeMatchupIndex | None = None,
    db: Any | None = None,
    min_style_sample: int = DEFAULT_MIN_SAMPLE,
) -> dict[str, Any]:
    if db is not None:
        fighter_a_id = int(matchup["fighter_a"]["id"])
        fighter_b_id = int(matchup["fighter_b"]["id"])
        db_path = getattr(db, "db_path", None)
        if db_path is not None and not both_fighters_have_fight_history(
            db_path, fighter_a_id, fighter_b_id
        ):
            matchup.pop("win_probability", None)
            return matchup

    estimate = estimate_win_probability(
        matchup,
        similar_matchups,
        archetype_index=archetype_index,
        db=db,
        min_style_sample=min_style_sample,
    )
    matchup["win_probability"] = {
        "p_fighter_a": estimate.p_fighter_a,
        "p_fighter_b": estimate.p_fighter_b,
        "components": [
            {
                "name": c.name,
                "label": c.label,
                "p_fighter_a": c.p_fighter_a,
                "blend_weight": c.blend_weight,
                "available": c.available,
                "detail": c.detail,
            }
            for c in estimate.components
        ],
    }
    return matchup
