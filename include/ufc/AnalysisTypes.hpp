#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ufc {

struct FighterSummary {
    std::string name;
    std::optional<std::string> weight_class;
};

struct ComparisonRow {
    std::string metric;
    std::string label;
    std::string fighter_a;
    std::string fighter_b;
    std::optional<std::string> advantage;  // "fighter_a", "fighter_b", "tie", or null
    std::string edge;
};

struct Prediction {
    std::string type;  // "even" or "winner"
    std::optional<std::string> winner_name;
    double p_fighter_a = 0.5;
    double p_fighter_b = 0.5;
    double certainty_pct = 50.0;
};

struct TaleOfTheTape {
    struct FighterInfo {
        std::string name;
        std::optional<std::string> weight_class;
    };
    FighterInfo fighter_a;
    FighterInfo fighter_b;
    std::vector<ComparisonRow> profile;
    std::vector<ComparisonRow> career;
    std::vector<std::string> archetype_summaries;
    std::optional<Prediction> prediction;
};

struct HistoryFight {
    std::string fighter1_name;
    std::string fighter2_name;
    int64_t fighter1_id = 0;
    int64_t fighter2_id = 0;
    int64_t winner_id = 0;
    bool fighter1_won = false;
    bool fighter2_won = false;
    std::string event_name;
    std::optional<std::string> event_date;
    std::optional<double> similarity;
    std::optional<double> similarity_pct;
};

struct MatchupHistory {
    std::vector<HistoryFight> prior_meetings;
    std::vector<HistoryFight> similar_matchups;
};

struct ResumeWin {
    std::string event_date;
    std::string event_name;
    std::string opponent_name;
    std::string result_method;
    std::string opponent_rank_label;
    int points = 0;
    int running_total = 0;
};

struct ResumeBreakdown {
    int score = 0;
    std::vector<ResumeWin> ranked_wins;
    int unranked_win_count = 0;
    int skipped_non_decisive = 0;
};

struct MomentumFight {
    std::string event_date;
    std::string event_name;
    std::string opponent_name;
    std::string result;  // "W" or "L"
    std::string result_method;
    std::string opponent_rank_label;
    double recency = 0.0;
    double opp_quality = 0.0;
    double finish_mult = 1.0;
    double contribution = 0.0;
    double weighted_contribution = 0.0;
};

struct MomentumBreakdown {
    std::string status;  // ok, no_fights, inactive, insufficient_fights
    std::optional<double> score;
    std::optional<double> days_since_last_fight;
    int min_decisive_fights = 3;
    int inactivity_days = 730;
    std::vector<MomentumFight> fights;
    std::optional<double> weighted_average;
    std::optional<double> finish_rate;
    std::optional<double> finish_boost;
    std::optional<double> adjusted;
    std::optional<double> neutral_score;
    std::optional<double> max_fight_contribution;
};

struct MatchupResponse {
    TaleOfTheTape tape;
    MatchupHistory history;
    std::optional<std::string> no_prediction_reason;
    struct ResumeSideBreakdown {
        ResumeBreakdown fighter_a;
        ResumeBreakdown fighter_b;
    } resume_breakdown;
    struct MomentumSideBreakdown {
        MomentumBreakdown fighter_a;
        MomentumBreakdown fighter_b;
    } momentum_breakdown;
};

}  // namespace ufc
