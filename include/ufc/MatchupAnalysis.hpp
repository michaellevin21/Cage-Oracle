#pragma once

#include "ufc/AnalysisTypes.hpp"

#include <stdexcept>
#include <vector>

struct sqlite3;

namespace ufc {

std::vector<FighterSummary> searchFighters(sqlite3* db, const std::string& query, int limit = 20);

MatchupResponse analyzeMatchup(
    sqlite3* db,
    const std::string& fighter_a_name,
    const std::string& fighter_b_name);

}  // namespace ufc
