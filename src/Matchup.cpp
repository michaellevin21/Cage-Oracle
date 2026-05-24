#include "ufc/Matchup.hpp"

#include "ufc/RoundStats.hpp"

#include <cmath>
#include <limits>
#include <utility>

namespace ufc {

namespace {

constexpr double kEpsilon = 1e-9;

ComparisonAdvantage advantageFromDelta(double delta, bool higher_is_better) {
    if (std::abs(delta) < kEpsilon) {
        return ComparisonAdvantage::Tie;
    }
    if (higher_is_better) {
        return delta > 0 ? ComparisonAdvantage::FighterA : ComparisonAdvantage::FighterB;
    }
    return delta < 0 ? ComparisonAdvantage::FighterA : ComparisonAdvantage::FighterB;
}

std::optional<double> asDouble(const std::optional<int64_t>& value) {
    return value ? std::optional<double>(static_cast<double>(*value)) : std::nullopt;
}

std::optional<double> optionalDouble(const std::optional<double>& value) {
    return value;
}

}  // namespace

const char* toString(ComparisonAdvantage advantage) {
    switch (advantage) {
        case ComparisonAdvantage::FighterA:
            return "fighter_a";
        case ComparisonAdvantage::FighterB:
            return "fighter_b";
        case ComparisonAdvantage::Tie:
            return "tie";
    }
    return "unknown";
}

Matchup::Matchup(Fighter a, Fighter b) : fighter_a(std::move(a)), fighter_b(std::move(b)) {
    buildProfileComparisons();
}

Matchup Matchup::fromDatabase(sqlite3* db, const Fighter& a, const Fighter& b) {
    Matchup matchup(a, b);
    const FighterCareerStats totals_a = FighterCareerStats::fromRoundStats(
        RoundStats::listForFighter(db, a.id),
        RoundStats::opponentTotalsForFighter(db, a.id));
    const FighterCareerStats totals_b = FighterCareerStats::fromRoundStats(
        RoundStats::listForFighter(db, b.id),
        RoundStats::opponentTotalsForFighter(db, b.id));
    matchup.buildCareerComparisons(totals_a, totals_b);
    return matchup;
}

Matchup Matchup::fromDatabase(sqlite3* db, int64_t fighter_a_id, int64_t fighter_b_id) {
    const std::optional<Fighter> a = Fighter::getById(db, fighter_a_id);
    const std::optional<Fighter> b = Fighter::getById(db, fighter_b_id);
    if (!a || !b) {
        return Matchup({}, {});
    }
    return fromDatabase(db, *a, *b);
}

void Matchup::addNumeric(const std::string& metric, std::optional<double> a, std::optional<double> b, bool higher_is_better) {
    auto valid = [](const std::optional<double>& v) {
        return v && !std::isnan(*v);
    };

    StatComparison row;
    row.metric = metric;
    row.fighter_a = valid(a) ? a : std::nullopt;
    row.fighter_b = valid(b) ? b : std::nullopt;

    if (valid(a) && valid(b)) {
        row.delta = *a - *b;
        row.advantage = advantageFromDelta(*row.delta, higher_is_better);
    }

    comparisons_.push_back(std::move(row));
}

void Matchup::addCategorical(const std::string& metric, const std::optional<std::string>& a, const std::optional<std::string>& b) {
    StatComparison row;
    row.metric = metric;
    row.fighter_a_label = a;
    row.fighter_b_label = b;
    comparisons_.push_back(std::move(row));
}

void Matchup::buildProfileComparisons() {
    addNumeric("height_cm", asDouble(fighter_a.height_cm), asDouble(fighter_b.height_cm));
    addNumeric("reach_cm", asDouble(fighter_a.reach_cm), asDouble(fighter_b.reach_cm));
    addNumeric("momentum_score", optionalDouble(fighter_a.momentum_score), optionalDouble(fighter_b.momentum_score));
    addCategorical("stance", fighter_a.stance, fighter_b.stance);
    addCategorical("weight_class", std::optional<std::string>(fighter_a.weight_class), std::optional<std::string>(fighter_b.weight_class));
    addCategorical("archetype", fighter_a.archetype, fighter_b.archetype);
}

void Matchup::buildCareerComparisons(const FighterCareerStats& a, const FighterCareerStats& b) {
    addNumeric("career_rounds", a.rounds, b.rounds);

    addNumeric("sig_strikes_landed_per_round", a.perRound(a.sig_strikes_landed), b.perRound(b.sig_strikes_landed));
    addNumeric("sig_strike_accuracy", a.accuracy(a.sig_strikes_landed, a.sig_strikes_attempted), b.accuracy(b.sig_strikes_landed, b.sig_strikes_attempted));
    addNumeric("striking_defense", a.strikingDefense(), b.strikingDefense());
    addNumeric("strikes_taken_per_round", a.strikesTakenPerRound(), b.strikesTakenPerRound(), false);
    addNumeric("total_strike_accuracy", a.accuracy(a.total_strikes_landed, a.total_strikes_attempted), b.accuracy(b.total_strikes_landed, b.total_strikes_attempted));
    addNumeric("takedown_accuracy", a.accuracy(a.takedowns_landed, a.takedowns_attempted), b.accuracy(b.takedowns_landed, b.takedowns_attempted));
    addNumeric("takedown_defense", a.takedownDefense(), b.takedownDefense());
    addNumeric("takedowns_landed_per_round", a.perRound(a.takedowns_landed), b.perRound(b.takedowns_landed));
    addNumeric("sub_attempts_per_round", a.perRound(a.sub_attempts), b.perRound(b.sub_attempts));
    addNumeric("reversals_per_round", a.perRound(a.reversals), b.perRound(b.reversals));
    addNumeric("knockdowns_per_round", a.perRound(a.knockdowns), b.perRound(b.knockdowns));
    addNumeric("control_time_seconds_per_round", a.perRound(a.control_time_seconds), b.perRound(b.control_time_seconds));
    addNumeric("head_strikes_landed_per_round", a.perRound(a.head_strikes_landed), b.perRound(b.head_strikes_landed));
    addNumeric("body_strikes_landed_per_round", a.perRound(a.body_strikes_landed), b.perRound(b.body_strikes_landed));
    addNumeric("leg_strikes_landed_per_round", a.perRound(a.leg_strikes_landed), b.perRound(b.leg_strikes_landed));
    addNumeric("distance_strikes_landed_per_round", a.perRound(a.distance_strikes_landed), b.perRound(b.distance_strikes_landed));
    addNumeric("clinch_strikes_landed_per_round", a.perRound(a.clinch_strikes_landed), b.perRound(b.clinch_strikes_landed));
    addNumeric("ground_strikes_landed_per_round", a.perRound(a.ground_strikes_landed), b.perRound(b.ground_strikes_landed));
}

}  // namespace ufc
