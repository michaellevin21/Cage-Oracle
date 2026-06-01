#include "ufc/FighterMomentum.hpp"

#include "ufc/SqliteHelpers.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace ufc {

namespace {

constexpr int kMaxRecentFights = 5;
constexpr int kMinDecisiveFights = 3;
constexpr int64_t kSecondsPerDay = 86400;

constexpr int64_t kRecencyFullWeightDays = 365;
constexpr int64_t kInactivityDays = 730;  // 2 years; no bout since then -> score 0
constexpr double kRecencyHalfLifeDays = 365.0;
constexpr double kMinRecencyWeight = 0.12;

constexpr double kUnrankedOpponentQuality = 0.35;
constexpr double kTitleFightQualityBonus = 0.12;
constexpr double kFinishWinMultiplier = 1.30;
constexpr double kFinishRateBoostWeight = 0.20;

constexpr double kNeutralScore = 50.0;
constexpr double kMaxFightContribution = 1.30;

struct MomentumFight {
    int64_t event_date = 0;
    int64_t opponent_id = 0;
    bool won = false;
    bool lost = false;
    bool is_finish_win = false;
    bool is_title_fight = false;
};

bool isFinishMethod(const std::string& method) {
    return method.find("KO") != std::string::npos || method.find("TKO") != std::string::npos ||
           method.find("SUB") != std::string::npos;
}

bool isNonDecisiveMethod(const std::string& method) {
    return method == "CNC" || method == "Overturned" || method == "Other" || method == "DRAW";
}

/// Recency is measured from today, not from the fighter's last bout. Full weight for
/// the first year after a fight; decay begins after that even for the most recent fight.
double recencyWeight(int64_t event_date, int64_t now) {
    const double days_since_fight =
        static_cast<double>(now - event_date) / kSecondsPerDay;
    if (days_since_fight <= static_cast<double>(kRecencyFullWeightDays)) {
        return 1.0;
    }
    const double days_past_grace = days_since_fight - kRecencyFullWeightDays;
    const double weight = std::pow(0.5, days_past_grace / kRecencyHalfLifeDays);
    return std::max(kMinRecencyWeight, weight);
}

std::optional<int64_t> bestOpponentRank(sqlite3* db, int64_t opponent_id) {
    const char* sql =
        "SELECT MIN(rank) FROM fighter_rankings WHERE fighter_id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, opponent_id);

    std::optional<int64_t> rank;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        rank = db::columnInt64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return rank;
}

double oppositionQuality(sqlite3* db, int64_t opponent_id, bool is_title_fight) {
    double quality = kUnrankedOpponentQuality;

    if (const std::optional<int64_t> rank = bestOpponentRank(db, opponent_id)) {
        if (*rank <= 0) {
            quality = 1.0;
        } else {
            quality = 1.0 - (static_cast<double>(*rank) / 16.0) * 0.45;
        }
    }

    if (is_title_fight) {
        quality = std::min(1.0, quality + kTitleFightQualityBonus);
    }

    return quality;
}

double finishMultiplier(bool won, bool is_finish_win) {
    if (won && is_finish_win) {
        return kFinishWinMultiplier;
    }
    return 1.0;
}

std::vector<MomentumFight> loadRecentDecisiveFights(sqlite3* db, int64_t fighter_id) {
    const char* sql =
        "SELECT e.event_date, "
        "       CASE WHEN f.fighter1_id = ?1 THEN f.fighter2_id ELSE f.fighter1_id END, "
        "       f.winner_id, f.result_method, f.is_title_fight "
        "FROM fights AS f "
        "INNER JOIN events AS e ON e.id = f.event_id "
        "WHERE f.fighter1_id = ?1 OR f.fighter2_id = ?1 "
        "ORDER BY e.event_date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    sqlite3_bind_int64(stmt, 1, fighter_id);

    std::vector<MomentumFight> fights;
    fights.reserve(kMaxRecentFights);

    while (sqlite3_step(stmt) == SQLITE_ROW && static_cast<int>(fights.size()) < kMaxRecentFights) {
        const int64_t event_date = db::columnInt64(stmt, 0);
        const int64_t opponent_id = db::columnInt64(stmt, 1);
        const int64_t winner_id = db::columnInt64(stmt, 2);
        const std::string result_method = db::columnText(stmt, 3);
        const bool is_title_fight = db::columnBool(stmt, 4);

        if (isNonDecisiveMethod(result_method) || winner_id == 0) {
            continue;
        }

        MomentumFight row;
        row.event_date = event_date;
        row.opponent_id = opponent_id;
        row.is_title_fight = is_title_fight;
        row.won = winner_id == fighter_id;
        row.lost = !row.won;
        row.is_finish_win = row.won && isFinishMethod(result_method);
        fights.push_back(row);
    }

    sqlite3_finalize(stmt);
    return fights;
}

std::optional<int64_t> mostRecentFightDate(sqlite3* db, int64_t fighter_id) {
    const char* sql =
        "SELECT MAX(e.event_date) "
        "FROM fights AS f "
        "INNER JOIN events AS e ON e.id = f.event_id "
        "WHERE f.fighter1_id = ?1 OR f.fighter2_id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, fighter_id);

    std::optional<int64_t> event_date;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        event_date = db::columnInt64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return event_date;
}

}  // namespace

std::optional<double> computeMomentumScore(sqlite3* db, int64_t fighter_id) {
    const int64_t now = static_cast<int64_t>(std::time(nullptr));

    if (const std::optional<int64_t> last_fight_date = mostRecentFightDate(db, fighter_id)) {
        const double days_since_last_fight =
            static_cast<double>(now - *last_fight_date) / kSecondsPerDay;
        if (days_since_last_fight > static_cast<double>(kInactivityDays)) {
            return 0.0;
        }
    } else {
        return std::nullopt;
    }

    const std::vector<MomentumFight> fights = loadRecentDecisiveFights(db, fighter_id);
    if (static_cast<int>(fights.size()) < kMinDecisiveFights) {
        return std::nullopt;
    }

    double weighted_sum = 0.0;
    double weight_total = 0.0;
    int wins = 0;
    int finish_wins = 0;

    for (const MomentumFight& fight : fights) {
        const double recency = recencyWeight(fight.event_date, now);
        const double quality = oppositionQuality(db, fight.opponent_id, fight.is_title_fight);
        const double outcome = fight.won ? 1.0 : -1.0;
        const double finish = finishMultiplier(fight.won, fight.is_finish_win);
        const double contribution = outcome * quality * finish;

        weighted_sum += contribution * recency;
        weight_total += recency;

        if (fight.won) {
            ++wins;
            if (fight.is_finish_win) {
                ++finish_wins;
            }
        }
    }

    if (weight_total <= 0.0) {
        return std::nullopt;
    }

    const double weighted_average = weighted_sum / weight_total;
    const double finish_rate = wins > 0 ? static_cast<double>(finish_wins) / wins : 0.0;
    const double finish_boost = 1.0 + kFinishRateBoostWeight * finish_rate;
    const double adjusted = weighted_average * finish_boost;

    double score = kNeutralScore + (adjusted / kMaxFightContribution) * kNeutralScore;
    score = std::clamp(score, 0.0, 100.0);
    return score;
}

}  // namespace ufc
