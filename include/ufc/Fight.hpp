#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ufc {

/// Row from the `fights` table (UfcDbSchema.sql).
class Fight {
public:
    int64_t id = 0;
    std::string ufc_fight_id;
    int64_t fighter1_id = 0;
    int64_t fighter2_id = 0;
    int64_t event_id = 0;
    int64_t winner_id = 0;  // 0 when draw/NC (SQL NULL)
    std::string result_method;
    std::optional<std::string> result_method_detail;
    int64_t result_round = 0;
    double result_time_seconds = 0.0;
    std::string weight_class;
    bool is_title_fight = false;

    static std::optional<Fight> getById(sqlite3* db, int64_t id);
    static std::optional<Fight> getByUfcFightId(sqlite3* db, const std::string& ufc_fight_id);
    static std::vector<Fight> getByFighters(
        sqlite3* db, int64_t fighter1_id, int64_t fighter2_id);
    static std::optional<Fight> getByFightersEvent(
        sqlite3* db, int64_t fighter1_id, int64_t fighter2_id, int64_t event_id);
    static std::vector<Fight> listForFighter(sqlite3* db, int64_t fighter_id);
    static std::vector<Fight> listForEvent(sqlite3* db, int64_t event_id);

private:
    static Fight fromStatement(sqlite3_stmt* stmt);
    static std::vector<Fight> fetchAll(sqlite3_stmt* stmt);
};

}  // namespace ufc
