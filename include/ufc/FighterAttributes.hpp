#pragma once

#include "ufc/FighterCareerStats.hpp"

#include <cstddef>
#include <optional>
#include <vector>

struct sqlite3;

namespace ufc {

class Fighter;

/// Career stat features plus height/reach for matchup-level cosine similarity.
class FighterAttributes {
public:
    static constexpr size_t kCareerStatDimension = 18;
    static constexpr size_t kPhysicalDeltaDimension = 2;
    static constexpr size_t kHeightDeltaIndex = kCareerStatDimension * 2;
    static constexpr size_t kReachDeltaIndex = kCareerStatDimension * 2 + 1;

    /// Post-normalization weights for physical deltas (reach weighted higher than height).
    static constexpr double kHeightDeltaWeight = 2.5;
    static constexpr double kReachDeltaWeight = 3.5;

    std::vector<double> values;
    std::optional<double> height_cm;
    std::optional<double> reach_cm;

    FighterAttributes();
    explicit FighterAttributes(std::vector<double> values);

    static FighterAttributes fromFighter(sqlite3* db, int64_t fighter_id);
    static FighterAttributes fromFighter(const Fighter& fighter, const FighterCareerStats& career);
    static FighterAttributes fromPrefight(const Fighter& fighter, const FighterCareerStats& career);

    /// [stats(lower_id), stats(higher_id), height_delta, reach_delta] — deltas are higher minus lower.
    static std::vector<double> matchupVector(const FighterAttributes& lower_id_fighter, const FighterAttributes& higher_id_fighter);

    static size_t matchupVectorDimension() {
        return kCareerStatDimension * 2 + kPhysicalDeltaDimension;
    }

    static void applyMatchupFeatureWeights(std::vector<double>& normalized);
};

}  // namespace ufc
