#pragma once

#include "ufc/Fighter.hpp"
#include "ufc/RoundStats.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace ufc {

enum class ComparisonAdvantage { FighterA, FighterB, Tie, Unknown };

/// One side-by-side metric (profile or career aggregate).
struct StatComparison {
    std::string metric;
    std::optional<double> fighter_a;
    std::optional<double> fighter_b;
    std::optional<double> delta;  // fighter_a - fighter_b when both numeric
    ComparisonAdvantage advantage = ComparisonAdvantage::Unknown;
    std::optional<std::string> fighter_a_label;
    std::optional<std::string> fighter_b_label;
};

/// Side-by-side comparison of two fighters (profile + career round-stats aggregates).
class Matchup {
public:
    Fighter fighter_a;
    Fighter fighter_b;

    Matchup(Fighter a, Fighter b);

    /// Loads career round stats from the database and builds comparisons.
    static Matchup fromDatabase(sqlite3* db, const Fighter& a, const Fighter& b);
    static Matchup fromDatabase(sqlite3* db, int64_t fighter_a_id, int64_t fighter_b_id);

    const std::vector<StatComparison>& comparisons() const noexcept { return comparisons_; }

private:
    struct CareerTotals {
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

        static CareerTotals fromRoundStats(const std::vector<RoundStats>& rows);
        static CareerTotals fromRoundStats(
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

    void buildProfileComparisons();
    void buildCareerComparisons(const CareerTotals& a, const CareerTotals& b);
    void addNumeric(
        const std::string& metric,
        std::optional<double> a,
        std::optional<double> b,
        bool higher_is_better = true);
    void addCategorical(
        const std::string& metric,
        const std::optional<std::string>& a,
        const std::optional<std::string>& b);

    std::vector<StatComparison> comparisons_;
};

const char* toString(ComparisonAdvantage advantage);

}  // namespace ufc
