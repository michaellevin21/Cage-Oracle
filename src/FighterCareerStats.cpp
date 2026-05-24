#include "ufc/FighterCareerStats.hpp"

namespace ufc {

FighterCareerStats FighterCareerStats::fromRoundStats(
    const std::vector<RoundStats>& rows,
    const RoundStats::OpponentRoundTotals& opponent) {
    FighterCareerStats totals = fromRoundStats(rows);
    totals.opponent_sig_strikes_attempted = opponent.sig_strikes_attempted;
    totals.opponent_sig_strikes_landed = opponent.sig_strikes_landed;
    totals.opponent_takedowns_attempted = opponent.takedowns_attempted;
    totals.opponent_takedowns_landed = opponent.takedowns_landed;
    return totals;
}

FighterCareerStats FighterCareerStats::fromRoundStats(const std::vector<RoundStats>& rows) {
    FighterCareerStats totals;
    totals.rounds = static_cast<int>(rows.size());
    for (const RoundStats& row : rows) {
        totals.sig_strikes_landed += row.sig_strikes_landed;
        totals.sig_strikes_attempted += row.sig_strikes_attempted;
        totals.total_strikes_landed += row.total_strikes_landed;
        totals.total_strikes_attempted += row.total_strikes_attempted;
        totals.takedowns_landed += row.takedowns_landed;
        totals.takedowns_attempted += row.takedowns_attempted;
        totals.sub_attempts += row.sub_attempts;
        totals.reversals += row.reversals;
        totals.knockdowns += row.knockdowns;
        totals.control_time_seconds += row.control_time_seconds;
        totals.head_strikes_landed += row.head_strikes_landed;
        totals.body_strikes_landed += row.body_strikes_landed;
        totals.leg_strikes_landed += row.leg_strikes_landed;
        totals.distance_strikes_landed += row.distance_strikes_landed;
        totals.clinch_strikes_landed += row.clinch_strikes_landed;
        totals.ground_strikes_landed += row.ground_strikes_landed;
    }
    return totals;
}

double FighterCareerStats::accuracy(int landed, int attempted) const {
    if (attempted == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(landed) / static_cast<double>(attempted);
}

double FighterCareerStats::defensePct(int landed_against, int attempted_against) const {
    if (attempted_against == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const int defended = attempted_against - landed_against;
    return static_cast<double>(defended) / static_cast<double>(attempted_against);
}

double FighterCareerStats::strikingDefense() const {
    return defensePct(opponent_sig_strikes_landed, opponent_sig_strikes_attempted);
}

double FighterCareerStats::takedownDefense() const {
    return defensePct(opponent_takedowns_landed, opponent_takedowns_attempted);
}

double FighterCareerStats::strikesTakenPerRound() const {
    return perRound(opponent_sig_strikes_landed);
}

double FighterCareerStats::perRound(int total) const {
    if (rounds == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return static_cast<double>(total) / static_cast<double>(rounds);
}

double FighterCareerStats::perRound(double total) const {
    if (rounds == 0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return total / static_cast<double>(rounds);
}

}  // namespace ufc
