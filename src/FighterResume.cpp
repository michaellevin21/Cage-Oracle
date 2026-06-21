#include "ufc/FighterResume.hpp"

#include "ufc/SqliteHelpers.hpp"

#include <string>
#include <vector>

#include <sqlite3.h>

namespace ufc {

namespace {

constexpr int kMaxRankedSlot = 15;
constexpr int kChampionPoints = 16;

bool isNonDecisiveMethod(const std::string& method) {
    return method == "CNC" || method == "Overturned" || method == "Other" || method == "DRAW";
}

std::optional<int64_t> bestWeightClassRank(sqlite3* db, int64_t opponent_id) {
    const char* sql =
        "SELECT MIN(rank) FROM fighter_rankings "
        "WHERE fighter_id = ?1 "
        "AND LOWER(weight_class) NOT LIKE '%pound-for-pound%'";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, opponent_id);

    std::optional<int64_t> best_rank;
    if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        best_rank = db::columnInt64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return best_rank;
}

int pointsForRank(int64_t rank) {
    if (rank < 0 || rank > kMaxRankedSlot) {
        return 0;
    }
    return kChampionPoints - static_cast<int>(rank);
}

std::vector<int64_t> loadDefeatedOpponentIds(sqlite3* db, int64_t fighter_id) {
    const char* sql =
        "SELECT CASE WHEN f.fighter1_id = ?1 THEN f.fighter2_id ELSE f.fighter1_id END, "
        "       f.result_method "
        "FROM fights AS f "
        "WHERE (f.fighter1_id = ?1 OR f.fighter2_id = ?1) "
        "  AND f.winner_id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    sqlite3_bind_int64(stmt, 1, fighter_id);

    std::vector<int64_t> opponents;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const int64_t opponent_id = db::columnInt64(stmt, 0);
        const std::string result_method = db::columnText(stmt, 1);
        if (isNonDecisiveMethod(result_method)) {
            continue;
        }
        opponents.push_back(opponent_id);
    }

    sqlite3_finalize(stmt);
    return opponents;
}

}  // namespace

double computeResumeScore(sqlite3* db, int64_t fighter_id) {
    const std::vector<int64_t> opponents = loadDefeatedOpponentIds(db, fighter_id);
    double total = 0.0;

    for (const int64_t opponent_id : opponents) {
        const std::optional<int64_t> rank = bestWeightClassRank(db, opponent_id);
        if (!rank) {
            continue;
        }
        total += static_cast<double>(pointsForRank(*rank));
    }

    return total;
}

}  // namespace ufc
