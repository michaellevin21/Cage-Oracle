#include "ufc_db.h"

#include "ufc/Fight.hpp"
#include "ufc/Fighter.hpp"
#include "ufc/FighterArchetype.hpp"
#include "ufc/JsonSerialize.hpp"
#include "ufc/Matchup.hpp"
#include "ufc/RoundStats.hpp"
#include "ufc/UfcDatabase.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

struct sqlite3;

struct UfcDb {
    ufc::UfcDatabase database;
    explicit UfcDb(const std::string& path) : database(path) {}
};

namespace {

thread_local std::string g_last_error;

char* duplicateString(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (!out) {
        return nullptr;
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

char* duplicateJson(const std::string& json) {
    return duplicateString(json);
}

UfcDb* asWrapper(UfcDb* handle) {
    return handle;
}

sqlite3* connection(UfcDb* handle) {
    return asWrapper(handle)->database.handle();
}

bool ensureOpen(UfcDb* handle) {
    if (!handle) {
        g_last_error = "database handle is null";
        return false;
    }
    if (!handle->database.handle()) {
        g_last_error = handle->database.lastError().empty()
                           ? "database is not open"
                           : handle->database.lastError();
        return false;
    }
    return true;
}

}  // namespace

extern "C" {

UfcDb* ufc_db_open(const char* path) {
    g_last_error.clear();
    if (!path) {
        g_last_error = "path is null";
        return nullptr;
    }

    try {
        auto* wrapper = new UfcDb(path);
        if (!wrapper->database.handle()) {
            g_last_error = wrapper->database.lastError();
            delete wrapper;
            return nullptr;
        }
        return wrapper;
    } catch (const std::exception& ex) {
        g_last_error = ex.what();
        return nullptr;
    } catch (...) {
        g_last_error = "unknown error opening database";
        return nullptr;
    }
}

void ufc_db_close(UfcDb* db) {
    delete db;
}

void ufc_free_string(char* value) {
    std::free(value);
}

const char* ufc_last_error(void) {
    return g_last_error.c_str();
}

char* ufc_get_fighter_by_id(UfcDb* db, long long id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    std::optional<ufc::Fighter> result = ufc::Fighter::getById(connection(db), id);
    return result ? duplicateJson(ufc::json::toJson(*result)) : nullptr;
}

char* ufc_get_fighter_by_ufc_id(UfcDb* db, const char* ufc_id) {
    if (!ensureOpen(db) || !ufc_id) {
        if (!ufc_id) {
            g_last_error = "ufc_id is null";
        }
        return nullptr;
    }
    std::optional<ufc::Fighter> result = ufc::Fighter::getByUfcId(connection(db), ufc_id);
    return result ? duplicateJson(ufc::json::toJson(*result)) : nullptr;
}

char* ufc_get_fighter_by_name(UfcDb* db, const char* name) {
    if (!ensureOpen(db) || !name) {
        if (!name) {
            g_last_error = "name is null";
        }
        return nullptr;
    }
    std::optional<ufc::Fighter> result = ufc::Fighter::getByName(connection(db), name);
    return result ? duplicateJson(ufc::json::toJson(*result)) : nullptr;
}

char* ufc_get_fight_by_id(UfcDb* db, long long id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    std::optional<ufc::Fight> result = ufc::Fight::getById(connection(db), id);
    return result ? duplicateJson(ufc::json::toJson(*result)) : nullptr;
}

char* ufc_get_fight_by_ufc_fight_id(UfcDb* db, const char* ufc_fight_id) {
    if (!ensureOpen(db) || !ufc_fight_id) {
        if (!ufc_fight_id) {
            g_last_error = "ufc_fight_id is null";
        }
        return nullptr;
    }
    std::optional<ufc::Fight> result = ufc::Fight::getByUfcFightId(connection(db), ufc_fight_id);
    return result ? duplicateJson(ufc::json::toJson(*result)) : nullptr;
}

char* ufc_get_fights_by_fighters(UfcDb* db, long long fighter1_id, long long fighter2_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    auto fights = ufc::Fight::getByFighters(connection(db), fighter1_id, fighter2_id);
    return duplicateJson(ufc::json::toJsonArray(fights));
}

char* ufc_get_fight_by_fighters_event(UfcDb* db, long long fighter1_id, long long fighter2_id, long long event_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    std::optional<ufc::Fight> result = ufc::Fight::getByFightersEvent(connection(db), fighter1_id, fighter2_id, event_id);
    return result ? duplicateJson(ufc::json::toJson(*result)) : nullptr;
}

char* ufc_list_fights_for_fighter(UfcDb* db, long long fighter_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    auto fights = ufc::Fight::listForFighter(connection(db), fighter_id);
    return duplicateJson(ufc::json::toJsonArray(fights));
}

char* ufc_list_fights_for_event(UfcDb* db, long long event_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    auto fights = ufc::Fight::listForEvent(connection(db), event_id);
    return duplicateJson(ufc::json::toJsonArray(fights));
}

char* ufc_get_round_stats_by_fight_fighter(UfcDb* db, long long fight_id, long long fighter_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    auto stats = ufc::RoundStats::getByFightFighter(connection(db), fight_id, fighter_id);
    return duplicateJson(ufc::json::toJsonArray(stats));
}

char* ufc_get_round_stat_by_fight_fighter_round(UfcDb* db, long long fight_id, long long fighter_id, int round_number) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    std::optional<ufc::RoundStats> result = ufc::RoundStats::getByFightFighterRound(connection(db), fight_id, fighter_id, round_number);
    return result ? duplicateJson(ufc::json::toJson(*result)) : nullptr;
}

char* ufc_list_round_stats_for_fight(UfcDb* db, long long fight_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    auto stats = ufc::RoundStats::listForFight(connection(db), fight_id);
    return duplicateJson(ufc::json::toJsonArray(stats));
}

char* ufc_list_round_stats_for_fighter(UfcDb* db, long long fighter_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    auto stats = ufc::RoundStats::listForFighter(connection(db), fighter_id);
    return duplicateJson(ufc::json::toJsonArray(stats));
}

char* ufc_get_matchup_by_ids(UfcDb* db, long long fighter_a_id, long long fighter_b_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    const std::optional<ufc::Fighter> a = ufc::Fighter::getById(connection(db), fighter_a_id);
    if (!a) {
        g_last_error = "fighter_a not found";
        return nullptr;
    }
    const std::optional<ufc::Fighter> b = ufc::Fighter::getById(connection(db), fighter_b_id);
    if (!b) {
        g_last_error = "fighter_b not found";
        return nullptr;
    }
    const ufc::Matchup matchup = ufc::Matchup::fromDatabase(connection(db), *a, *b);
    return duplicateJson(ufc::json::toJson(matchup));
}

char* ufc_get_matchup_by_names(UfcDb* db, const char* fighter_a_name, const char* fighter_b_name) {
    if (!ensureOpen(db) || !fighter_a_name || !fighter_b_name) {
        if (!fighter_a_name || !fighter_b_name) {
            g_last_error = "fighter name is null";
        }
        return nullptr;
    }
    const std::optional<ufc::Fighter> a = ufc::Fighter::getByName(connection(db), fighter_a_name);
    if (!a) {
        g_last_error = std::string("fighter not found: ") + fighter_a_name;
        return nullptr;
    }
    const std::optional<ufc::Fighter> b = ufc::Fighter::getByName(connection(db), fighter_b_name);
    if (!b) {
        g_last_error = std::string("fighter not found: ") + fighter_b_name;
        return nullptr;
    }
    const ufc::Matchup matchup = ufc::Matchup::fromDatabase(connection(db), *a, *b);
    return duplicateJson(ufc::json::toJson(matchup));
}

char* ufc_classify_archetype_by_fighter_id(UfcDb* db, long long fighter_id) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    const std::optional<ufc::FighterArchetype> archetype =
        ufc::classifyArchetypeForFighter(connection(db), fighter_id);
    return archetype ? duplicateString(ufc::toString(*archetype)) : nullptr;
}

}  // extern "C"
