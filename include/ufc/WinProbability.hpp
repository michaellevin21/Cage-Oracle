#pragma once

#include "ufc/ArchetypeMatchupHistory.hpp"
#include "ufc/Matchup.hpp"
#include "ufc/SimilaritySearch.hpp"

struct sqlite3;

namespace ufc {

struct WinProbabilityResult {
    double p_fighter_a = 0.5;
    double p_fighter_b = 0.5;
};

WinProbabilityResult estimateWinProbability(
    sqlite3* db,
    const Matchup& matchup,
    const SimilarMatchupResults& similar,
    int64_t fighter_a_id,
    int64_t fighter_b_id,
    const ArchetypeMatchupIndex& archetype_index);

bool bothFightersHaveFightHistory(sqlite3* db, int64_t fighter_a_id, int64_t fighter_b_id);

}  // namespace ufc
