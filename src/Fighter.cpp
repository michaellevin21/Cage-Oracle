#include "ufc/Fighter.hpp"

#include "ufc/SqliteHelpers.hpp"

#include <sqlite3.h>

namespace ufc {

namespace {

constexpr const char* kFighterColumns =
    "id, ufc_id, name, stance, reach_cm, height_cm, weight_class, "
    "date_of_birth, archetype, momentum_score, resume_score, profile_url, last_updated";

}  // namespace

Fighter Fighter::fromStatement(sqlite3_stmt* stmt) {
    using db::columnDoubleOptional;
    using db::columnInt64;
    using db::columnInt64Optional;
    using db::columnText;
    using db::columnTextOptional;

    Fighter f;
    int col = 0;
    f.id = columnInt64(stmt, col++);
    f.ufc_id = columnText(stmt, col++);
    f.name = columnText(stmt, col++);
    f.stance = columnTextOptional(stmt, col++);
    f.reach_cm = columnInt64Optional(stmt, col++);
    f.height_cm = columnInt64Optional(stmt, col++);
    f.weight_class = columnText(stmt, col++);
    f.date_of_birth = columnInt64Optional(stmt, col++);
    f.archetype = columnTextOptional(stmt, col++);
    f.momentum_score = columnDoubleOptional(stmt, col++);
    f.resume_score = columnDoubleOptional(stmt, col++);
    f.profile_url = columnText(stmt, col++);
    f.last_updated = columnInt64(stmt, col++);
    return f;
}

std::optional<Fighter> Fighter::getById(sqlite3* db, int64_t id) {
    const std::string sql =
        std::string("SELECT ") + kFighterColumns + " FROM fighters WHERE id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, id);

    std::optional<Fighter> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = fromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<Fighter> Fighter::getByUfcId(sqlite3* db, const std::string& ufc_id) {
    const std::string sql =
        std::string("SELECT ") + kFighterColumns + " FROM fighters WHERE ufc_id = ?1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, ufc_id.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Fighter> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = fromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<Fighter> Fighter::getByName(sqlite3* db, const std::string& name) {
    const std::string sql =
        std::string("SELECT ") + kFighterColumns + " FROM fighters WHERE name = ?1 LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Fighter> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = fromStatement(stmt);
    }

    sqlite3_finalize(stmt);
    return result;
}

}  // namespace ufc
