"""Historical archetype-vs-archetype win rates by UFC ranking division."""

from __future__ import annotations

import sqlite3
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ufc_db_ffi import UfcCareerTotals, UfcDb

ARCHETYPE_ORDER = (
    "Pressure Striker",
    "Control Wrestler",
    "Ground Finisher",
    "All-Around Fighter",
    "Counter Striker",
)

ARCHETYPE_INDEX = {name: i for i, name in enumerate(ARCHETYPE_ORDER)}

P4P_MARKER = "pound-for-pound"

RANKINGS_WEIGHT_CLASS_ORDER = (
    "Flyweight",
    "Bantamweight",
    "Featherweight",
    "Lightweight",
    "Welterweight",
    "Middleweight",
    "Light Heavyweight",
    "Heavyweight",
    "Women's Strawweight",
    "Women's Flyweight",
    "Women's Bantamweight",
)

DEFAULT_MIN_SAMPLE = 5

_index_cache: dict[tuple, ArchetypeMatchupIndex] = {}


@dataclass
class PairCell:
    lower_wins: int = 0
    higher_wins: int = 0
    draws: int = 0
    mirror_decisive: int = 0

    @property
    def decisive(self) -> int:
        return self.lower_wins + self.higher_wins

    @property
    def total(self) -> int:
        return self.decisive + self.draws + self.mirror_decisive


@dataclass
class StyleWinRate:
    win_pct: float
    decisive: int
    draws: int


@dataclass
class ArchetypeMatchupIndex:
    """Win-rate tables keyed by ranked division then archetype pair."""

    mode: str
    by_weight: dict[str, dict[tuple[str, str], PairCell]]
    ranked_weight_classes: frozenset[str]

    @classmethod
    def load(
        cls,
        db_path: str | Path,
        *,
        mode: str = "current",
        lib_path: str | Path | None = None,
    ) -> ArchetypeMatchupIndex:
        db_path = Path(db_path)
        conn = sqlite3.connect(db_path)
        conn.row_factory = sqlite3.Row
        try:
            ranked = frozenset(load_ranked_weight_classes(conn))
            if mode == "current":
                by_weight, _, _ = build_matrix_current(
                    conn, allowed_weight_classes=set(ranked)
                )
            elif mode == "prefight":
                with UfcDb(db_path, lib_path=lib_path) as store:
                    by_weight, _, _ = build_matrix_prefight(
                        conn, store, allowed_weight_classes=set(ranked)
                    )
            else:
                raise ValueError(f"unknown archetype history mode: {mode!r}")
        finally:
            conn.close()
        return cls(mode=mode, by_weight=by_weight, ranked_weight_classes=ranked)

    def win_rate(
        self, archetype_a: str, archetype_b: str, weight_class: str
    ) -> StyleWinRate | None:
        if archetype_a == archetype_b:
            return None
        if weight_class not in self.by_weight:
            return None
        store = self.by_weight[weight_class]
        lower, higher = canonical_pair(archetype_a, archetype_b)
        cell = store.get((lower, higher))
        if not cell or cell.decisive == 0:
            return None
        wins = cell.lower_wins if archetype_a == lower else cell.higher_wins
        return StyleWinRate(
            win_pct=100.0 * wins / cell.decisive,
            decisive=cell.decisive,
            draws=cell.draws,
        )


def get_archetype_index(
    db_path: str | Path,
    *,
    mode: str = "current",
    lib_path: str | Path | None = None,
) -> ArchetypeMatchupIndex:
    db_path = Path(db_path)
    mtime = db_path.stat().st_mtime if db_path.is_file() else 0.0
    key = (str(db_path.resolve()), mtime, mode, str(lib_path) if lib_path else None)
    cached = _index_cache.get(key)
    if cached is not None:
        return cached
    index = ArchetypeMatchupIndex.load(db_path, mode=mode, lib_path=lib_path)
    _index_cache[key] = index
    return index


def canonical_pair(arch_a: str, arch_b: str) -> tuple[str, str]:
    if ARCHETYPE_INDEX[arch_a] <= ARCHETYPE_INDEX[arch_b]:
        return arch_a, arch_b
    return arch_b, arch_a


def is_pound_for_pound(weight_class: str) -> bool:
    return P4P_MARKER in weight_class.lower()


def load_ranked_weight_classes(conn: sqlite3.Connection) -> list[str]:
    rows = conn.execute(
        "SELECT DISTINCT weight_class FROM fighter_rankings ORDER BY weight_class"
    ).fetchall()
    ranked = [r[0] for r in rows if r[0] and not is_pound_for_pound(r[0])]
    order_index = {name: i for i, name in enumerate(RANKINGS_WEIGHT_CLASS_ORDER)}

    def sort_key(name: str) -> tuple[int, str]:
        return (order_index.get(name, len(RANKINGS_WEIGHT_CLASS_ORDER)), name)

    return sorted(ranked, key=sort_key)


def normalize_weight_class(label: str | None) -> str | None:
    if not label:
        return None
    trimmed = label.strip()
    return trimmed or None


def archetype_plural(label: str) -> str:
    """Pluralize a canonical archetype label while keeping title casing."""
    head, _, word = label.rpartition(" ")
    if not word:
        return label
    suffixes = {
        "Fighter": "Fighters",
        "Striker": "Strikers",
        "Wrestler": "Wrestlers",
        "Finisher": "Finishers",
    }
    plural_word = suffixes.get(word)
    if plural_word is None:
        return f"{label}s"
    return f"{head} {plural_word}" if head else plural_word


def weight_class_phrase(weight_class: str) -> str:
    return weight_class.strip()


def format_style_matchup_sentence(
    archetype_a: str,
    archetype_b: str,
    win_pct: float,
    weight_class: str,
    decisive: int,
) -> str:
    fight_word = "fight" if decisive == 1 else "fights"
    return (
        f"{archetype_plural(archetype_a)} win {win_pct:.0f}% against "
        f"{archetype_plural(archetype_b)} at {weight_class_phrase(weight_class)} "
        f"({decisive} {fight_word})."
    )


def resolve_matchup_weight_classes(
    fighter_a: dict[str, Any],
    fighter_b: dict[str, Any],
    ranked: set[str] | frozenset[str],
) -> list[str]:
    """Ranked divisions to use for historical style lookup (one or both fighters)."""
    wc_a = normalize_weight_class(fighter_a.get("weight_class"))
    wc_b = normalize_weight_class(fighter_b.get("weight_class"))
    classes: list[str] = []
    if wc_a and wc_a in ranked:
        classes.append(wc_a)
    if wc_b and wc_b in ranked and wc_b not in classes:
        classes.append(wc_b)
    order_index = {name: i for i, name in enumerate(RANKINGS_WEIGHT_CLASS_ORDER)}
    return sorted(classes, key=lambda name: (order_index.get(name, 999), name))


def fighter_archetype(fighter: dict[str, Any]) -> str | None:
    arch = fighter.get("archetype")
    if arch and arch in ARCHETYPE_INDEX:
        return arch
    return None


def style_matchup_summary(
    arch_a: str,
    arch_b: str,
    weight_class: str,
    index: ArchetypeMatchupIndex,
    *,
    min_sample: int = DEFAULT_MIN_SAMPLE,
) -> str:
    """One-line historical style outcome for a division."""
    if arch_a == arch_b:
        store = index.by_weight.get(weight_class, {})
        cell = store.get((arch_a, arch_a))
        total = cell.total if cell else 0
        return (
            f"Both fighters are {archetype_plural(arch_a)}; "
            f"{total} historical same-style bouts at {weight_class_phrase(weight_class)}."
        )

    rate_a = index.win_rate(arch_a, arch_b, weight_class)
    if not rate_a or rate_a.decisive < min_sample:
        n = rate_a.decisive if rate_a else 0
        return (
            f"Insufficient data for {archetype_plural(arch_a)} vs "
            f"{archetype_plural(arch_b)} at {weight_class_phrase(weight_class)} "
            f"({n} decisive fights, need {min_sample})."
        )

    if rate_a.win_pct > 50:
        return format_style_matchup_sentence(
            arch_a, arch_b, rate_a.win_pct, weight_class, rate_a.decisive
        )
    if rate_a.win_pct < 50:
        return format_style_matchup_sentence(
            arch_b, arch_a, 100.0 - rate_a.win_pct, weight_class, rate_a.decisive
        )

    fight_word = "fight" if rate_a.decisive == 1 else "fights"
    return (
        f"{archetype_plural(arch_a)} and {archetype_plural(arch_b)} win equally often "
        f"at {weight_class_phrase(weight_class)} ({rate_a.decisive} {fight_word})."
    )


def build_archetype_history(
    matchup: dict[str, Any],
    index: ArchetypeMatchupIndex,
    *,
    min_sample: int = DEFAULT_MIN_SAMPLE,
) -> dict[str, Any] | None:
    fighter_a = matchup["fighter_a"]
    fighter_b = matchup["fighter_b"]
    arch_a = fighter_archetype(fighter_a)
    arch_b = fighter_archetype(fighter_b)
    if not arch_a or not arch_b:
        return None

    weight_classes = resolve_matchup_weight_classes(
        fighter_a, fighter_b, index.ranked_weight_classes
    )
    if not weight_classes:
        return {
            "weight_classes": [],
            "reason": "no_ranked_division",
            "fighter_a_archetype": arch_a,
            "fighter_b_archetype": arch_b,
        }

    summaries = [
        style_matchup_summary(arch_a, arch_b, wc, index, min_sample=min_sample)
        for wc in weight_classes
    ]

    return {
        "weight_classes": weight_classes,
        "same_archetype": arch_a == arch_b,
        "fighter_a_archetype": arch_a,
        "fighter_b_archetype": arch_b,
        "min_sample": min_sample,
        "summaries": summaries,
        "summary": summaries[0] if len(summaries) == 1 else None,
    }


def attach_archetype_history(
    matchup: dict[str, Any],
    index: ArchetypeMatchupIndex,
    *,
    min_sample: int = DEFAULT_MIN_SAMPLE,
) -> dict[str, Any]:
    history = build_archetype_history(matchup, index, min_sample=min_sample)
    if history is not None:
        matchup["archetype_history"] = history
    return matchup


# --- historical bout aggregation ---


@dataclass
class CareerAccumulator:
    rounds: int = 0
    sig_strikes_landed: int = 0
    sig_strikes_attempted: int = 0
    total_strikes_landed: int = 0
    total_strikes_attempted: int = 0
    takedowns_landed: int = 0
    takedowns_attempted: int = 0
    opponent_sig_strikes_landed: int = 0
    opponent_sig_strikes_attempted: int = 0
    opponent_takedowns_landed: int = 0
    opponent_takedowns_attempted: int = 0
    sub_attempts: int = 0
    reversals: int = 0
    knockdowns: int = 0
    control_time_seconds: float = 0.0
    head_strikes_landed: int = 0
    body_strikes_landed: int = 0
    leg_strikes_landed: int = 0
    distance_strikes_landed: int = 0
    clinch_strikes_landed: int = 0
    ground_strikes_landed: int = 0

    def to_ctypes(self) -> UfcCareerTotals:
        return UfcCareerTotals(
            rounds=self.rounds,
            sig_strikes_landed=self.sig_strikes_landed,
            sig_strikes_attempted=self.sig_strikes_attempted,
            total_strikes_landed=self.total_strikes_landed,
            total_strikes_attempted=self.total_strikes_attempted,
            takedowns_landed=self.takedowns_landed,
            takedowns_attempted=self.takedowns_attempted,
            opponent_sig_strikes_landed=self.opponent_sig_strikes_landed,
            opponent_sig_strikes_attempted=self.opponent_sig_strikes_attempted,
            opponent_takedowns_landed=self.opponent_takedowns_landed,
            opponent_takedowns_attempted=self.opponent_takedowns_attempted,
            sub_attempts=self.sub_attempts,
            reversals=self.reversals,
            knockdowns=self.knockdowns,
            control_time_seconds=self.control_time_seconds,
            head_strikes_landed=self.head_strikes_landed,
            body_strikes_landed=self.body_strikes_landed,
            leg_strikes_landed=self.leg_strikes_landed,
            distance_strikes_landed=self.distance_strikes_landed,
            clinch_strikes_landed=self.clinch_strikes_landed,
            ground_strikes_landed=self.ground_strikes_landed,
        )

    def add_round(self, row: sqlite3.Row, *, opponent: sqlite3.Row) -> None:
        self.rounds += 1
        self.sig_strikes_landed += row["sig_strikes_landed"]
        self.sig_strikes_attempted += row["sig_strikes_attempted"]
        self.total_strikes_landed += row["total_strikes_landed"]
        self.total_strikes_attempted += row["total_strikes_attempted"]
        self.takedowns_landed += row["takedowns_landed"]
        self.takedowns_attempted += row["takedowns_attempted"]
        self.sub_attempts += row["sub_attempts"]
        self.reversals += row["reversals"]
        self.knockdowns += row["knockdowns"]
        self.control_time_seconds += row["control_time_seconds"]
        self.head_strikes_landed += row["head_strikes_landed"]
        self.body_strikes_landed += row["body_strikes_landed"]
        self.leg_strikes_landed += row["leg_strikes_landed"]
        self.distance_strikes_landed += row["distance_strikes_landed"]
        self.clinch_strikes_landed += row["clinch_strikes_landed"]
        self.ground_strikes_landed += row["ground_strikes_landed"]
        self.opponent_sig_strikes_landed += opponent["sig_strikes_landed"]
        self.opponent_sig_strikes_attempted += opponent["sig_strikes_attempted"]
        self.opponent_takedowns_landed += opponent["takedowns_landed"]
        self.opponent_takedowns_attempted += opponent["takedowns_attempted"]


def get_pair_cell(
    store: dict[tuple[str, str], PairCell], arch_a: str, arch_b: str
) -> PairCell:
    key = canonical_pair(arch_a, arch_b)
    if key not in store:
        store[key] = PairCell()
    return store[key]


def record_outcome(
    store: dict[tuple[str, str], PairCell],
    arch_a: str,
    arch_b: str,
    winner_arch: str | None,
) -> None:
    lower, higher = canonical_pair(arch_a, arch_b)
    cell = get_pair_cell(store, arch_a, arch_b)
    if lower == higher:
        if winner_arch is None:
            cell.draws += 1
        else:
            cell.mirror_decisive += 1
        return
    if winner_arch is None:
        cell.draws += 1
    elif winner_arch == lower:
        cell.lower_wins += 1
    elif winner_arch == higher:
        cell.higher_wins += 1


def winner_archetype(
    winner_id: int | None,
    fighter1_id: int,
    fighter2_id: int,
    arch1: str,
    arch2: str,
) -> str | None:
    if winner_id is None or winner_id == 0:
        return None
    if winner_id == fighter1_id:
        return arch1
    if winner_id == fighter2_id:
        return arch2
    return None


def record_fight(
    by_weight: dict[str, dict[tuple[str, str], PairCell]],
    weight_class: str | None,
    arch1: str | None,
    arch2: str | None,
    winner_id: int | None,
    fighter1_id: int,
    fighter2_id: int,
    *,
    allowed_weight_classes: set[str],
) -> bool:
    if not weight_class or weight_class not in allowed_weight_classes:
        return False
    if not arch1 or not arch2:
        return False
    if arch1 not in ARCHETYPE_INDEX or arch2 not in ARCHETYPE_INDEX:
        return False
    wc_store = by_weight.setdefault(weight_class, {})
    winner_arch = winner_archetype(winner_id, fighter1_id, fighter2_id, arch1, arch2)
    record_outcome(wc_store, arch1, arch2, winner_arch)
    return True


def build_matrix_current(
    conn: sqlite3.Connection,
    *,
    allowed_weight_classes: set[str],
) -> tuple[dict[str, dict[tuple[str, str], PairCell]], int, int]:
    by_weight: dict[str, dict[tuple[str, str], PairCell]] = {}
    included = 0
    skipped = 0
    query = """
        SELECT f.fighter1_id, f.fighter2_id, f.winner_id,
               f.weight_class,
               f1.archetype AS arch1, f2.archetype AS arch2
        FROM fights f
        JOIN fighters f1 ON f1.id = f.fighter1_id
        JOIN fighters f2 ON f2.id = f.fighter2_id
    """
    for row in conn.execute(query):
        wc = normalize_weight_class(row["weight_class"])
        if record_fight(
            by_weight,
            wc,
            row["arch1"],
            row["arch2"],
            row["winner_id"],
            row["fighter1_id"],
            row["fighter2_id"],
            allowed_weight_classes=allowed_weight_classes,
        ):
            included += 1
        else:
            skipped += 1
    return by_weight, included, skipped


def build_matrix_prefight(
    conn: sqlite3.Connection,
    store: UfcDb,
    *,
    allowed_weight_classes: set[str],
) -> tuple[dict[str, dict[tuple[str, str], PairCell]], int, int]:
    by_weight: dict[str, dict[tuple[str, str], PairCell]] = {}
    included = 0
    skipped = 0

    fights = conn.execute(
        """
        SELECT f.id, f.fighter1_id, f.fighter2_id, f.winner_id, f.weight_class
        FROM fights f
        INNER JOIN events e ON e.id = f.event_id
        ORDER BY e.event_date ASC, f.id ASC
        """
    ).fetchall()

    round_rows = conn.execute(
        """
        SELECT rs.fight_id, rs.fighter_id, rs.round_number,
               rs.sig_strikes_landed, rs.sig_strikes_attempted,
               rs.total_strikes_landed, rs.total_strikes_attempted,
               rs.takedowns_landed, rs.takedowns_attempted,
               rs.sub_attempts, rs.reversals, rs.knockdowns,
               rs.control_time_seconds, rs.head_strikes_landed,
               rs.body_strikes_landed, rs.leg_strikes_landed,
               rs.distance_strikes_landed, rs.clinch_strikes_landed,
               rs.ground_strikes_landed
        FROM round_stats rs
        ORDER BY rs.fight_id, rs.round_number, rs.fighter_id
        """
    ).fetchall()

    by_fight: dict[int, list[sqlite3.Row]] = defaultdict(list)
    for row in round_rows:
        by_fight[row["fight_id"]].append(row)

    career: dict[int, CareerAccumulator] = defaultdict(CareerAccumulator)

    for fight in fights:
        fid = fight["id"]
        f1 = fight["fighter1_id"]
        f2 = fight["fighter2_id"]
        arch1 = store.classify_archetype_from_totals(career[f1].to_ctypes())
        arch2 = store.classify_archetype_from_totals(career[f2].to_ctypes())
        wc = normalize_weight_class(fight["weight_class"])
        if record_fight(
            by_weight,
            wc,
            arch1,
            arch2,
            fight["winner_id"],
            f1,
            f2,
            allowed_weight_classes=allowed_weight_classes,
        ):
            included += 1
        else:
            skipped += 1

        stats = by_fight.get(fid, [])
        by_fighter_round: dict[tuple[int, int], sqlite3.Row] = {}
        for row in stats:
            by_fighter_round[(row["fighter_id"], row["round_number"])] = row

        for (fighter_id, round_number), row in by_fighter_round.items():
            opponent_id = f2 if fighter_id == f1 else f1
            opp = by_fighter_round.get((opponent_id, round_number))
            if opp is None:
                continue
            career[fighter_id].add_round(row, opponent=opp)

    return by_weight, included, skipped
