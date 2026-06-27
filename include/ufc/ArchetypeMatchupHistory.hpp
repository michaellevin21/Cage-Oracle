#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct sqlite3;

namespace ufc {

struct StyleWinRate {
    double win_pct = 0.0;
    int decisive = 0;
    int draws = 0;
};

struct ArchetypePairCell {
    int lower_wins = 0;
    int higher_wins = 0;
    int draws = 0;
    int mirror_decisive = 0;
    int decisive() const { return lower_wins + higher_wins; }
    int total() const { return decisive() + draws + mirror_decisive; }
};

using ArchetypePairKey = std::pair<std::string, std::string>;
using ArchetypeWeightStore = std::map<ArchetypePairKey, ArchetypePairCell>;
using ArchetypeByWeight = std::map<std::string, ArchetypeWeightStore>;

class ArchetypeMatchupIndex {
public:
    static ArchetypeMatchupIndex load(sqlite3* db, const std::string& mode = "current");

    std::optional<StyleWinRate> winRate(
        const std::string& archetype_a,
        const std::string& archetype_b,
        const std::string& weight_class) const;

    const std::set<std::string>& rankedWeightClasses() const noexcept { return ranked_weight_classes_; }

    std::vector<std::string> buildSummaries(
        const std::string& arch_a,
        const std::string& arch_b,
        const std::string& wc_a,
        const std::string& wc_b,
        int min_sample = 5) const;

private:
    std::string mode_;
    ArchetypeByWeight by_weight_;
    std::set<std::string> ranked_weight_classes_;
};

std::pair<std::string, std::string> canonicalArchetypePair(const std::string& a, const std::string& b);

const ArchetypeMatchupIndex& getArchetypeIndex(sqlite3* db);

std::vector<std::string> resolveMatchupWeightClasses(
    const std::string& wc_a,
    const std::string& wc_b,
    const std::set<std::string>& ranked);

}  // namespace ufc
