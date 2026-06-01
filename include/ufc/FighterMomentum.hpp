#pragma once

#include <cstdint>
#include <optional>

struct sqlite3;

namespace ufc {

/// Recency-weighted recent performance score (0–100, 50 = neutral).
///
/// Returns 0 when the fighter's most recent bout was more than two years ago.
/// Otherwise uses up to five decisive UFC fights.
///
/// Momentum is only computed when there are at least three decisive fights;
/// fighters with fewer decisive bouts return nullopt.
///
/// Recency is measured from today: full weight for one year after each bout,
/// then exponential decay. Each bout is scored by outcome, opponent quality
/// (rankings + title fights), and whether the win was a finish (KO/TKO or submission).
std::optional<double> computeMomentumScore(sqlite3* db, int64_t fighter_id);

}  // namespace ufc
