#include "ufc/Fight.hpp"

#include "ufc/SqliteHelpers.hpp"

#include <sqlite3.h>

namespace ufc {

namespace {

constexpr const char* kFightColumns =
    "id, ufc_fight_id, fighter1_id, fighter2_id, event_id, winner_id, "
    "result_method, result_method_detail, result_round, result_time_seconds, "
    "weight_class, is_title_fight";

}  // namespace

std::vector<Fight> Fight::fetchAll(sqlite3_stmt* stmt) {
    std::vector<Fight> fights;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        fights.push_back(fromStatement(stmt));
    }
    return fights;
}

Fight Fight::fromStatement(sqlite3_stmt* stmt) {
    using db::columnBool;
    using db::columnDouble;
    using db::columnInt64;
    using db::columnText;
    using db::columnTextOptional;

    Fight f;
    int col = 0;
    f.id = columnInt64(stmt, col++);
    f.ufc_fight_id = columnText(stmt, col++);
    f.fighter1_id = columnInt64(stmt, col++);
    f.fighter2_id = columnInt64(stmt, col++);
    f.event_id = columnInt64(stmt, col++);
    f.winner_id = columnInt64(stmt, col++);
    f.result_method = columnText(stmt, col++);
    f.result_method_detail = columnTextOptional(stmt, col++);
    f.result_round = columnInt64(stmt, col++);
    f.result_time_seconds = columnDouble(stmt, col++);
    f.weight_class = columnText(stmt, col++);
    f.is_title_fight = columnBool(stmt, col++);
    return f;
}

std::optional<Fight> Fight::getById(sqlite3* db, int64_t id) {
    const std::string sql =
        std::string("SELECT ") + kFightColumns + " FROM fights WHERE id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id);

    std::optional<Fight> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = fromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<Fight> Fight::getByUfcFightId(sqlite3* db, const std::string& ufc_fight_id) {
    const std::string sql =
        std::string("SELECT ") + kFightColumns + " FROM fights WHERE ufc_fight_id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, ufc_fight_id.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Fight> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = fromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<Fight> Fight::getByFighters(sqlite3* db, int64_t fighter1_id, int64_t fighter2_id) {
    const std::string sql =
        "SELECT f.id, f.ufc_fight_id, f.fighter1_id, f.fighter2_id, f.event_id, f.winner_id, "
        "f.result_method, f.result_method_detail, f.result_round, f.result_time_seconds, "
        "f.weight_class, f.is_title_fight "
        "FROM fights AS f "
        "INNER JOIN events AS e ON e.id = f.event_id "
        "WHERE (f.fighter1_id = ?1 AND f.fighter2_id = ?2) "
        "   OR (f.fighter1_id = ?2 AND f.fighter2_id = ?1) "
        "ORDER BY e.event_date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    sqlite3_bind_int64(stmt, 1, fighter1_id);
    sqlite3_bind_int64(stmt, 2, fighter2_id);
    std::vector<Fight> fights = fetchAll(stmt);
    sqlite3_finalize(stmt);
    return fights;
}

std::optional<Fight> Fight::getByFightersEvent(
    sqlite3* db, int64_t fighter1_id, int64_t fighter2_id, int64_t event_id) {
    const std::string sql =
        std::string("SELECT ") + kFightColumns +
        " FROM fights "
        "WHERE event_id = ?3 "
        "  AND ((fighter1_id = ?1 AND fighter2_id = ?2) "
        "    OR (fighter1_id = ?2 AND fighter2_id = ?1)) "
        "LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, fighter1_id);
    sqlite3_bind_int64(stmt, 2, fighter2_id);
    sqlite3_bind_int64(stmt, 3, event_id);

    std::optional<Fight> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = fromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<Fight> Fight::listForFighter(sqlite3* db, int64_t fighter_id) {
    const std::string sql =
        "SELECT f.id, f.ufc_fight_id, f.fighter1_id, f.fighter2_id, f.event_id, f.winner_id, "
        "f.result_method, f.result_method_detail, f.result_round, f.result_time_seconds, "
        "f.weight_class, f.is_title_fight "
        "FROM fights AS f "
        "INNER JOIN events AS e ON e.id = f.event_id "
        "WHERE f.fighter1_id = ?1 OR f.fighter2_id = ?1 "
        "ORDER BY e.event_date DESC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_int64(stmt, 1, fighter_id);
    std::vector<Fight> fights = fetchAll(stmt);
    sqlite3_finalize(stmt);
    return fights;
}

std::vector<Fight> Fight::listForEvent(sqlite3* db, int64_t event_id) {
    const std::string sql =
        std::string("SELECT ") + kFightColumns + " FROM fights WHERE event_id = ?1 ORDER BY id";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_int64(stmt, 1, event_id);
    std::vector<Fight> fights = fetchAll(stmt);
    sqlite3_finalize(stmt);
    return fights;
}

}  // namespace ufc
