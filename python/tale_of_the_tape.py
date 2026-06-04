"""Format matchup JSON from the C++ engine as a console tale-of-the-tape."""

from __future__ import annotations

import sys
from datetime import datetime, timezone
from typing import Any

PROFILE_METRICS = (
    "age",
    "height_cm",
    "reach_cm",
    "momentum_score",
    "stance",
    "weight_class",
    "archetype",
)

METRIC_LABELS: dict[str, str] = {
    "age": "Age",
    "height_cm": "Height (cm)",
    "reach_cm": "Reach (cm)",
    "momentum_score": "Momentum",
    "stance": "Stance",
    "weight_class": "Weight class",
    "archetype": "Archetype",
    "career_rounds": "Career rounds tracked",
    "sig_strikes_landed_per_round": "Sig. strikes landed / round",
    "sig_strike_accuracy": "Sig. strike accuracy",
    "striking_defense": "Striking defense",
    "strikes_taken_per_round": "Strikes taken / round",
    "total_strike_accuracy": "Total strike accuracy",
    "takedown_accuracy": "Takedown accuracy",
    "takedown_defense": "Takedown defense",
    "takedowns_landed_per_round": "Takedowns landed / round",
    "sub_attempts_per_round": "Submission attempts / round",
    "reversals_per_round": "Reversals / round",
    "knockdowns_per_round": "Knockdowns / round",
    "control_time_seconds_per_round": "Control time / round",
    "head_strikes_landed_per_round": "Head strikes landed / round",
    "body_strikes_landed_per_round": "Body strikes landed / round",
    "leg_strikes_landed_per_round": "Leg strikes landed / round",
    "distance_strikes_landed_per_round": "Distance strikes landed / round",
    "clinch_strikes_landed_per_round": "Clinch strikes landed / round",
    "ground_strikes_landed_per_round": "Ground strikes landed / round",
}


def _label(metric: str) -> str:
    return METRIC_LABELS.get(metric, metric.replace("_", " ").title())


def _age_from_dob(date_of_birth: Any) -> int | None:
    if date_of_birth is None:
        return None
    born = datetime.fromtimestamp(int(date_of_birth), tz=timezone.utc)
    today = datetime.now(timezone.utc)
    age = today.year - born.year
    if (today.month, today.day) < (born.month, born.day):
        age -= 1
    return age


def _age_comparison_row(fighter_a: dict[str, Any], fighter_b: dict[str, Any]) -> dict[str, Any]:
    age_a = _age_from_dob(fighter_a.get("date_of_birth"))
    age_b = _age_from_dob(fighter_b.get("date_of_birth"))
    row: dict[str, Any] = {
        "metric": "age",
        "fighter_a": age_a,
        "fighter_b": age_b,
    }
    if age_a is not None and age_b is not None:
        row["delta"] = age_a - age_b
        if age_a == age_b:
            row["advantage"] = "tie"
        elif age_a < age_b:
            row["advantage"] = "fighter_a"
        else:
            row["advantage"] = "fighter_b"
    return row


def _format_value(metric: str, row: dict[str, Any], side: str) -> str:
    label_key = f"{side}_label"
    if row.get(label_key) is not None:
        return str(row[label_key])

    value = row.get(side)
    if value is None:
        return "-"

    if metric.endswith("_accuracy") or metric.endswith("_defense"):
        return f"{value * 100:.1f}%"

    if metric == "control_time_seconds_per_round":
        total = float(value)
        minutes = int(total // 60)
        seconds = int(round(total % 60))
        return f"{minutes}:{seconds:02d}"

    if metric == "age":
        return str(int(value))

    if metric in ("height_cm", "reach_cm", "career_rounds"):
        return str(int(round(float(value))))

    if metric == "momentum_score":
        return f"{float(value):.2f}"

    if isinstance(value, float):
        return f"{value:.2f}"

    return str(value)


def _edge_marker(advantage: str | None) -> str:
    if advantage == "fighter_a":
        return ">"
    if advantage == "fighter_b":
        return "<"
    if advantage == "tie":
        return "="
    return " "


def _comparisons_by_metric(matchup: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {row["metric"]: row for row in matchup.get("comparisons", [])}


def render_matchup(matchup: dict[str, Any], *, width: int = 78) -> str:
    fighter_a = matchup["fighter_a"]
    fighter_b = matchup["fighter_b"]
    name_a = fighter_a["name"]
    name_b = fighter_b["name"]
    by_metric = _comparisons_by_metric(matchup)
    age_row = _age_comparison_row(fighter_a, fighter_b)
    if age_row["fighter_a"] is not None or age_row["fighter_b"] is not None:
        by_metric["age"] = age_row

    col_name = 30
    col_val = 16
    col_gap = 2
    lines: list[str] = []

    def rule(char: str = "-") -> None:
        lines.append(char * width)

    def header_block() -> None:
        lines.append("TALE OF THE TAPE".center(width))
        rule("=")
        lines.append(f"{name_a}  vs  {name_b}".center(width))
        wc_a = fighter_a.get("weight_class") or ""
        wc_b = fighter_b.get("weight_class") or ""
        if wc_a or wc_b:
            weight_line = wc_a if wc_a == wc_b and wc_a else f"{wc_a or '?'} / {wc_b or '?'}"
            lines.append(weight_line.center(width))
        rule("=")
        lines.append("")

    def section(title: str, metrics: tuple[str, ...]) -> None:
        rows = [by_metric[m] for m in metrics if m in by_metric]
        if not rows:
            return

        lines.append(title)
        rule()
        short_a = name_a if len(name_a) <= col_val else name_a[: col_val - 1] + "…"
        short_b = name_b if len(name_b) <= col_val else name_b[: col_val - 1] + "…"
        gap = " " * col_gap
        lines.append(
            f"{'Metric':<{col_name}}{gap}"
            f"{short_a:>{col_val}}{gap}"
            f"{short_b:>{col_val}}{gap}"
            f"{'Edge':>4}"
        )
        rule()

        for row in rows:
            metric = row["metric"]
            val_a = _format_value(metric, row, "fighter_a")
            val_b = _format_value(metric, row, "fighter_b")
            edge = _edge_marker(row.get("advantage"))
            lines.append(
                f"{_label(metric):<{col_name}}{gap}"
                f"{val_a:>{col_val}}{gap}"
                f"{val_b:>{col_val}}{gap}"
                f"{edge:>4}"
            )

        lines.append("")

    header_block()
    section("PROFILE", PROFILE_METRICS)

    career_metrics = tuple(
        m for m in by_metric if m not in PROFILE_METRICS
    )
    section("CAREER (per-round & accuracy)", career_metrics)

    history = matchup.get("archetype_history")
    if history:
        summaries = history.get("summaries")
        if not summaries and history.get("summary"):
            summaries = [history["summary"]]
        if summaries:
            lines.append("HISTORICAL STYLE MATCHUP")
            rule()
            for line in summaries:
                lines.append(line)
            lines.append("")

    lines.append("Edge: > favors left fighter, < favors right, = even".center(width))
    return "\n".join(lines)


def _bold(text: str) -> str:
    if sys.stdout.isatty():
        return f"\033[1m{text}\033[0m"
    return f"**{text}**"


def _matchup_fighter_label(name: str, fighter_id: int | None, hit: dict[str, Any]) -> str:
    winner_id = hit.get("winner_id")
    if winner_id and fighter_id is not None and int(winner_id) == int(fighter_id):
        return _bold(name)
    return name


def _format_matchup_line(hit: dict[str, Any], *, rank: int, show_similarity: bool) -> str:
    f1 = _matchup_fighter_label(
        hit.get("fighter1_name") or f"id {hit.get('fighter1_id')}",
        hit.get("fighter1_id"),
        hit,
    )
    f2 = _matchup_fighter_label(
        hit.get("fighter2_name") or f"id {hit.get('fighter2_id')}",
        hit.get("fighter2_id"),
        hit,
    )
    event = hit.get("event_name") or ""
    line = f"{rank}. {f1} vs {f2}"
    if event:
        line += f" @ {event}"
    if show_similarity:
        sim = hit.get("similarity")
        sim_text = f"{100.0 * sim:.1f}%" if isinstance(sim, (int, float)) else "?"
        line += f"  ({sim_text} similarity)"
    return line


def render_prior_meetings(result: dict[str, Any]) -> str:
    hits = result.get("prior_meetings") or []
    if not hits:
        return ""
    lines = ["Previous meetings", "-" * 17]
    for rank, hit in enumerate(hits, start=1):
        lines.append(_format_matchup_line(hit, rank=rank, show_similarity=True))
    lines.append("")
    return "\n".join(lines)


def render_similar_matchups(result: dict[str, Any]) -> str:
    hits = result.get("similar_matchups") or []
    if not hits:
        return ""
    lines = ["Comparable historical matchups (based off pre-fight career stats and physical attributes)", "-" * 30]
    for rank, hit in enumerate(hits, start=1):
        lines.append(_format_matchup_line(hit, rank=rank, show_similarity=True))
    lines.append("")
    return "\n".join(lines)


def render_matchup_history(result: dict[str, Any]) -> str:
    parts = [render_prior_meetings(result), render_similar_matchups(result)]
    return "".join(part for part in parts if part)
