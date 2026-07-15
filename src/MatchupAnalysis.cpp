#include "ufc/MatchupAnalysis.hpp"

#include "ufc/ArchetypeMatchupHistory.hpp"
#include "ufc/Fighter.hpp"
#include "ufc/Matchup.hpp"
#include "ufc/ScoreBreakdown.hpp"
#include "ufc/SimilaritySearch.hpp"
#include "ufc/SqliteHelpers.hpp"
#include "ufc/WinProbability.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace ufc {

namespace {

const char* kProfileMetrics[] = {
    "age", "height_cm", "reach_cm", "momentum_score", "resume_score",
    "stance", "weight_class", "archetype",
};

const char* kCareerMetrics[] = {
    "career_rounds",
    "sig_strikes_landed_per_round",
    "sig_strike_accuracy",
    "striking_defense",
    "strikes_taken_per_round",
    "total_strike_accuracy",
    "takedown_accuracy",
    "takedown_defense",
    "takedowns_landed_per_round",
    "sub_attempts_per_round",
    "reversals_per_round",
    "knockdowns_per_round",
    "control_time_seconds_per_round",
    "head_strikes_landed_per_round",
    "body_strikes_landed_per_round",
    "leg_strikes_landed_per_round",
    "distance_strikes_landed_per_round",
    "clinch_strikes_landed_per_round",
    "ground_strikes_landed_per_round",
};

const std::unordered_set<std::string> kBlankEdgeMetrics = {
    "stance", "weight_class", "archetype", "career_rounds",
};

const std::unordered_map<std::string, std::string> kMetricLabels = {
    {"age", "Age"},
    {"height_cm", "Height (cm)"},
    {"reach_cm", "Reach (cm)"},
    {"momentum_score", "Momentum"},
    {"resume_score", "Resume"},
    {"stance", "Stance"},
    {"weight_class", "Weight class"},
    {"archetype", "Archetype"},
    {"career_rounds", "Career rounds tracked"},
    {"sig_strikes_landed_per_round", "Sig. strikes landed / round"},
    {"sig_strike_accuracy", "Sig. strike accuracy"},
    {"striking_defense", "Striking defense"},
    {"strikes_taken_per_round", "Strikes taken / round"},
    {"total_strike_accuracy", "Total strike accuracy"},
    {"takedown_accuracy", "Takedown accuracy"},
    {"takedown_defense", "Takedown defense"},
    {"takedowns_landed_per_round", "Takedowns landed / round"},
    {"sub_attempts_per_round", "Submission attempts / round"},
    {"reversals_per_round", "Reversals / round"},
    {"knockdowns_per_round", "Knockdowns / round"},
    {"control_time_seconds_per_round", "Control time / round"},
    {"head_strikes_landed_per_round", "Head strikes landed / round"},
    {"body_strikes_landed_per_round", "Body strikes landed / round"},
    {"leg_strikes_landed_per_round", "Leg strikes landed / round"},
    {"distance_strikes_landed_per_round", "Distance strikes landed / round"},
    {"clinch_strikes_landed_per_round", "Clinch strikes landed / round"},
    {"ground_strikes_landed_per_round", "Ground strikes landed / round"},
};

std::string metricLabel(const std::string& metric) {
    const auto it = kMetricLabels.find(metric);
    if (it != kMetricLabels.end()) {
        return it->second;
    }
    std::string label = metric;
    for (char& c : label) {
        if (c == '_') {
            c = ' ';
        }
    }
    if (!label.empty()) {
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    }
    return label;
}

std::optional<int> ageFromDob(std::optional<int64_t> dob) {
    if (!dob) {
        return std::nullopt;
    }
    const std::time_t t = static_cast<std::time_t>(*dob);
    std::tm born{};
#ifdef _WIN32
    if (gmtime_s(&born, &t) != 0) {
        return std::nullopt;
    }
#else
    if (!gmtime_r(&t, &born)) {
        return std::nullopt;
    }
#endif
    const std::time_t now_t = std::time(nullptr);
    std::tm today{};
#ifdef _WIN32
    gmtime_s(&today, &now_t);
#else
    gmtime_r(&now_t, &today);
#endif
    int age = today.tm_year + 1900 - (born.tm_year + 1900);
    if (today.tm_mon < born.tm_mon ||
        (today.tm_mon == born.tm_mon && today.tm_mday < born.tm_mday)) {
        age -= 1;
    }
    return age;
}

std::string lastName(const std::string& full) {
    const size_t pos = full.find_last_of(' ');
    return pos == std::string::npos ? full : full.substr(pos + 1);
}

std::string edgeNameLabel(const std::string& full, const std::string& other) {
    const std::string last = lastName(full);
    if (lastName(other) == last) {
        const size_t pos = full.find(' ');
        if (pos != std::string::npos && pos > 0) {
            return std::string(1, full[0]) + ". " + last;
        }
    }
    return last;
}

std::string formatValue(
    const std::string& metric,
    const StatComparison& row,
    bool side_a) {
    if (side_a && row.fighter_a_label) {
        return *row.fighter_a_label;
    }
    if (!side_a && row.fighter_b_label) {
        return *row.fighter_b_label;
    }
    const auto val = side_a ? row.fighter_a : row.fighter_b;
    if (!val) {
        return "-";
    }
    if (metric.find("_accuracy") != std::string::npos ||
        metric.find("_defense") != std::string::npos) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << (*val * 100.0) << "%";
        return oss.str();
    }
    if (metric == "control_time_seconds_per_round") {
        const int total = static_cast<int>(std::round(*val));
        const int minutes = total / 60;
        const int seconds = total % 60;
        std::ostringstream oss;
        oss << minutes << ":" << std::setfill('0') << std::setw(2) << seconds;
        return oss.str();
    }
    if (metric == "age" || metric == "height_cm" || metric == "reach_cm" || metric == "career_rounds") {
        return std::to_string(static_cast<int>(std::round(*val)));
    }
    if (metric == "momentum_score") {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << *val;
        return oss.str();
    }
    if (metric == "resume_score") {
        return std::to_string(static_cast<int>(std::round(*val)));
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << *val;
    return oss.str();
}

std::string formatEventDate(int64_t event_date) {
    if (event_date <= 0) {
        return {};
    }
    std::time_t t = static_cast<std::time_t>(event_date);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d");
    return oss.str();
}

ComparisonRow buildComparisonRow(
    const std::string& metric,
    const StatComparison& row,
    const std::string& edge_a,
    const std::string& edge_b) {
    ComparisonRow out;
    out.metric = metric;
    out.label = metricLabel(metric);
    out.fighter_a = formatValue(metric, row, true);
    out.fighter_b = formatValue(metric, row, false);

    if (kBlankEdgeMetrics.count(metric)) {
        out.edge = "";
        out.advantage = std::nullopt;
        return out;
    }

    const std::string val_a = out.fighter_a;
    const std::string val_b = out.fighter_b;
    if (val_a != "-" && val_b != "-" && val_a == val_b) {
        out.edge = "Even";
        out.advantage = std::nullopt;
        return out;
    }

    std::string edge;
    if (row.advantage == ComparisonAdvantage::FighterA) {
        edge = edge_a;
    } else if (row.advantage == ComparisonAdvantage::FighterB) {
        edge = edge_b;
    } else if (row.advantage == ComparisonAdvantage::Tie) {
        edge = "Even";
    }
    out.edge = edge;
    if (edge == "Even" || edge.empty()) {
        out.advantage = std::nullopt;
    } else if (row.advantage == ComparisonAdvantage::FighterA) {
        out.advantage = "fighter_a";
    } else if (row.advantage == ComparisonAdvantage::FighterB) {
        out.advantage = "fighter_b";
    } else if (row.advantage == ComparisonAdvantage::Tie) {
        out.advantage = "tie";
    }
    return out;
}

std::optional<double> liveMomentumScore(const MomentumBreakdown& breakdown) {
    if (breakdown.status == "inactive") {
        return 0.0;
    }
    if (breakdown.status == "ok") {
        return breakdown.score;
    }
    return std::nullopt;
}

std::optional<double> liveResumeScore(const ResumeBreakdown& breakdown) {
    return static_cast<double>(breakdown.score);
}

TaleOfTheTape buildTape(
    const Matchup& matchup,
    const std::optional<WinProbabilityResult>& win_prob,
    const std::vector<std::string>& archetype_summaries) {
    TaleOfTheTape tape;
    tape.fighter_a.name = matchup.fighter_a.name;
    tape.fighter_a.weight_class = matchup.fighter_a.weight_class.empty()
        ? std::nullopt : std::optional<std::string>(matchup.fighter_a.weight_class);
    tape.fighter_b.name = matchup.fighter_b.name;
    tape.fighter_b.weight_class = matchup.fighter_b.weight_class.empty()
        ? std::nullopt : std::optional<std::string>(matchup.fighter_b.weight_class);

    const std::string edge_a = edgeNameLabel(matchup.fighter_a.name, matchup.fighter_b.name);
    const std::string edge_b = edgeNameLabel(matchup.fighter_b.name, matchup.fighter_a.name);

    std::unordered_map<std::string, StatComparison> by_metric;
    for (const StatComparison& row : matchup.comparisons()) {
        by_metric[row.metric] = row;
    }

    StatComparison age_row;
    age_row.metric = "age";
    const auto age_a = ageFromDob(matchup.fighter_a.date_of_birth);
    const auto age_b = ageFromDob(matchup.fighter_b.date_of_birth);
    if (age_a) {
        age_row.fighter_a = static_cast<double>(*age_a);
    }
    if (age_b) {
        age_row.fighter_b = static_cast<double>(*age_b);
    }
    if (age_a && age_b) {
        age_row.delta = static_cast<double>(*age_a - *age_b);
        if (*age_a == *age_b) {
            age_row.advantage = ComparisonAdvantage::Tie;
        } else if (*age_a < *age_b) {
            age_row.advantage = ComparisonAdvantage::FighterA;
        } else {
            age_row.advantage = ComparisonAdvantage::FighterB;
        }
    }
    if (age_a || age_b) {
        by_metric["age"] = age_row;
    }

    for (const char* m : kProfileMetrics) {
        const auto it = by_metric.find(m);
        if (it != by_metric.end()) {
            tape.profile.push_back(buildComparisonRow(m, it->second, edge_a, edge_b));
        }
    }
    for (const char* m : kCareerMetrics) {
        const auto it = by_metric.find(m);
        if (it != by_metric.end()) {
            tape.career.push_back(buildComparisonRow(m, it->second, edge_a, edge_b));
        }
    }

    tape.archetype_summaries = archetype_summaries;

    if (win_prob) {
        Prediction pred;
        pred.p_fighter_a = win_prob->p_fighter_a;
        pred.p_fighter_b = win_prob->p_fighter_b;
        if (std::abs(win_prob->p_fighter_a - win_prob->p_fighter_b) < 1e-9) {
            pred.type = "even";
            pred.winner_name = std::nullopt;
            pred.certainty_pct = 100.0 * win_prob->p_fighter_a;
        } else if (win_prob->p_fighter_a > win_prob->p_fighter_b) {
            pred.type = "winner";
            pred.winner_name = matchup.fighter_a.name;
            pred.certainty_pct = 100.0 * win_prob->p_fighter_a;
        } else {
            pred.type = "winner";
            pred.winner_name = matchup.fighter_b.name;
            pred.certainty_pct = 100.0 * win_prob->p_fighter_b;
        }
        tape.prediction = pred;
    }

    return tape;
}

HistoryFight buildHistoryHit(
    sqlite3* db, const SimilarMatchupHit& hit, bool show_similarity) {
    HistoryFight hf;
    hf.fighter1_id = hit.fighter1_id;
    hf.fighter2_id = hit.fighter2_id;
    hf.winner_id = hit.winner_id;
    if (const auto f1 = Fighter::getById(db, hit.fighter1_id)) {
        hf.fighter1_name = f1->name;
    } else {
        hf.fighter1_name = "id " + std::to_string(hit.fighter1_id);
    }
    if (const auto f2 = Fighter::getById(db, hit.fighter2_id)) {
        hf.fighter2_name = f2->name;
    } else {
        hf.fighter2_name = "id " + std::to_string(hit.fighter2_id);
    }
    hf.fighter1_won = hit.winner_id > 0 && hit.winner_id == hit.fighter1_id;
    hf.fighter2_won = hit.winner_id > 0 && hit.winner_id == hit.fighter2_id;
    hf.event_name = hit.event_name;
    const std::string date = formatEventDate(hit.event_date);
    hf.event_date = date.empty() ? std::nullopt : std::optional<std::string>(date);
    if (show_similarity) {
        hf.similarity = hit.similarity;
        hf.similarity_pct = std::round(100.0 * std::max(0.0, hit.similarity) * 10.0) / 10.0;
    }
    return hf;
}

MatchupHistory buildHistory(sqlite3* db, const SimilarMatchupResults& similar) {
    MatchupHistory history;
    for (const SimilarMatchupHit& hit : similar.prior_meetings) {
        history.prior_meetings.push_back(buildHistoryHit(db, hit, false));
    }
    for (const SimilarMatchupHit& hit : similar.similar_matchups) {
        history.similar_matchups.push_back(buildHistoryHit(db, hit, true));
    }
    return history;
}

}  // namespace

std::vector<FighterSummary> searchFighters(sqlite3* db, const std::string& query, int limit) {
    std::vector<FighterSummary> results;
    const std::string q = query;
    if (q.empty()) {
        return results;
    }
    const std::string pattern = "%" + q + "%";
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT name, weight_class FROM fighters "
        "WHERE lower(name) LIKE lower(?) ORDER BY name LIMIT ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return results;
    }
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FighterSummary fs;
        fs.name = db::columnText(stmt, 0);
        const std::string wc = db::columnText(stmt, 1);
        if (!wc.empty()) {
            fs.weight_class = wc;
        }
        results.push_back(fs);
    }
    sqlite3_finalize(stmt);
    return results;
}

MatchupResponse analyzeMatchup(
    sqlite3* db,
    const std::string& fighter_a_name,
    const std::string& fighter_b_name) {
    const auto fa = Fighter::getByName(db, fighter_a_name);
    if (!fa) {
        throw std::runtime_error("Fighter not found: " + fighter_a_name);
    }
    const auto fb = Fighter::getByName(db, fighter_b_name);
    if (!fb) {
        throw std::runtime_error("Fighter not found: " + fighter_b_name);
    }

    Fighter fighter_a = *fa;
    Fighter fighter_b = *fb;

    const ResumeBreakdown resume_breakdown_a = buildResumeBreakdown(db, fighter_a.id);
    const ResumeBreakdown resume_breakdown_b = buildResumeBreakdown(db, fighter_b.id);
    fighter_a.resume_score = liveResumeScore(resume_breakdown_a);
    fighter_b.resume_score = liveResumeScore(resume_breakdown_b);

    const MomentumBreakdown mom_breakdown_a = buildMomentumBreakdown(db, fighter_a.id);
    const MomentumBreakdown mom_breakdown_b = buildMomentumBreakdown(db, fighter_b.id);
    fighter_a.momentum_score = liveMomentumScore(mom_breakdown_a);
    fighter_b.momentum_score = liveMomentumScore(mom_breakdown_b);

    const Matchup matchup = Matchup::fromDatabase(db, fighter_a, fighter_b);
    const SimilarMatchupResults similar = findSimilarHistoricalMatchups(db, fa->id, fb->id, 0.50);
    const ArchetypeMatchupIndex& index = getArchetypeIndex(db);

    const std::string arch_a = fa->archetype.value_or("");
    const std::string arch_b = fb->archetype.value_or("");
    const auto summaries = index.buildSummaries(
        arch_a, arch_b, fa->weight_class, fb->weight_class);

    std::optional<WinProbabilityResult> win_prob;
    std::optional<std::string> no_prediction_reason;
    if (bothFightersHaveFightHistory(db, fa->id, fb->id)) {
        win_prob = estimateWinProbability(db, matchup, similar, fa->id, fb->id, index);
    } else {
        no_prediction_reason = "Both fighters need at least 1 fight in the database.";
    }

    MatchupResponse response;
    response.tape = buildTape(matchup, win_prob, summaries);
    response.history = buildHistory(db, similar);
    response.no_prediction_reason = no_prediction_reason;
    response.resume_breakdown.fighter_a = resume_breakdown_a;
    response.resume_breakdown.fighter_b = resume_breakdown_b;
    response.momentum_breakdown.fighter_a = mom_breakdown_a;
    response.momentum_breakdown.fighter_b = mom_breakdown_b;
    return response;
}

}  // namespace ufc
