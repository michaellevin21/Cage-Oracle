#include "ufc/ScoreBreakdown.hpp"

#include "ufc/SqliteHelpers.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace ufc {

namespace {

constexpr int kMaxRecentFights = 5;
constexpr int kMinDecisiveFights = 3;
constexpr int kSecondsPerDay = 86400;
constexpr int kRecencyFullWeightDays = 365;
constexpr int kInactivityDays = 730;
constexpr double kRecencyHalfLifeDays = 365.0;
constexpr double kMinRecencyWeight = 0.12;
constexpr double kUnrankedOpponentQuality = 0.35;
constexpr double kTitleFightQualityBonus = 0.12;
constexpr double kFinishWinMultiplier = 1.30;
constexpr double kFinishRateBoostWeight = 0.20;
constexpr double kLossContributionBase = 1.0;
constexpr double kNeutralScore = 50.0;
constexpr double kMaxFightContribution = 1.30;
constexpr int kMaxRankedSlot = 15;
constexpr int kChampionPoints = 16;

bool isFinishMethod(const std::string& method) {
    return method.find("KO") != std::string::npos ||
           method.find("TKO") != std::string::npos ||
           method.find("SUB") != std::string::npos;
}

bool isNonDecisive(const std::string& method) {
    return method == "CNC" || method == "Overturned" || method == "Other" || method == "DRAW";
}

double recencyWeight(int64_t event_date, int64_t now) {
    const double days_since = static_cast<double>(now - event_date) / kSecondsPerDay;
    if (days_since <= kRecencyFullWeightDays) {
        return 1.0;
    }
    const double days_past = days_since - kRecencyFullWeightDays;
    const double weight = std::pow(0.5, days_past / kRecencyHalfLifeDays);
    return std::max(kMinRecencyWeight, weight);
}

std::optional<int> bestOpponentRank(sqlite3* db, int64_t opponent_id) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT MIN(rank) FROM fighter_rankings WHERE fighter_id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, opponent_id);
    std::optional<int> rank;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        rank = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return rank;
}

std::optional<int> bestWeightClassRank(sqlite3* db, int64_t opponent_id) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT MIN(rank) FROM fighter_rankings "
        "WHERE fighter_id = ? AND LOWER(weight_class) NOT LIKE '%pound-for-pound%'";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }
    sqlite3_bind_int64(stmt, 1, opponent_id);
    std::optional<int> rank;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        rank = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return rank;
}

std::string rankLabel(std::optional<int> rank) {
    if (!rank) {
        return "Unranked";
    }
    if (*rank == 0) {
        return "Champion";
    }
    return "#" + std::to_string(*rank);
}

std::pair<double, std::optional<int>> oppositionQuality(
    sqlite3* db, int64_t opponent_id, bool is_title_fight) {
    const auto rank = bestOpponentRank(db, opponent_id);
    double quality = kUnrankedOpponentQuality;
    if (rank) {
        if (*rank <= 0) {
            quality = 1.0;
        } else {
            quality = 1.0 - (*rank / 16.0) * 0.45;
        }
    }
    if (is_title_fight) {
        quality = std::min(1.0, quality + kTitleFightQualityBonus);
    }
    return {quality, rank};
}

double fightContribution(bool won, double quality, double finish_mult) {
    if (won) {
        return quality * finish_mult;
    }
    return -kLossContributionBase * finish_mult;
}

int pointsForRank(int rank) {
    if (rank < 0 || rank > kMaxRankedSlot) {
        return 0;
    }
    return kChampionPoints - rank;
}

std::string formatDate(int64_t ts) {
    if (ts <= 0) {
        return "?";
    }
    std::time_t t = static_cast<std::time_t>(ts);
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

std::string fighterName(sqlite3* db, int64_t id) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT name FROM fighters WHERE id = ?", -1, &stmt, nullptr) != SQLITE_OK) {
        return "id " + std::to_string(id);
    }
    sqlite3_bind_int64(stmt, 1, id);
    std::string name = "id " + std::to_string(id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        name = db::columnText(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return name;
}

}  // namespace

MomentumBreakdown buildMomentumBreakdown(sqlite3* db, int64_t fighter_id) {
    MomentumBreakdown result;
    result.min_decisive_fights = kMinDecisiveFights;
    result.inactivity_days = kInactivityDays;

    const int64_t now = static_cast<int64_t>(std::time(nullptr));

    sqlite3_stmt* stmt = nullptr;
    const char* last_sql =
        "SELECT MAX(e.event_date) FROM fights f "
        "JOIN events e ON e.id = f.event_id "
        "WHERE f.fighter1_id = ? OR f.fighter2_id = ?";
    if (sqlite3_prepare_v2(db, last_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        result.status = "no_fights";
        return result;
    }
    sqlite3_bind_int64(stmt, 1, fighter_id);
    sqlite3_bind_int64(stmt, 2, fighter_id);
    if (sqlite3_step(stmt) != SQLITE_ROW || sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
        sqlite3_finalize(stmt);
        result.status = "no_fights";
        return result;
    }
    const int64_t last_fight = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);

    const double days_since = static_cast<double>(now - last_fight) / kSecondsPerDay;
    if (days_since > kInactivityDays) {
        result.status = "inactive";
        result.score = 0.0;
        result.days_since_last_fight = days_since;
        return result;
    }

    const char* fight_sql =
        "SELECT e.event_date, e.name, "
        "CASE WHEN f.fighter1_id = ? THEN f.fighter2_id ELSE f.fighter1_id END, "
        "f.winner_id, f.result_method, f.is_title_fight "
        "FROM fights f JOIN events e ON e.id = f.event_id "
        "WHERE f.fighter1_id = ? OR f.fighter2_id = ? "
        "ORDER BY e.event_date DESC";
    if (sqlite3_prepare_v2(db, fight_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        result.status = "insufficient_fights";
        return result;
    }
    sqlite3_bind_int64(stmt, 1, fighter_id);
    sqlite3_bind_int64(stmt, 2, fighter_id);
    sqlite3_bind_int64(stmt, 3, fighter_id);

    struct FightRow {
        int64_t event_date;
        std::string event_name;
        int64_t opponent_id;
        int64_t winner_id;
        std::string method;
        bool is_title;
    };
    std::vector<FightRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (static_cast<int>(rows.size()) >= kMaxRecentFights) {
            break;
        }
        const std::string method = db::columnText(stmt, 4);
        const int64_t winner_id = db::columnInt64(stmt, 3);
        if (isNonDecisive(method) || winner_id == 0) {
            continue;
        }
        rows.push_back({
            db::columnInt64(stmt, 0),
            db::columnText(stmt, 1),
            db::columnInt64(stmt, 2),
            winner_id,
            method,
            db::columnBool(stmt, 5),
        });
    }
    sqlite3_finalize(stmt);

    double weighted_sum = 0.0;
    double weight_total = 0.0;
    int wins = 0;
    int finish_wins = 0;

    for (const FightRow& row : rows) {
        const bool won = row.winner_id == fighter_id;
        const bool finish_win = won && isFinishMethod(row.method);
        const bool finish_loss = !won && isFinishMethod(row.method);
        const double finish_mult = (finish_win || finish_loss) ? kFinishWinMultiplier : 1.0;
        const double rec = recencyWeight(row.event_date, now);
        const auto [quality, opp_rank] = oppositionQuality(db, row.opponent_id, row.is_title);
        const double contribution = fightContribution(won, quality, finish_mult);
        const double weighted = contribution * rec;

        MomentumFight mf;
        mf.event_date = formatDate(row.event_date);
        mf.event_name = row.event_name;
        mf.opponent_name = fighterName(db, row.opponent_id);
        mf.result = won ? "W" : "L";
        mf.result_method = row.method;
        mf.opponent_rank_label = rankLabel(opp_rank);
        mf.recency = std::round(rec * 100.0) / 100.0;
        mf.opp_quality = std::round(quality * 100.0) / 100.0;
        mf.finish_mult = std::round(finish_mult * 100.0) / 100.0;
        mf.contribution = std::round(contribution * 1000.0) / 1000.0;
        mf.weighted_contribution = std::round(weighted * 1000.0) / 1000.0;
        result.fights.push_back(mf);

        weighted_sum += weighted;
        weight_total += rec;
        if (won) {
            wins += 1;
            if (finish_win) {
                finish_wins += 1;
            }
        }
    }

    if (static_cast<int>(result.fights.size()) < kMinDecisiveFights) {
        result.status = "insufficient_fights";
        result.days_since_last_fight = days_since;
        return result;
    }

    const double weighted_average = weight_total > 0 ? weighted_sum / weight_total : 0.0;
    const double finish_rate = wins > 0 ? static_cast<double>(finish_wins) / wins : 0.0;
    const double finish_boost = 1.0 + kFinishRateBoostWeight * finish_rate;
    const double adjusted = weighted_average * finish_boost;
    double score = kNeutralScore + (adjusted / kMaxFightContribution) * kNeutralScore;
    score = std::max(0.0, std::min(100.0, score));

    result.status = "ok";
    result.score = std::round(score * 100.0) / 100.0;
    result.days_since_last_fight = days_since;
    result.weighted_average = std::round(weighted_average * 1000.0) / 1000.0;
    result.finish_rate = std::round(finish_rate * 100.0) / 100.0;
    result.finish_boost = std::round(finish_boost * 1000.0) / 1000.0;
    result.adjusted = std::round(adjusted * 1000.0) / 1000.0;
    result.neutral_score = kNeutralScore;
    result.max_fight_contribution = kMaxFightContribution;
    return result;
}

ResumeBreakdown buildResumeBreakdown(sqlite3* db, int64_t fighter_id) {
    ResumeBreakdown result;
    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT e.event_date, e.name, "
        "CASE WHEN f.fighter1_id = ? THEN f.fighter2_id ELSE f.fighter1_id END, "
        "f.result_method "
        "FROM fights f JOIN events e ON e.id = f.event_id "
        "WHERE (f.fighter1_id = ? OR f.fighter2_id = ?) AND f.winner_id = ? "
        "ORDER BY e.event_date ASC";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    sqlite3_bind_int64(stmt, 1, fighter_id);
    sqlite3_bind_int64(stmt, 2, fighter_id);
    sqlite3_bind_int64(stmt, 3, fighter_id);
    sqlite3_bind_int64(stmt, 4, fighter_id);

    int running = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const std::string method = db::columnText(stmt, 3);
        if (isNonDecisive(method)) {
            result.skipped_non_decisive += 1;
            continue;
        }
        const int64_t opponent_id = db::columnInt64(stmt, 2);
        const auto opp_rank = bestWeightClassRank(db, opponent_id);
        const int pts = opp_rank ? pointsForRank(*opp_rank) : 0;
        if (pts == 0) {
            result.unranked_win_count += 1;
            continue;
        }
        running += pts;
        ResumeWin win;
        win.event_date = formatDate(db::columnInt64(stmt, 0));
        win.event_name = db::columnText(stmt, 1);
        win.opponent_name = fighterName(db, opponent_id);
        win.result_method = method;
        win.opponent_rank_label = rankLabel(opp_rank);
        win.points = pts;
        win.running_total = running;
        result.ranked_wins.push_back(win);
    }
    sqlite3_finalize(stmt);

    std::sort(result.ranked_wins.begin(), result.ranked_wins.end(),
        [](const ResumeWin& a, const ResumeWin& b) {
            const int ra = a.opponent_rank_label == "Champion" ? 0 :
                (a.opponent_rank_label.size() > 1 && a.opponent_rank_label[0] == '#'
                    ? std::stoi(a.opponent_rank_label.substr(1)) : 999);
            const int rb = b.opponent_rank_label == "Champion" ? 0 :
                (b.opponent_rank_label.size() > 1 && b.opponent_rank_label[0] == '#'
                    ? std::stoi(b.opponent_rank_label.substr(1)) : 999);
            if (ra != rb) {
                return ra < rb;
            }
            return a.event_date > b.event_date;
        });

    result.score = running;
    return result;
}

}  // namespace ufc
