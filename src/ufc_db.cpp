#include "ufc_db.h"

#include "ufc/Fight.hpp"
#include "ufc/Fighter.hpp"
#include "ufc/FighterArchetype.hpp"
#include "ufc/FighterMomentum.hpp"
#include "ufc/FighterResume.hpp"
#include "ufc/JsonSerialize.hpp"
#include "ufc/Matchup.hpp"
#include "ufc/RoundStats.hpp"
#include "ufc/SimilaritySearch.hpp"
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

char* ufc_classify_archetype_from_totals(const UfcCareerTotals* totals) {
    g_last_error.clear();
    if (!totals) {
        g_last_error = "totals is null";
        return nullptr;
    }

    ufc::FighterCareerStats stats;
    stats.rounds = totals->rounds;
    stats.sig_strikes_landed = totals->sig_strikes_landed;
    stats.sig_strikes_attempted = totals->sig_strikes_attempted;
    stats.total_strikes_landed = totals->total_strikes_landed;
    stats.total_strikes_attempted = totals->total_strikes_attempted;
    stats.takedowns_landed = totals->takedowns_landed;
    stats.takedowns_attempted = totals->takedowns_attempted;
    stats.opponent_sig_strikes_landed = totals->opponent_sig_strikes_landed;
    stats.opponent_sig_strikes_attempted = totals->opponent_sig_strikes_attempted;
    stats.opponent_takedowns_landed = totals->opponent_takedowns_landed;
    stats.opponent_takedowns_attempted = totals->opponent_takedowns_attempted;
    stats.sub_attempts = totals->sub_attempts;
    stats.reversals = totals->reversals;
    stats.knockdowns = totals->knockdowns;
    stats.control_time_seconds = totals->control_time_seconds;
    stats.head_strikes_landed = totals->head_strikes_landed;
    stats.body_strikes_landed = totals->body_strikes_landed;
    stats.leg_strikes_landed = totals->leg_strikes_landed;
    stats.distance_strikes_landed = totals->distance_strikes_landed;
    stats.clinch_strikes_landed = totals->clinch_strikes_landed;
    stats.ground_strikes_landed = totals->ground_strikes_landed;

    const std::optional<ufc::FighterArchetype> archetype = ufc::classifyArchetype(stats);
    return archetype ? duplicateString(ufc::toString(*archetype)) : nullptr;
}

double ufc_compute_momentum_by_fighter_id(UfcDb* db, long long fighter_id) {
    if (!ensureOpen(db)) {
        return 0.0;
    }
    const std::optional<double> score = ufc::computeMomentumScore(connection(db), fighter_id);
    return score ? *score : 0.0;
}

int ufc_compute_momentum_by_fighter_id_out(UfcDb* db, long long fighter_id, double* out_score) {
    g_last_error.clear();
    if (!out_score) {
        g_last_error = "out_score is null";
        return 0;
    }
    if (!ensureOpen(db)) {
        return 0;
    }
    const std::optional<double> score = ufc::computeMomentumScore(connection(db), fighter_id);
    if (!score) {
        return 0;
    }
    *out_score = *score;
    return 1;
}

double ufc_compute_resume_by_fighter_id(UfcDb* db, long long fighter_id) {
    if (!ensureOpen(db)) {
        return 0.0;
    }
    return ufc::computeResumeScore(connection(db), fighter_id);
}

int ufc_compute_resume_by_fighter_id_out(UfcDb* db, long long fighter_id, double* out_score) {
    g_last_error.clear();
    if (!out_score) {
        g_last_error = "out_score is null";
        return 0;
    }
    if (!ensureOpen(db)) {
        return 0;
    }
    *out_score = ufc::computeResumeScore(connection(db), fighter_id);
    return 1;
}

char* ufc_find_similar_matchups(UfcDb* db, long long fighter_a_id, long long fighter_b_id, int top_k) {
    if (!ensureOpen(db)) {
        return nullptr;
    }
    if (!ufc::Fighter::getById(connection(db), fighter_a_id)) {
        g_last_error = "fighter_a not found";
        return nullptr;
    }
    if (!ufc::Fighter::getById(connection(db), fighter_b_id)) {
        g_last_error = "fighter_b not found";
        return nullptr;
    }
    const int k = top_k > 0 ? top_k : 5;
    const ufc::SimilarMatchupResults results =
        ufc::findSimilarHistoricalMatchups(connection(db), fighter_a_id, fighter_b_id, k);
    return duplicateJson(
        ufc::json::toJsonSimilarMatchups(connection(db), fighter_a_id, fighter_b_id, results));
}

char* ufc_find_similar_matchups_by_names(UfcDb* db, const char* fighter_a_name, const char* fighter_b_name, int top_k) {
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
    return ufc_find_similar_matchups(db, a->id, b->id, top_k);
}

}  // extern "C"
