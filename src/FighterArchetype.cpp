#include "ufc/FighterArchetype.hpp"

#include "ufc/RoundStats.hpp"

#include <array>
#include <cmath>

namespace ufc {

namespace {

constexpr int kMinRoundsToClassify = 3;

// Fallbacks when career metrics cannot be computed (NaN).
constexpr double kDefaultStrikeDefense = 0.5;
constexpr double kDefaultStrikesTakenPerRound = 5.0;

// Grappling vs striking index (first branch split).
constexpr double kGrapplingTakedownLandedWeight = 3.0;
constexpr double kGrapplingSubAttemptWeight = 4.0;
constexpr double kGrapplingControlTimeDivisor = 45.0;
constexpr double kStrikingKnockdownWeight = 2.0;
constexpr double kGrapplingDominanceStrikeMultiplier = 0.55;

// Control Time Specialist (grappling branch).
constexpr double kWrestlerMinTakedownsLandedPerRound = 0.45;
constexpr double kWrestlerMinTakedownsAttemptedPerRound = 1.0;
constexpr double kWrestlerMaxSubAttemptsPerRound = 0.12;

// Ground Finisher (grappling branch).
constexpr double kFinisherMinSubAttemptsPerRound = 0.12;
constexpr double kFinisherSoftMinSubAttemptsPerRound = 0.06;
constexpr double kFinisherSoftMinGroundStrikeRatio = 0.12;
constexpr double kFinisherMinControlTimePerRound = 45.0;

// All-Around Fighter.
constexpr double kAllAroundMinPace = 5.0;
constexpr double kAllAroundMinTakedownsAttemptedPerRound = 0.45;
constexpr double kAllAroundMinClinchStrikeRatio = 0.10;

// Pressure Striker (striking branch).
constexpr double kPressureStrikerMinDistanceRatio = 0.50;
constexpr double kPressureStrikerMaxTakedownsLandedPerRound = 0.35;
constexpr double kPressureStrikerMaxSubAttemptsPerRound = 0.08;
constexpr double kPressureStrikerMinSigPerRound = 3.5;

// Counter Striker: distance-oriented, defensively sound, limited grappling.
constexpr double kCounterMinDistanceRatio = 0.62;
constexpr double kCounterMinStrikeDefense = 0.54;
constexpr double kCounterMaxTakedownsLandedPerRound = kPressureStrikerMaxTakedownsLandedPerRound;
constexpr double kCounterMaxTakedownsAttemptedPerRound = 0.60;
constexpr double kCounterMaxSubAttemptsPerRound = kPressureStrikerMaxSubAttemptsPerRound;
constexpr double kCounterMaxSigPerRound = 22.0;
constexpr double kCounterMaxTakenPerRound = 18.0;
constexpr double kCounterLowVolumeMaxSigPerRound = 5.5;
constexpr double kCounterLowVolumeMaxTakenPerRound = 5.5;
constexpr double kCounterLowVolumeMinStrikeDefense = 0.56;

double ratio(int numerator, int denominator) {
    if (denominator == 0) {
        return 0.0;
    }
    return static_cast<double>(numerator) / static_cast<double>(denominator);
}

double valueOrDefault(double value, double default_value) {
    return std::isfinite(value) ? value : default_value;
}

}  // namespace

const char* toString(FighterArchetype archetype) {
    switch (archetype) {
        case FighterArchetype::PressureStriker:
            return "Pressure Striker";
        case FighterArchetype::ControlTimeSpecialist:
            return "Control Time Specialist";
        case FighterArchetype::GroundFinisher:
            return "Ground Finisher";
        case FighterArchetype::AllAroundFighter:
            return "All-Around Fighter";
        case FighterArchetype::CounterStriker:
            return "Counter Striker";
    }
    return "Unknown";
}

std::optional<FighterArchetype> parseArchetype(const std::string& label) {
    static const std::array<std::pair<const char*, FighterArchetype>, 5> kLabels = {{
        {"Pressure Striker", FighterArchetype::PressureStriker},
        {"Control Time Specialist", FighterArchetype::ControlTimeSpecialist},
        {"Ground Finisher", FighterArchetype::GroundFinisher},
        {"All-Around Fighter", FighterArchetype::AllAroundFighter},
        {"Counter Striker", FighterArchetype::CounterStriker},
    }};

    for (const auto& entry : kLabels) {
        if (label == entry.first) {
            return entry.second;
        }
    }
    return std::nullopt;
}

std::optional<FighterArchetype> classifyArchetype(const FighterCareerStats& stats) {
    if (stats.rounds < kMinRoundsToClassify) {
        return std::nullopt;
    }

    const double sig_per_round = stats.perRound(stats.sig_strikes_landed);
    const double distance_strikes_per_round = stats.perRound(stats.distance_strikes_landed);
    const double td_landed_per_round = stats.perRound(stats.takedowns_landed);
    const double td_attempted_per_round = stats.perRound(stats.takedowns_attempted);
    const double sub_per_round = stats.perRound(stats.sub_attempts);
    const double control_per_round = stats.perRound(stats.control_time_seconds);
    const double knockdowns_per_round = stats.perRound(stats.knockdowns);
    const double distance_ratio = ratio(stats.distance_strikes_landed, stats.sig_strikes_landed);
    const double clinch_ratio = ratio(stats.clinch_strikes_landed, stats.sig_strikes_landed);
    const double ground_ratio = ratio(stats.ground_strikes_landed, stats.sig_strikes_landed);
    const double strike_defense = valueOrDefault(stats.strikingDefense(), kDefaultStrikeDefense);
    const double taken_per_round = valueOrDefault(stats.strikesTakenPerRound(), kDefaultStrikesTakenPerRound);

    const double grappling_index = td_landed_per_round * kGrapplingTakedownLandedWeight +
                                   sub_per_round * kGrapplingSubAttemptWeight +
                                   control_per_round / kGrapplingControlTimeDivisor;
    const double striking_index =
        distance_strikes_per_round + knockdowns_per_round * kStrikingKnockdownWeight;

    if (grappling_index >= striking_index * kGrapplingDominanceStrikeMultiplier) {
        if ((td_landed_per_round >= kWrestlerMinTakedownsLandedPerRound ||
             td_attempted_per_round >= kWrestlerMinTakedownsAttemptedPerRound) &&
            sub_per_round < kWrestlerMaxSubAttemptsPerRound) {
            return FighterArchetype::ControlTimeSpecialist;
        }
        if (sub_per_round >= kFinisherMinSubAttemptsPerRound ||
            (sub_per_round >= kFinisherSoftMinSubAttemptsPerRound &&
             ground_ratio >= kFinisherSoftMinGroundStrikeRatio)) {
            return FighterArchetype::GroundFinisher;
        }
        if (td_landed_per_round >= kWrestlerMinTakedownsLandedPerRound ||
            td_attempted_per_round >= kWrestlerMinTakedownsAttemptedPerRound) {
            return FighterArchetype::ControlTimeSpecialist;
        }
        if (control_per_round >= kFinisherMinControlTimePerRound) {
            return FighterArchetype::GroundFinisher;
        }
        return FighterArchetype::ControlTimeSpecialist;
    }

    const double pace = sig_per_round + td_attempted_per_round;
    const bool high_pace = pace >= kAllAroundMinPace;
    const bool mixes_wrestling = td_attempted_per_round >= kAllAroundMinTakedownsAttemptedPerRound;
    const bool low_grappling = td_landed_per_round < kCounterMaxTakedownsLandedPerRound &&
                               td_attempted_per_round < kCounterMaxTakedownsAttemptedPerRound &&
                               sub_per_round < kCounterMaxSubAttemptsPerRound;
    const bool distance_counter = distance_ratio >= kCounterMinDistanceRatio &&
                                  strike_defense >= kCounterMinStrikeDefense && low_grappling &&
                                  sig_per_round <= kCounterMaxSigPerRound &&
                                  taken_per_round <= kCounterMaxTakenPerRound;
    const bool low_volume_counter = low_grappling && strike_defense >= kCounterLowVolumeMinStrikeDefense &&
                                    taken_per_round <= kCounterLowVolumeMaxTakenPerRound &&
                                    sig_per_round <= kCounterLowVolumeMaxSigPerRound;
    const bool range_striker = distance_ratio >= kPressureStrikerMinDistanceRatio &&
                               td_landed_per_round < kPressureStrikerMaxTakedownsLandedPerRound &&
                               sub_per_round < kPressureStrikerMaxSubAttemptsPerRound;

    if (distance_counter || low_volume_counter) {
        return FighterArchetype::CounterStriker;
    }
    if (high_pace && mixes_wrestling && clinch_ratio >= kAllAroundMinClinchStrikeRatio) {
        return FighterArchetype::AllAroundFighter;
    }
    if (range_striker) {
        return FighterArchetype::PressureStriker;
    }
    if (high_pace && mixes_wrestling) {
        return FighterArchetype::AllAroundFighter;
    }
    if (sig_per_round >= kPressureStrikerMinSigPerRound &&
        td_landed_per_round < kPressureStrikerMaxTakedownsLandedPerRound) {
        return FighterArchetype::PressureStriker;
    }
    return FighterArchetype::PressureStriker;
}

std::optional<FighterArchetype> classifyArchetypeForFighter(sqlite3* db, int64_t fighter_id) {
    const FighterCareerStats stats = FighterCareerStats::fromRoundStats(
        RoundStats::listForFighter(db, fighter_id),
        RoundStats::opponentTotalsForFighter(db, fighter_id));
    return classifyArchetype(stats);
}

}  // namespace ufc
