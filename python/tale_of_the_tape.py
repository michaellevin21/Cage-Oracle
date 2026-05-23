"""Format matchup JSON from the C++ engine as a console tale-of-the-tape."""

from __future__ import annotations

from typing import Any

PROFILE_METRICS = (
    "height_cm",
    "reach_cm",
    "momentum_score",
    "stance",
    "weight_class",
    "archetype",
)

METRIC_LABELS: dict[str, str] = {
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

    lines.append("Edge: > favors left fighter, < favors right, = even".center(width))
    return "\n".join(lines)
