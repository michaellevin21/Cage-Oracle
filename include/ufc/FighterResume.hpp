#pragma once

#include <cstdint>

struct sqlite3;

namespace ufc {

/// Sum of points from career wins over opponents currently ranked in a weight class.
/// Rank 0 (champion) = 16 points, rank 1 = 15, …, rank 15 = 1. Unranked opponents
/// and pound-for-pound-only listings contribute nothing. Opponents ranked in multiple
/// weight classes use only their best (lowest rank number) listing.
double computeResumeScore(sqlite3* db, int64_t fighter_id);

}  // namespace ufc
