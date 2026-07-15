#include "ufc/WinProbability.hpp"

#include "ufc/RoundStats.hpp"
#include "ufc/SqliteHelpers.hpp"

#include <cmath>
#include <ctime>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace ufc {

namespace {

constexpr double kWeightCareer = 0.28;
constexpr double kWeightPhysical = 0.10;
constexpr double kWeightStyle = 0.20;
constexpr double kWeightResume = 0.06;
constexpr double kWeightMomentum = 0.18;
constexpr double kWeightSimilar = 0.14;
constexpr double kWeightH2HOne = 0.17;
constexpr double kWeightH2HMulti = 0.25;
constexpr int kH2HSecondsPerDay = 86400;
constexpr int kH2HRecencyFullDays = 365;
constexpr double kH2HHalfLifeDays = 365.0;
constexpr double kH2HMinRecency = 0.12;
constexpr double kSimilarConfidenceRef = 0.75;
constexpr int kDefaultMinSample = 5;

const std::unordered_set<std::string> kProfileMetrics = {
    "height_cm", "reach_cm", "momentum_score", "resume_score",
    "stance", "weight_class", "archetype", "age",
};

struct Component {
    double p_fighter_a = 0.5;
    double blend_weight = 0.0;
    bool available = false;
};

double sigmoid(double x) {
    if (x >= 0) {
        const double z = std::exp(-x);
        return 1.0 / (1.0 + z);
    }
    const double z = std::exp(x);
    return z / (1.0 + z);
}

double clampProb(double p) {
    return std::min(0.97, std::max(0.03, p));
}

double relativeEdge(double a, double b, bool higher_is_better) {
    const double scale = std::max(std::max(std::abs(a), std::abs(b)), 1e-9);
    const double delta = (a - b) / scale;
    return higher_is_better ? delta : -delta;
}

std::optional<std::pair<double, double>> comparisonValues(
    const Matchup& matchup, const std::string& metric) {
    for (const StatComparison& row : matchup.comparisons()) {
        if (row.metric != metric) {
            continue;
        }
        if (row.fighter_a && row.fighter_b) {
            return std::make_pair(*row.fighter_a, *row.fighter_b);
        }
    }
    return std::nullopt;
}

Component physicalComponent(const Matchup& matchup) {
    Component c;
    double score = 0.0;
    double total_weight = 0.0;
    struct Spec { double weight; double scale; };
    const std::unordered_map<std::string, Spec> measures = {
        {"height_cm", {2.5, 10.0}},
        {"reach_cm", {3.5, 10.0}},
    };
    for (const auto& [metric, spec] : measures) {
        const auto vals = comparisonValues(matchup, metric);
        if (!vals) {
            continue;
        }
        const double edge = (vals->first - vals->second) / spec.scale;
        score += spec.weight * edge;
        total_weight += spec.weight;
    }
    if (total_weight <= 0) {
        return c;
    }
    c.p_fighter_a = clampProb(sigmoid(3.0 * score / total_weight));
    c.blend_weight = kWeightPhysical;
    c.available = true;
    return c;
}

Component careerComponent(const Matchup& matchup) {
    Component c;
    struct Spec { bool higher; double weight; };
    const std::unordered_map<std::string, Spec> metrics = {
        {"sig_strikes_landed_per_round", {true, 1.0}},
        {"sig_strike_accuracy", {true, 1.1}},
        {"striking_defense", {true, 1.1}},
        {"strikes_taken_per_round", {false, 0.9}},
        {"total_strike_accuracy", {true, 0.8}},
        {"takedown_accuracy", {true, 1.0}},
        {"takedown_defense", {true, 1.0}},
        {"takedowns_landed_per_round", {true, 1.0}},
        {"sub_attempts_per_round", {true, 0.9}},
        {"reversals_per_round", {true, 0.7}},
        {"knockdowns_per_round", {true, 1.0}},
        {"control_time_seconds_per_round", {true, 1.0}},
        {"head_strikes_landed_per_round", {true, 0.6}},
        {"body_strikes_landed_per_round", {true, 0.5}},
        {"leg_strikes_landed_per_round", {true, 0.5}},
        {"distance_strikes_landed_per_round", {true, 0.6}},
        {"clinch_strikes_landed_per_round", {true, 0.6}},
        {"ground_strikes_landed_per_round", {true, 0.7}},
    };

    double score = 0.0;
    double total_weight = 0.0;
    int edges = 0;
    for (const StatComparison& row : matchup.comparisons()) {
        if (kProfileMetrics.count(row.metric) || !metrics.count(row.metric)) {
            continue;
        }
        if (!row.fighter_a || !row.fighter_b) {
            continue;
        }
        const Spec& spec = metrics.at(row.metric);
        const double edge = relativeEdge(*row.fighter_a, *row.fighter_b, spec.higher);
        score += spec.weight * edge;
        total_weight += spec.weight;
        edges += 1;
    }
    if (total_weight <= 0 || edges == 0) {
        return c;
    }
    c.p_fighter_a = clampProb(sigmoid(3.0 * score / total_weight));
    c.blend_weight = kWeightCareer;
    c.available = true;
    return c;
}

std::optional<std::string> fighterArchetype(const Fighter& f) {
    if (f.archetype && !f.archetype->empty()) {
        return f.archetype;
    }
    return std::nullopt;
}

Component styleComponent(
    const Matchup& matchup, const ArchetypeMatchupIndex& index, int min_sample) {
    Component c;
    const auto arch_a = fighterArchetype(matchup.fighter_a);
    const auto arch_b = fighterArchetype(matchup.fighter_b);
    if (!arch_a || !arch_b) {
        return c;
    }
    if (*arch_a == *arch_b) {
        return c;
    }
    const auto wcs = resolveMatchupWeightClasses(
        matchup.fighter_a.weight_class, matchup.fighter_b.weight_class,
        index.rankedWeightClasses());
    if (wcs.empty()) {
        return c;
    }
    std::optional<std::tuple<double, int, std::string>> best;
    for (const std::string& wc : wcs) {
        const auto rate = index.winRate(*arch_a, *arch_b, wc);
        if (!rate || rate->decisive < min_sample) {
            continue;
        }
        if (!best || rate->decisive > std::get<1>(*best)) {
            best = std::make_tuple(rate->win_pct / 100.0, rate->decisive, wc);
        }
    }
    if (!best) {
        return c;
    }
    const double p_a = std::get<0>(*best);
    const int sample = std::get<1>(*best);
    const double weight_scale = std::min(sample / 20.0, 1.0);
    c.p_fighter_a = clampProb(p_a);
    c.blend_weight = kWeightStyle * weight_scale;
    c.available = true;
    return c;
}

Component momentumComponent(const Matchup& matchup) {
    Component c;
    std::optional<double> mom_a = matchup.fighter_a.momentum_score;
    std::optional<double> mom_b = matchup.fighter_b.momentum_score;
    if (!mom_a || !mom_b) {
        const auto vals = comparisonValues(matchup, "momentum_score");
        if (vals) {
            if (!mom_a) mom_a = vals->first;
            if (!mom_b) mom_b = vals->second;
        }
    }
    if (!mom_a || !mom_b) {
        return c;
    }
    const double diff = (*mom_a - *mom_b) / 20.0;
    c.p_fighter_a = clampProb(sigmoid(diff));
    c.blend_weight = kWeightMomentum;
    c.available = true;
    return c;
}

Component resumeComponent(const Matchup& matchup) {
    Component c;
    std::optional<double> res_a = matchup.fighter_a.resume_score;
    std::optional<double> res_b = matchup.fighter_b.resume_score;
    if (!res_a || !res_b) {
        const auto vals = comparisonValues(matchup, "resume_score");
        if (vals) {
            if (!res_a) res_a = vals->first;
            if (!res_b) res_b = vals->second;
        }
    }
    if (!res_a || !res_b) {
        return c;
    }
    if (*res_a <= 0 && *res_b <= 0) {
        return c;
    }
    const double diff = (*res_a - *res_b) / 25.0;
    c.p_fighter_a = clampProb(sigmoid(diff));
    c.blend_weight = kWeightResume;
    c.available = true;
    return c;
}

std::optional<bool> winnerIsFighterA(
    const SimilarMatchupHit& hit, int64_t fighter_a_id, int64_t fighter_b_id) {
    if (hit.winner_id <= 0) {
        return std::nullopt;
    }
    if (hit.winner_id == fighter_a_id) {
        return true;
    }
    if (hit.winner_id == fighter_b_id) {
        return false;
    }
    return std::nullopt;
}

std::optional<bool> similarHitFavorsA(
    const SimilarMatchupHit& hit, int64_t fighter_a_id, int64_t fighter_b_id) {
    const bool a_in = hit.fighter1_id == fighter_a_id || hit.fighter2_id == fighter_a_id;
    const bool b_in = hit.fighter1_id == fighter_b_id || hit.fighter2_id == fighter_b_id;
    if (a_in && b_in) {
        return winnerIsFighterA(hit, fighter_a_id, fighter_b_id);
    }
    if (a_in) {
        return hit.winner_id == fighter_a_id;
    }
    if (b_in) {
        return hit.winner_id != fighter_b_id;
    }
    return std::nullopt;
}

Component similarComponent(
    const SimilarMatchupResults& similar, int64_t fighter_a_id, int64_t fighter_b_id) {
    Component c;
    double weighted_a = 0.0;
    double weighted_total = 0.0;
    int used = 0;
    for (const SimilarMatchupHit& hit : similar.similar_matchups) {
        if (hit.similarity <= 0) {
            continue;
        }
        const auto favor = similarHitFavorsA(hit, fighter_a_id, fighter_b_id);
        if (!favor) {
            continue;
        }
        weighted_a += *favor ? hit.similarity : 0.0;
        weighted_total += hit.similarity;
        used += 1;
    }
    if (weighted_total <= 0 || used == 0) {
        return c;
    }
    const double raw_p = weighted_a / weighted_total;
    const double avg_sim = weighted_total / used;
    const double confidence = kSimilarConfidenceRef > 0
        ? std::min(1.0, std::max(0.0, avg_sim / kSimilarConfidenceRef))
        : 1.0;
    c.p_fighter_a = clampProb(0.5 + (raw_p - 0.5) * confidence);
    c.blend_weight = kWeightSimilar * confidence;
    c.available = true;
    return c;
}

double h2hRecencyWeight(int64_t event_date, int64_t now) {
    const double days_since = static_cast<double>(now - event_date) / kH2HSecondsPerDay;
    if (days_since <= kH2HRecencyFullDays) {
        return 1.0;
    }
    const double days_past = days_since - kH2HRecencyFullDays;
    const double weight = std::pow(0.5, days_past / kH2HHalfLifeDays);
    return std::max(kH2HMinRecency, weight);
}

double h2hMeetingRecency(const SimilarMatchupHit& hit, int64_t now) {
    if (hit.event_date <= 0) {
        return 1.0;
    }
    return h2hRecencyWeight(hit.event_date, now);
}

std::unordered_map<std::string, double> aggregateRoundStats(
    const std::vector<RoundStats>& rows, int64_t fighter_id) {
    const std::vector<std::string> keys = {
        "sig_strikes_landed", "total_strikes_landed", "takedowns_landed",
        "sub_attempts", "knockdowns", "control_time_seconds",
    };
    std::unordered_map<std::string, double> totals;
    for (const auto& k : keys) {
        totals[k] = 0.0;
    }
    int rounds = 0;
    for (const RoundStats& row : rows) {
        if (row.fighter_id != fighter_id) {
            continue;
        }
        rounds += 1;
        totals["sig_strikes_landed"] += row.sig_strikes_landed;
        totals["total_strikes_landed"] += row.total_strikes_landed;
        totals["takedowns_landed"] += row.takedowns_landed;
        totals["sub_attempts"] += row.sub_attempts;
        totals["knockdowns"] += row.knockdowns;
        totals["control_time_seconds"] += row.control_time_seconds;
    }
    if (rounds == 0) {
        return {};
    }
    for (auto& [k, v] : totals) {
        v /= rounds;
    }
    return totals;
}

std::optional<double> h2hStatsEdge(
    const std::unordered_map<std::string, double>& a,
    const std::unordered_map<std::string, double>& b) {
    const std::unordered_map<std::string, bool> metrics = {
        {"sig_strikes_landed", true}, {"total_strikes_landed", true},
        {"takedowns_landed", true}, {"sub_attempts", true},
        {"knockdowns", true}, {"control_time_seconds", true},
    };
    double score = 0.0;
    double weight = 0.0;
    for (const auto& [metric, higher] : metrics) {
        const auto ia = a.find(metric);
        const auto ib = b.find(metric);
        if (ia == a.end() || ib == b.end()) {
            continue;
        }
        score += relativeEdge(ia->second, ib->second, higher);
        weight += 1.0;
    }
    if (weight <= 0) {
        return std::nullopt;
    }
    return score / weight;
}

Component h2hComponent(
    sqlite3* db,
    const SimilarMatchupResults& similar,
    int64_t fighter_a_id,
    int64_t fighter_b_id) {
    Component c;
    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    const auto& meetings = similar.prior_meetings;
    if (meetings.empty()) {
        return c;
    }

    int wins_a = 0;
    int wins_b = 0;
    double weighted_wins_a = 0.0;
    double record_weight = 0.0;
    for (const SimilarMatchupHit& hit : meetings) {
        const double recency = h2hMeetingRecency(hit, now);
        const auto outcome = winnerIsFighterA(hit, fighter_a_id, fighter_b_id);
        if (outcome == true) {
            wins_a += 1;
            weighted_wins_a += recency;
            record_weight += recency;
        } else if (outcome == false) {
            wins_b += 1;
            record_weight += recency;
        }
    }

    std::optional<double> record_p;
    const int decisive = wins_a + wins_b;
    if (record_weight > 0) {
        record_p = weighted_wins_a / record_weight;
    }

    std::unordered_map<std::string, double> stats_a_totals, stats_b_totals;
    const std::vector<std::string> stat_keys = {
        "sig_strikes_landed", "total_strikes_landed", "takedowns_landed",
        "sub_attempts", "knockdowns", "control_time_seconds",
    };
    for (const auto& k : stat_keys) {
        stats_a_totals[k] = 0.0;
        stats_b_totals[k] = 0.0;
    }
    double stats_weight = 0.0;
    int stat_fights = 0;

    for (const SimilarMatchupHit& hit : meetings) {
        if (hit.fight_id <= 0) {
            continue;
        }
        const double recency = h2hMeetingRecency(hit, now);
        const auto rows = RoundStats::listForFight(db, hit.fight_id);
        const auto per_a = aggregateRoundStats(rows, fighter_a_id);
        const auto per_b = aggregateRoundStats(rows, fighter_b_id);
        if (per_a.empty() || per_b.empty()) {
            continue;
        }
        for (const auto& k : stat_keys) {
            stats_a_totals[k] += per_a.at(k) * recency;
            stats_b_totals[k] += per_b.at(k) * recency;
        }
        stats_weight += recency;
        stat_fights += 1;
    }

    std::optional<double> stats_p;
    if (stats_weight > 0) {
        std::unordered_map<std::string, double> avg_a, avg_b;
        for (const auto& k : stat_keys) {
            avg_a[k] = stats_a_totals[k] / stats_weight;
            avg_b[k] = stats_b_totals[k] / stats_weight;
        }
        if (const auto edge = h2hStatsEdge(avg_a, avg_b)) {
            stats_p = clampProb(sigmoid(2.5 * *edge));
        }
    }

    if (!record_p && !stats_p) {
        return c;
    }

    double recency_scale = 1.0;
    if (!meetings.empty()) {
        double sum = 0.0;
        for (const SimilarMatchupHit& hit : meetings) {
            sum += h2hMeetingRecency(hit, now);
        }
        recency_scale = sum / meetings.size();
    }

    double p_a;
    if (record_p && stats_p) {
        p_a = 0.6 * *record_p + 0.4 * *stats_p;
    } else if (record_p) {
        p_a = *record_p;
    } else {
        p_a = stats_p.value_or(0.5);
    }

    const int evidence = decisive > 0 ? decisive : stat_fights;
    double blend = 0.0;
    if (evidence == 1) {
        blend = kWeightH2HOne;
    } else if (evidence > 1) {
        blend = kWeightH2HMulti;
    }

    c.p_fighter_a = clampProb(p_a);
    c.blend_weight = blend * recency_scale;
    c.available = true;
    return c;
}

double blendComponents(const std::vector<Component>& components) {
    double total_weight = 0.0;
    double weighted = 0.0;
    for (const Component& c : components) {
        if (c.available && c.blend_weight > 0) {
            total_weight += c.blend_weight;
            weighted += c.p_fighter_a * c.blend_weight;
        }
    }
    if (total_weight <= 0) {
        return 0.5;
    }
    return clampProb(weighted / total_weight);
}

}  // namespace

bool bothFightersHaveFightHistory(sqlite3* db, int64_t fighter_a_id, int64_t fighter_b_id) {
    auto count = [&](int64_t id) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db,
                "SELECT COUNT(*) FROM fights WHERE fighter1_id = ? OR fighter2_id = ?",
                -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }
        sqlite3_bind_int64(stmt, 1, id);
        sqlite3_bind_int64(stmt, 2, id);
        int n = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            n = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return n;
    };
    return count(fighter_a_id) >= 1 && count(fighter_b_id) >= 1;
}

WinProbabilityResult estimateWinProbability(
    sqlite3* db,
    const Matchup& matchup,
    const SimilarMatchupResults& similar,
    int64_t fighter_a_id,
    int64_t fighter_b_id,
    const ArchetypeMatchupIndex& archetype_index) {
    const std::vector<Component> components = {
        careerComponent(matchup),
        physicalComponent(matchup),
        styleComponent(matchup, archetype_index, kDefaultMinSample),
        resumeComponent(matchup),
        momentumComponent(matchup),
        similarComponent(similar, fighter_a_id, fighter_b_id),
        h2hComponent(db, similar, fighter_a_id, fighter_b_id),
    };
    WinProbabilityResult result;
    result.p_fighter_a = blendComponents(components);
    result.p_fighter_b = 1.0 - result.p_fighter_a;
    return result;
}

}  // namespace ufc
