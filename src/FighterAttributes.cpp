#include "ufc/FighterAttributes.hpp"

#include "ufc/Fighter.hpp"
#include "ufc/RoundStats.hpp"

#include <cmath>
#include <limits>

namespace ufc {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

double finiteOrNan(double value) {
    return std::isfinite(value) ? value : kNaN;
}

std::optional<double> asDouble(const std::optional<int64_t>& value) {
    return value ? std::optional<double>(static_cast<double>(*value)) : std::nullopt;
}

void push(std::vector<double>& features, std::optional<double> value) {
    features.push_back(value ? finiteOrNan(*value) : kNaN);
}

double signedDelta(std::optional<double> lower, std::optional<double> higher) {
    if (!lower || !higher) {
        return kNaN;
    }
    return *higher - *lower;
}

void appendCareerStats(std::vector<double>& career_stats, const FighterCareerStats& career) {
    push(career_stats, career.perRound(career.sig_strikes_landed));
    push(career_stats, career.accuracy(career.sig_strikes_landed, career.sig_strikes_attempted));
    push(career_stats, career.strikingDefense());
    push(career_stats, career.strikesTakenPerRound());
    push(career_stats, career.accuracy(career.total_strikes_landed, career.total_strikes_attempted));
    push(career_stats, career.accuracy(career.takedowns_landed, career.takedowns_attempted));
    push(career_stats, career.takedownDefense());
    push(career_stats, career.perRound(career.takedowns_landed));
    push(career_stats, career.perRound(career.sub_attempts));
    push(career_stats, career.perRound(career.reversals));
    push(career_stats, career.perRound(career.knockdowns));
    push(career_stats, career.perRound(career.control_time_seconds));
    push(career_stats, career.perRound(career.head_strikes_landed));
    push(career_stats, career.perRound(career.body_strikes_landed));
    push(career_stats, career.perRound(career.leg_strikes_landed));
    push(career_stats, career.perRound(career.distance_strikes_landed));
    push(career_stats, career.perRound(career.clinch_strikes_landed));
    push(career_stats, career.perRound(career.ground_strikes_landed));
}

FighterAttributes makeAttributes(const Fighter& fighter, const FighterCareerStats& career) {
    FighterAttributes attrs;
    attrs.values.reserve(FighterAttributes::kCareerStatDimension);
    appendCareerStats(attrs.values, career);
    attrs.height_cm = asDouble(fighter.height_cm);
    attrs.reach_cm = asDouble(fighter.reach_cm);
    return attrs;
}

}  // namespace

FighterAttributes::FighterAttributes() : values(kCareerStatDimension, kNaN) {}

FighterAttributes::FighterAttributes(std::vector<double> values) : values(std::move(values)) {
    if (this->values.size() != kCareerStatDimension) {
        this->values.resize(kCareerStatDimension, kNaN);
    }
}

FighterAttributes FighterAttributes::fromFighter(const Fighter& fighter, const FighterCareerStats& career) {
    return makeAttributes(fighter, career);
}

FighterAttributes FighterAttributes::fromPrefight(const Fighter& fighter, const FighterCareerStats& career) {
    return makeAttributes(fighter, career);
}

FighterAttributes FighterAttributes::fromFighter(sqlite3* db, int64_t fighter_id) {
    const std::optional<Fighter> fighter = Fighter::getById(db, fighter_id);
    if (!fighter) {
        return {};
    }
    const FighterCareerStats career = FighterCareerStats::fromRoundStats(
        RoundStats::listForFighter(db, fighter_id),
        RoundStats::opponentTotalsForFighter(db, fighter_id));
    return fromFighter(*fighter, career);
}

std::vector<double> FighterAttributes::matchupVector(const FighterAttributes& lower_id_fighter, const FighterAttributes& higher_id_fighter) {
    std::vector<double> matchup_vector;
    matchup_vector.reserve(matchupVectorDimension());
    matchup_vector.insert(
        matchup_vector.end(), lower_id_fighter.values.begin(), lower_id_fighter.values.end());
    matchup_vector.insert(
        matchup_vector.end(), higher_id_fighter.values.begin(), higher_id_fighter.values.end());
    matchup_vector.push_back(signedDelta(lower_id_fighter.height_cm, higher_id_fighter.height_cm));
    matchup_vector.push_back(signedDelta(lower_id_fighter.reach_cm, higher_id_fighter.reach_cm));
    return matchup_vector;
}

void FighterAttributes::applyMatchupFeatureWeights(std::vector<double>& normalized) {
    if (normalized.size() <= kReachDeltaIndex) {
        return;
    }
    if (std::isfinite(normalized[kHeightDeltaIndex])) {
        normalized[kHeightDeltaIndex] *= kHeightDeltaWeight;
    }
    if (std::isfinite(normalized[kReachDeltaIndex])) {
        normalized[kReachDeltaIndex] *= kReachDeltaWeight;
    }
}

}  // namespace ufc
