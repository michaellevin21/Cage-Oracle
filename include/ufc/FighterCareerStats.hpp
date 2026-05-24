#pragma once

#include "ufc/RoundStats.hpp"

#include <vector>

namespace ufc {

/// Career aggregates from `round_stats` rows (used by matchups and archetype classification).
struct FighterCareerStats {
    int rounds = 0;
    int sig_strikes_landed = 0;
    int sig_strikes_attempted = 0;
    int total_strikes_landed = 0;
    int total_strikes_attempted = 0;
    int takedowns_landed = 0;
    int takedowns_attempted = 0;
    int opponent_sig_strikes_landed = 0;
    int opponent_sig_strikes_attempted = 0;
    int opponent_takedowns_landed = 0;
    int opponent_takedowns_attempted = 0;
    int sub_attempts = 0;
    int reversals = 0;
    int knockdowns = 0;
    double control_time_seconds = 0.0;
    int head_strikes_landed = 0;
    int body_strikes_landed = 0;
    int leg_strikes_landed = 0;
    int distance_strikes_landed = 0;
    int clinch_strikes_landed = 0;
    int ground_strikes_landed = 0;

    static FighterCareerStats fromRoundStats(const std::vector<RoundStats>& rows);
    static FighterCareerStats fromRoundStats(
        const std::vector<RoundStats>& rows,
        const RoundStats::OpponentRoundTotals& opponent);

    double accuracy(int landed, int attempted) const;
    double defensePct(int landed_against, int attempted_against) const;
    double strikingDefense() const;
    double takedownDefense() const;
    double strikesTakenPerRound() const;
    double perRound(int total) const;
    double perRound(double total) const;
};

}  // namespace ufc
