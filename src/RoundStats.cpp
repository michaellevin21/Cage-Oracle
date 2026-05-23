#include "ufc/RoundStats.hpp"

#include "ufc/SqliteHelpers.hpp"

#include <sqlite3.h>

namespace ufc {

namespace {

constexpr const char* kRoundStatsColumns =
    "id, fight_id, fighter_id, round_number, "
    "sig_strikes_landed, sig_strikes_attempted, "
    "total_strikes_landed, total_strikes_attempted, "
    "takedowns_landed, takedowns_attempted, sub_attempts, reversals, knockdowns, "
    "control_time_seconds, head_strikes_landed, body_strikes_landed, "
    "leg_strikes_landed, distance_strikes_landed, clinch_strikes_landed, "
    "ground_strikes_landed";

}  // namespace

std::vector<RoundStats> RoundStats::fetchAll(sqlite3_stmt* stmt) {
    std::vector<RoundStats> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        rows.push_back(fromStatement(stmt));
    }
    return rows;
}

RoundStats RoundStats::fromStatement(sqlite3_stmt* stmt) {
    using db::columnDouble;
    using db::columnInt;
    using db::columnInt64;

    RoundStats r;
    int col = 0;
    r.id = columnInt64(stmt, col++);
    r.fight_id = columnInt64(stmt, col++);
    r.fighter_id = columnInt64(stmt, col++);
    r.round_number = columnInt(stmt, col++);
    r.sig_strikes_landed = columnInt(stmt, col++);
    r.sig_strikes_attempted = columnInt(stmt, col++);
    r.total_strikes_landed = columnInt(stmt, col++);
    r.total_strikes_attempted = columnInt(stmt, col++);
    r.takedowns_landed = columnInt(stmt, col++);
    r.takedowns_attempted = columnInt(stmt, col++);
    r.sub_attempts = columnInt(stmt, col++);
    r.reversals = columnInt(stmt, col++);
    r.knockdowns = columnInt(stmt, col++);
    r.control_time_seconds = columnDouble(stmt, col++);
    r.head_strikes_landed = columnInt(stmt, col++);
    r.body_strikes_landed = columnInt(stmt, col++);
    r.leg_strikes_landed = columnInt(stmt, col++);
    r.distance_strikes_landed = columnInt(stmt, col++);
    r.clinch_strikes_landed = columnInt(stmt, col++);
    r.ground_strikes_landed = columnInt(stmt, col++);
    return r;
}

std::optional<RoundStats> RoundStats::getById(sqlite3* db, int64_t id) {
    const std::string sql =
        std::string("SELECT ") + kRoundStatsColumns + " FROM round_stats WHERE id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id);

    std::optional<RoundStats> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = fromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<RoundStats> RoundStats::getByFightFighter(
    sqlite3* db, int64_t fight_id, int64_t fighter_id) {
    const std::string sql = std::string("SELECT ") + kRoundStatsColumns +
                            " FROM round_stats "
                            "WHERE fight_id = ?1 AND fighter_id = ?2 "
                            "ORDER BY round_number";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    sqlite3_bind_int64(stmt, 1, fight_id);
    sqlite3_bind_int64(stmt, 2, fighter_id);
    std::vector<RoundStats> rows = fetchAll(stmt);
    sqlite3_finalize(stmt);
    return rows;
}

std::optional<RoundStats> RoundStats::getByFightFighterRound(
    sqlite3* db, int64_t fight_id, int64_t fighter_id, int round_number) {
    const std::string sql = std::string("SELECT ") + kRoundStatsColumns +
                            " FROM round_stats "
                            "WHERE fight_id = ?1 AND fighter_id = ?2 AND round_number = ?3";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, fight_id);
    sqlite3_bind_int64(stmt, 2, fighter_id);
    sqlite3_bind_int(stmt, 3, round_number);

    std::optional<RoundStats> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = fromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<RoundStats> RoundStats::listForFight(sqlite3* db, int64_t fight_id) {
    const std::string sql = std::string("SELECT ") + kRoundStatsColumns +
                            " FROM round_stats WHERE fight_id = ?1 "
                            "ORDER BY round_number, fighter_id";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_int64(stmt, 1, fight_id);
    std::vector<RoundStats> rows = fetchAll(stmt);
    sqlite3_finalize(stmt);
    return rows;
}

RoundStats::OpponentRoundTotals RoundStats::opponentTotalsForFighter(
    sqlite3* db, int64_t fighter_id) {
    const char* sql =
        "SELECT "
        "  COALESCE(SUM(opp.sig_strikes_attempted), 0), "
        "  COALESCE(SUM(opp.sig_strikes_landed), 0), "
        "  COALESCE(SUM(opp.takedowns_attempted), 0), "
        "  COALESCE(SUM(opp.takedowns_landed), 0) "
        "FROM round_stats AS self "
        "INNER JOIN round_stats AS opp "
        "  ON opp.fight_id = self.fight_id "
        " AND opp.round_number = self.round_number "
        " AND opp.fighter_id != self.fighter_id "
        "WHERE self.fighter_id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    sqlite3_bind_int64(stmt, 1, fighter_id);

    OpponentRoundTotals totals;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        totals.sig_strikes_attempted = db::columnInt(stmt, 0);
        totals.sig_strikes_landed = db::columnInt(stmt, 1);
        totals.takedowns_attempted = db::columnInt(stmt, 2);
        totals.takedowns_landed = db::columnInt(stmt, 3);
    }

    sqlite3_finalize(stmt);
    return totals;
}

std::vector<RoundStats> RoundStats::listForFighter(sqlite3* db, int64_t fighter_id) {
    const std::string sql =
        "SELECT rs.id, rs.fight_id, rs.fighter_id, rs.round_number, "
        "rs.sig_strikes_landed, rs.sig_strikes_attempted, "
        "rs.total_strikes_landed, rs.total_strikes_attempted, "
        "rs.takedowns_landed, rs.takedowns_attempted, rs.sub_attempts, rs.reversals, "
        "rs.knockdowns, rs.control_time_seconds, rs.head_strikes_landed, "
        "rs.body_strikes_landed, rs.leg_strikes_landed, rs.distance_strikes_landed, "
        "rs.clinch_strikes_landed, rs.ground_strikes_landed "
        "FROM round_stats AS rs "
        "INNER JOIN fights AS f ON f.id = rs.fight_id "
        "INNER JOIN events AS e ON e.id = f.event_id "
        "WHERE rs.fighter_id = ?1 "
        "ORDER BY e.event_date DESC, rs.round_number";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }
    sqlite3_bind_int64(stmt, 1, fighter_id);
    std::vector<RoundStats> rows = fetchAll(stmt);
    sqlite3_finalize(stmt);
    return rows;
}

}  // namespace ufc
