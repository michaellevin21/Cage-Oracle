#pragma once

/**
 * C API for calling UFC database code from other languages (Python ctypes, etc.).
 *
 * Lifetime: call ufc_db_open once per process/worker, pass the handle to queries,
 * then ufc_db_close on shutdown.
 *
 * Return values: JSON strings allocated with malloc; free with ufc_free_string.
 *   - Single-entity lookups return nullptr when not found (no error).
 *   - List queries return "[]" when empty.
 *   - nullptr + ufc_last_error() indicates a library/setup failure.
 */

#ifdef _WIN32
#  ifdef UFC_DB_BUILDING
#    define UFC_DB_API __declspec(dllexport)
#  else
#    define UFC_DB_API __declspec(dllimport)
#  endif
#else
#  define UFC_DB_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct UfcDb UfcDb;

UFC_DB_API UfcDb* ufc_db_open(const char* path);
UFC_DB_API void ufc_db_close(UfcDb* db);

UFC_DB_API void ufc_free_string(char* value);
UFC_DB_API const char* ufc_last_error(void);

/* Fighter */
UFC_DB_API char* ufc_get_fighter_by_id(UfcDb* db, long long id);
UFC_DB_API char* ufc_get_fighter_by_ufc_id(UfcDb* db, const char* ufc_id);
UFC_DB_API char* ufc_get_fighter_by_name(UfcDb* db, const char* name);

/* Fight */
UFC_DB_API char* ufc_get_fight_by_id(UfcDb* db, long long id);
UFC_DB_API char* ufc_get_fight_by_ufc_fight_id(UfcDb* db, const char* ufc_fight_id);
UFC_DB_API char* ufc_get_fights_by_fighters(
    UfcDb* db, long long fighter1_id, long long fighter2_id);
UFC_DB_API char* ufc_get_fight_by_fighters_event(
    UfcDb* db, long long fighter1_id, long long fighter2_id, long long event_id);
UFC_DB_API char* ufc_list_fights_for_fighter(UfcDb* db, long long fighter_id);
UFC_DB_API char* ufc_list_fights_for_event(UfcDb* db, long long event_id);

/* Round stats */
UFC_DB_API char* ufc_get_round_stats_by_fight_fighter(
    UfcDb* db, long long fight_id, long long fighter_id);
UFC_DB_API char* ufc_get_round_stat_by_fight_fighter_round(
    UfcDb* db, long long fight_id, long long fighter_id, int round_number);
UFC_DB_API char* ufc_list_round_stats_for_fight(UfcDb* db, long long fight_id);
UFC_DB_API char* ufc_list_round_stats_for_fighter(UfcDb* db, long long fighter_id);

/* Matchup (side-by-side profile + career aggregates) */
UFC_DB_API char* ufc_get_matchup_by_ids(
    UfcDb* db, long long fighter_a_id, long long fighter_b_id);
UFC_DB_API char* ufc_get_matchup_by_names(
    UfcDb* db, const char* fighter_a_name, const char* fighter_b_name);

/* Archetype classification (career round stats) */
UFC_DB_API char* ufc_classify_archetype_by_fighter_id(UfcDb* db, long long fighter_id);

#ifdef __cplusplus
}
#endif
