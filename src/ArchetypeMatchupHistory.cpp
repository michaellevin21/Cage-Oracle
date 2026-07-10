#include "ufc/ArchetypeMatchupHistory.hpp"

#include "ufc/FighterArchetype.hpp"
#include "ufc/PrefightCareer.hpp"
#include "ufc/RoundStats.hpp"
#include "ufc/SqliteHelpers.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>

namespace ufc {

namespace {

constexpr int kDefaultMinSample = 5;

const char* kArchetypeOrder[] = {
    "Pressure Striker",
    "Control Wrestler",
    "Ground Finisher",
    "All-Around Fighter",
    "Counter Striker",
};

int archetypeIndex(const std::string& name) {
    for (int i = 0; i < 5; ++i) {
        if (name == kArchetypeOrder[i]) {
            return i;
        }
    }
    return -1;
}

bool isPoundForPound(const std::string& wc) {
    std::string lower = wc;
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower.find("pound-for-pound") != std::string::npos;
}

const char* kRankingsOrder[] = {
    "Flyweight", "Bantamweight", "Featherweight", "Lightweight", "Welterweight",
    "Middleweight", "Light Heavyweight", "Heavyweight",
    "Women's Strawweight", "Women's Flyweight", "Women's Bantamweight",
};

std::set<std::string> loadRankedWeightClasses(sqlite3* db) {
    std::set<std::string> ranked;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT DISTINCT weight_class FROM fighter_rankings ORDER BY weight_class", -1, &stmt, nullptr) != SQLITE_OK) {
        return ranked;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const std::string wc = db::columnText(stmt, 0);
        if (!wc.empty() && !isPoundForPound(wc)) {
            ranked.insert(wc);
        }
    }
    sqlite3_finalize(stmt);
    return ranked;
}

std::string normalizeWeightClass(const std::string& label) {
    size_t start = label.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return {};
    }
    size_t end = label.find_last_not_of(" \t");
    return label.substr(start, end - start + 1);
}

std::optional<std::string> winnerArchetype(
    int64_t winner_id, int64_t f1, int64_t f2,
    const std::string& arch1, const std::string& arch2) {
    if (winner_id <= 0) {
        return std::nullopt;
    }
    if (winner_id == f1) {
        return arch1;
    }
    if (winner_id == f2) {
        return arch2;
    }
    return std::nullopt;
}

using WeightStore = ArchetypeWeightStore;
using ByWeight = ArchetypeByWeight;

void recordOutcome(
    WeightStore& store,
    const std::string& arch_a,
    const std::string& arch_b,
    const std::optional<std::string>& winner_arch) {
    auto [lower, higher] = canonicalArchetypePair(arch_a, arch_b);
    auto& cell = store[{lower, higher}];
    if (lower == higher) {
        if (!winner_arch) {
            cell.draws += 1;
        } else {
            cell.mirror_decisive += 1;
        }
        return;
    }
    if (!winner_arch) {
        cell.draws += 1;
    } else if (*winner_arch == lower) {
        cell.lower_wins += 1;
    } else if (*winner_arch == higher) {
        cell.higher_wins += 1;
    }
}

bool recordFight(
    ByWeight& by_weight,
    const std::string& weight_class,
    const std::string& arch1,
    const std::string& arch2,
    int64_t winner_id,
    int64_t f1,
    int64_t f2,
    const std::set<std::string>& allowed) {
    const std::string wc = normalizeWeightClass(weight_class);
    if (wc.empty() || allowed.find(wc) == allowed.end()) {
        return false;
    }
    if (arch1.empty() || arch2.empty()) {
        return false;
    }
    if (archetypeIndex(arch1) < 0 || archetypeIndex(arch2) < 0) {
        return false;
    }
    auto winner = winnerArchetype(winner_id, f1, f2, arch1, arch2);
    recordOutcome(by_weight[wc], arch1, arch2, winner);
    return true;
}

std::string archetypePlural(const std::string& label) {
    const size_t pos = label.rfind(' ');
    if (pos == std::string::npos) {
        return label + "s";
    }
    const std::string word = label.substr(pos + 1);
    const std::string head = label.substr(0, pos);
    if (word == "Fighter") {
        return head.empty() ? "Fighters" : head + " Fighters";
    }
    if (word == "Striker") {
        return head.empty() ? "Strikers" : head + " Strikers";
    }
    if (word == "Wrestler") {
        return head.empty() ? "Wrestlers" : head + " Wrestlers";
    }
    if (word == "Finisher") {
        return head.empty() ? "Finishers" : head + " Finishers";
    }
    return label + "s";
}

std::string formatStyleSentence(
    const std::string& arch_a, const std::string& arch_b,
    double win_pct, const std::string& wc, int decisive) {
    const char* fight_word = decisive == 1 ? "fight" : "fights";
    char buf[512];
    std::snprintf(buf, sizeof(buf), "%s win %.0f%% against %s at %s (%d %s).",
        archetypePlural(arch_a).c_str(), win_pct,
        archetypePlural(arch_b).c_str(), wc.c_str(), decisive, fight_word);
    return buf;
}

ByWeight buildMatrixCurrent(sqlite3* db, const std::set<std::string>& allowed) {
    ByWeight by_weight;
    const char* sql =
        "SELECT f.fighter1_id, f.fighter2_id, f.winner_id, f.weight_class, "
        "f1.archetype AS arch1, f2.archetype AS arch2 "
        "FROM fights f "
        "JOIN fighters f1 ON f1.id = f.fighter1_id "
        "JOIN fighters f2 ON f2.id = f.fighter2_id";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return by_weight;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        recordFight(by_weight,
            db::columnText(stmt, 3),
            db::columnText(stmt, 4),
            db::columnText(stmt, 5),
            db::columnInt64(stmt, 2),
            db::columnInt64(stmt, 0),
            db::columnInt64(stmt, 1),
            allowed);
    }
    sqlite3_finalize(stmt);
    return by_weight;
}

ByWeight buildMatrixPrefight(sqlite3* db, const std::set<std::string>& allowed) {
    ByWeight by_weight;

    struct FightRow {
        int64_t id, f1, f2, winner_id;
        std::string wc;
    };
    std::vector<FightRow> fights;
    const char* fight_sql =
        "SELECT f.id, f.fighter1_id, f.fighter2_id, f.winner_id, f.weight_class "
        "FROM fights f INNER JOIN events e ON e.id = f.event_id "
        "ORDER BY e.event_date ASC, f.id ASC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, fight_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return by_weight;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        fights.push_back({
            db::columnInt64(stmt, 0), db::columnInt64(stmt, 1),
            db::columnInt64(stmt, 2), db::columnInt64(stmt, 3),
            db::columnText(stmt, 4),
        });
    }
    sqlite3_finalize(stmt);

    struct RoundKey {
        int64_t fighter_id;
        int round_number;
        bool operator==(const RoundKey& o) const {
            return fighter_id == o.fighter_id && round_number == o.round_number;
        }
    };
    struct RoundKeyHash {
        size_t operator()(const RoundKey& k) const {
            return std::hash<int64_t>()(k.fighter_id) ^ (std::hash<int>()(k.round_number) << 1);
        }
    };

    const char* rs_sql =
        "SELECT rs.fight_id, rs.fighter_id, rs.round_number, "
        "rs.sig_strikes_landed, rs.sig_strikes_attempted, "
        "rs.total_strikes_landed, rs.total_strikes_attempted, "
        "rs.takedowns_landed, rs.takedowns_attempted, "
        "rs.sub_attempts, rs.reversals, rs.knockdowns, "
        "rs.control_time_seconds, rs.head_strikes_landed, "
        "rs.body_strikes_landed, rs.leg_strikes_landed, "
        "rs.distance_strikes_landed, rs.clinch_strikes_landed, rs.ground_strikes_landed "
        "FROM round_stats rs ORDER BY rs.fight_id, rs.round_number, rs.fighter_id";
    std::unordered_map<int64_t, std::vector<RoundStats>> by_fight;
    if (sqlite3_prepare_v2(db, rs_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            RoundStats r;
            int col = 0;
            r.fight_id = db::columnInt64(stmt, col++);
            r.fighter_id = db::columnInt64(stmt, col++);
            r.round_number = db::columnInt(stmt, col++);
            r.sig_strikes_landed = db::columnInt(stmt, col++);
            r.sig_strikes_attempted = db::columnInt(stmt, col++);
            r.total_strikes_landed = db::columnInt(stmt, col++);
            r.total_strikes_attempted = db::columnInt(stmt, col++);
            r.takedowns_landed = db::columnInt(stmt, col++);
            r.takedowns_attempted = db::columnInt(stmt, col++);
            r.sub_attempts = db::columnInt(stmt, col++);
            r.reversals = db::columnInt(stmt, col++);
            r.knockdowns = db::columnInt(stmt, col++);
            r.control_time_seconds = db::columnDouble(stmt, col++);
            r.head_strikes_landed = db::columnInt(stmt, col++);
            r.body_strikes_landed = db::columnInt(stmt, col++);
            r.leg_strikes_landed = db::columnInt(stmt, col++);
            r.distance_strikes_landed = db::columnInt(stmt, col++);
            r.clinch_strikes_landed = db::columnInt(stmt, col++);
            r.ground_strikes_landed = db::columnInt(stmt, col++);
            by_fight[r.fight_id].push_back(r);
        }
        sqlite3_finalize(stmt);
    }

    std::unordered_map<int64_t, CareerAccumulator> career;

    for (const FightRow& fight : fights) {
        std::string arch1, arch2;
        if (const auto a = classifyArchetype(career[fight.f1].toStats())) {
            arch1 = toString(*a);
        }
        if (const auto a = classifyArchetype(career[fight.f2].toStats())) {
            arch2 = toString(*a);
        }
        recordFight(by_weight, fight.wc, arch1, arch2, fight.winner_id, fight.f1, fight.f2, allowed);

        std::unordered_map<RoundKey, const RoundStats*, RoundKeyHash> by_key;
        for (const RoundStats& row : by_fight[fight.id]) {
            by_key[{row.fighter_id, row.round_number}] = &row;
        }
        for (const auto& [key, row] : by_key) {
            const int64_t opponent_id = key.fighter_id == fight.f1 ? fight.f2 : fight.f1;
            const auto it = by_key.find({opponent_id, key.round_number});
            if (it != by_key.end()) {
                career[key.fighter_id].addRound(*row, *it->second);
            }
        }
    }
    return by_weight;
}

}  // namespace

std::pair<std::string, std::string> canonicalArchetypePair(
    const std::string& a, const std::string& b) {
    const int ia = archetypeIndex(a);
    const int ib = archetypeIndex(b);
    if (ia < 0 || ib < 0) {
        return ia <= ib ? std::make_pair(a, b) : std::make_pair(b, a);
    }
    return ia <= ib ? std::make_pair(a, b) : std::make_pair(b, a);
}

ArchetypeMatchupIndex ArchetypeMatchupIndex::load(sqlite3* db, const std::string& mode) {
    ArchetypeMatchupIndex index;
    index.mode_ = mode;
    index.ranked_weight_classes_ = loadRankedWeightClasses(db);
    if (mode == "current") {
        index.by_weight_ = buildMatrixCurrent(db, index.ranked_weight_classes_);
    } else {
        index.by_weight_ = buildMatrixPrefight(db, index.ranked_weight_classes_);
    }
    return index;
}

std::optional<StyleWinRate> ArchetypeMatchupIndex::winRate(
    const std::string& archetype_a,
    const std::string& archetype_b,
    const std::string& weight_class) const {
    if (archetype_a == archetype_b) {
        return std::nullopt;
    }
    const auto wc_it = by_weight_.find(weight_class);
    if (wc_it == by_weight_.end()) {
        return std::nullopt;
    }
    const auto [lower, higher] = canonicalArchetypePair(archetype_a, archetype_b);
    const auto cell_it = wc_it->second.find({lower, higher});
    if (cell_it == wc_it->second.end() || cell_it->second.decisive() == 0) {
        return std::nullopt;
    }
    const ArchetypePairCell& cell = cell_it->second;
    const int wins = archetype_a == lower ? cell.lower_wins : cell.higher_wins;
    StyleWinRate rate;
    rate.win_pct = 100.0 * wins / cell.decisive();
    rate.decisive = cell.decisive();
    rate.draws = cell.draws;
    return rate;
}

std::vector<std::string> resolveMatchupWeightClasses(
    const std::string& wc_a,
    const std::string& wc_b,
    const std::set<std::string>& ranked) {
    std::vector<std::string> classes;
    const std::string a = normalizeWeightClass(wc_a);
    const std::string b = normalizeWeightClass(wc_b);
    if (!a.empty() && ranked.count(a)) {
        classes.push_back(a);
    }
    if (!b.empty() && ranked.count(b) && b != a) {
        classes.push_back(b);
    }
    auto orderIndex = [](const std::string& name) {
        for (int i = 0; i < 11; ++i) {
            if (name == kRankingsOrder[i]) {
                return i;
            }
        }
        return 999;
    };
    std::sort(classes.begin(), classes.end(), [&](const std::string& x, const std::string& y) {
        const int ix = orderIndex(x);
        const int iy = orderIndex(y);
        return ix < iy || (ix == iy && x < y);
    });
    return classes;
}

std::vector<std::string> ArchetypeMatchupIndex::buildSummaries(
    const std::string& arch_a,
    const std::string& arch_b,
    const std::string& wc_a,
    const std::string& wc_b,
    int min_sample) const {
    std::vector<std::string> summaries;
    if (arch_a.empty() || arch_b.empty()) {
        return summaries;
    }
    const auto weight_classes = resolveMatchupWeightClasses(wc_a, wc_b, ranked_weight_classes_);
    if (weight_classes.empty()) {
        return summaries;
    }
    for (const std::string& wc : weight_classes) {
        if (arch_a == arch_b) {
            const auto wc_it = by_weight_.find(wc);
            int total = 0;
            if (wc_it != by_weight_.end()) {
                const auto cell_it = wc_it->second.find({arch_a, arch_a});
                if (cell_it != wc_it->second.end()) {
                    total = cell_it->second.total();
                }
            }
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "Both fighters are %s; %d historical same-style bouts at %s.",
                archetypePlural(arch_a).c_str(), total, wc.c_str());
            summaries.push_back(buf);
            continue;
        }
        const auto rate = winRate(arch_a, arch_b, wc);
        if (!rate || rate->decisive < min_sample) {
            const int n = rate ? rate->decisive : 0;
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "Insufficient data for %s vs %s at %s (%d decisive fights, need %d).",
                archetypePlural(arch_a).c_str(), archetypePlural(arch_b).c_str(),
                wc.c_str(), n, min_sample);
            summaries.push_back(buf);
            continue;
        }
        if (rate->win_pct > 50.0) {
            summaries.push_back(formatStyleSentence(arch_a, arch_b, rate->win_pct, wc, rate->decisive));
        } else if (rate->win_pct < 50.0) {
            summaries.push_back(formatStyleSentence(arch_b, arch_a, 100.0 - rate->win_pct, wc, rate->decisive));
        } else {
            const char* fight_word = rate->decisive == 1 ? "fight" : "fights";
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "%s and %s win equally often at %s (%d %s).",
                archetypePlural(arch_a).c_str(), archetypePlural(arch_b).c_str(),
                wc.c_str(), rate->decisive, fight_word);
            summaries.push_back(buf);
        }
    }
    return summaries;
}

const ArchetypeMatchupIndex& getArchetypeIndex(sqlite3* db) {
    static sqlite3* cached_db = nullptr;
    static ArchetypeMatchupIndex cached_index = [] {
        ArchetypeMatchupIndex empty;
        return empty;
    }();
    if (db != cached_db) {
        cached_db = db;
        cached_index = ArchetypeMatchupIndex::load(db, "current");
    }
    return cached_index;
}

}  // namespace ufc
