#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ufc {

/// Row from the `round_stats` table (UfcDbSchema.sql).
class RoundStats {
public:
    int64_t id = 0;
    int64_t fight_id = 0;
    int64_t fighter_id = 0;
    int round_number = 0;
    int sig_strikes_landed = 0;
    int sig_strikes_attempted = 0;
    int total_strikes_landed = 0;
    int total_strikes_attempted = 0;
    int takedowns_landed = 0;
    int takedowns_attempted = 0;
    int sub_attempts = 0;
    int reversals = 0;
    int knockdowns = 0;
    double control_time_seconds = 0.0;
    int head_strikes_landed = 0;
    int body_strikes_landed = 0;
    int leg_strikes_landed = 0;
    int distance_strikes_landed = 0;
    int clinch_strikes_landed = 0;
    int ground_strikes_landed = 0;

    static std::optional<RoundStats> getById(sqlite3* db, int64_t id);
    static std::vector<RoundStats> getByFightFighter(
        sqlite3* db, int64_t fight_id, int64_t fighter_id);
    static std::optional<RoundStats> getByFightFighterRound(
        sqlite3* db, int64_t fight_id, int64_t fighter_id, int round_number);
    static std::vector<RoundStats> listForFight(sqlite3* db, int64_t fight_id);
    static std::vector<RoundStats> listForFighter(sqlite3* db, int64_t fighter_id);

private:
    static RoundStats fromStatement(sqlite3_stmt* stmt);
    static std::vector<RoundStats> fetchAll(sqlite3_stmt* stmt);
};

}  // namespace ufc
