#pragma once

#include "ufc/FighterCareerStats.hpp"
#include "ufc/RoundStats.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct sqlite3;

namespace ufc {

class CareerAccumulator {
public:
    void addRound(const RoundStats& row, const RoundStats& opponent);
    FighterCareerStats toStats() const;

private:
    FighterCareerStats stats_;
};

struct PrefightFightRecord {
    int64_t id = 0;
    int64_t fighter1_id = 0;
    int64_t fighter2_id = 0;
    int64_t event_id = 0;
    int64_t event_date = 0;
    int64_t winner_id = 0;
    std::string event_name;
};

/// Pre-fight attribute vectors for every fight, built in chronological order.
class PrefightMatchupIndex {
public:
    bool build(sqlite3* db);

    const std::vector<PrefightFightRecord>& fights() const noexcept { return fights_; }
    const std::vector<double>* vectorForFight(int64_t fight_id) const;

private:
    std::vector<PrefightFightRecord> fights_;
    std::unordered_map<int64_t, std::vector<double>> vectors_by_fight_;
};

}  // namespace ufc
