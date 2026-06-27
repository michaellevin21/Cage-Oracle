#include "ufc/PrefightCareer.hpp"

#include "ufc/Fighter.hpp"
#include "ufc/FighterAttributes.hpp"
#include "ufc/SqliteHelpers.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <unordered_map>

namespace ufc {

namespace {

RoundStats roundStatsFromStatement(sqlite3_stmt* stmt) {
    using db::columnDouble;
    using db::columnInt;
    using db::columnInt64;

    RoundStats r;
    int col = 0;
    r.id = columnInt64(stmt, col++);
    r.fight_id = columnInt64(stmt, col++);
    r.fighter_id = columnInt64(stmt, col++);
    r.round_number = columnInt(stmt, col++);
    r.sig_strikes_landed = columnInt(stmt, col++);
    r.sig_strikes_attempted = columnInt(stmt, col++);
    r.total_strikes_landed = columnInt(stmt, col++);
    r.total_strikes_attempted = columnInt(stmt, col++);
    r.takedowns_landed = columnInt(stmt, col++);
    r.takedowns_attempted = columnInt(stmt, col++);
    r.sub_attempts = columnInt(stmt, col++);
    r.reversals = columnInt(stmt, col++);
    r.knockdowns = columnInt(stmt, col++);
    r.control_time_seconds = columnDouble(stmt, col++);
    r.head_strikes_landed = columnInt(stmt, col++);
    r.body_strikes_landed = columnInt(stmt, col++);
    r.leg_strikes_landed = columnInt(stmt, col++);
    r.distance_strikes_landed = columnInt(stmt, col++);
    r.clinch_strikes_landed = columnInt(stmt, col++);
    r.ground_strikes_landed = columnInt(stmt, col++);
    return r;
}

}  // namespace

void CareerAccumulator::addRound(const RoundStats& row, const RoundStats& opponent) {
    stats_.rounds += 1;
    stats_.sig_strikes_landed += row.sig_strikes_landed;
    stats_.sig_strikes_attempted += row.sig_strikes_attempted;
    stats_.total_strikes_landed += row.total_strikes_landed;
    stats_.total_strikes_attempted += row.total_strikes_attempted;
    stats_.takedowns_landed += row.takedowns_landed;
    stats_.takedowns_attempted += row.takedowns_attempted;
    stats_.sub_attempts += row.sub_attempts;
    stats_.reversals += row.reversals;
    stats_.knockdowns += row.knockdowns;
    stats_.control_time_seconds += row.control_time_seconds;
    stats_.head_strikes_landed += row.head_strikes_landed;
    stats_.body_strikes_landed += row.body_strikes_landed;
    stats_.leg_strikes_landed += row.leg_strikes_landed;
    stats_.distance_strikes_landed += row.distance_strikes_landed;
    stats_.clinch_strikes_landed += row.clinch_strikes_landed;
    stats_.ground_strikes_landed += row.ground_strikes_landed;
    stats_.opponent_sig_strikes_landed += opponent.sig_strikes_landed;
    stats_.opponent_sig_strikes_attempted += opponent.sig_strikes_attempted;
    stats_.opponent_takedowns_landed += opponent.takedowns_landed;
    stats_.opponent_takedowns_attempted += opponent.takedowns_attempted;
}

FighterCareerStats CareerAccumulator::toStats() const {
    return stats_;
}

namespace {

std::vector<PrefightFightRecord> loadFightsChronological(sqlite3* db) {
    const char* sql =
        "SELECT f.id, f.fighter1_id, f.fighter2_id, f.event_id, f.winner_id, "
        "e.event_date, e.name "
        "FROM fights f "
        "INNER JOIN events e ON e.id = f.event_id "
        "ORDER BY e.event_date ASC, f.id ASC";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    std::vector<PrefightFightRecord> fights;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PrefightFightRecord row;
        int col = 0;
        row.id = db::columnInt64(stmt, col++);
        row.fighter1_id = db::columnInt64(stmt, col++);
        row.fighter2_id = db::columnInt64(stmt, col++);
        row.event_id = db::columnInt64(stmt, col++);
        row.winner_id = db::columnInt64(stmt, col++);
        row.event_date = db::columnInt64(stmt, col++);
        row.event_name = db::columnText(stmt, col++);
        fights.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return fights;
}

std::unordered_map<int64_t, std::vector<RoundStats>> loadRoundStatsByFight(sqlite3* db) {
    const char* sql =
        "SELECT id, fight_id, fighter_id, round_number, "
        "sig_strikes_landed, sig_strikes_attempted, "
        "total_strikes_landed, total_strikes_attempted, "
        "takedowns_landed, takedowns_attempted, sub_attempts, reversals, knockdowns, "
        "control_time_seconds, head_strikes_landed, body_strikes_landed, "
        "leg_strikes_landed, distance_strikes_landed, clinch_strikes_landed, "
        "ground_strikes_landed "
        "FROM round_stats "
        "ORDER BY fight_id, round_number, fighter_id";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return {};
    }

    std::unordered_map<int64_t, std::vector<RoundStats>> by_fight;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RoundStats row = roundStatsFromStatement(stmt);
        by_fight[row.fight_id].push_back(row);
    }
    sqlite3_finalize(stmt);
    return by_fight;
}

}  // namespace

bool PrefightMatchupIndex::build(sqlite3* db) {
    fights_.clear();
    vectors_by_fight_.clear();

    fights_ = loadFightsChronological(db);
    const std::unordered_map<int64_t, std::vector<RoundStats>> rounds_by_fight =
        loadRoundStatsByFight(db);

    std::unordered_map<int64_t, CareerAccumulator> career;
    std::unordered_map<int64_t, Fighter> fighter_cache;

    auto fighterFor = [&](int64_t id) -> const Fighter* {
        const auto cached = fighter_cache.find(id);
        if (cached != fighter_cache.end()) {
            return &cached->second;
        }
        const std::optional<Fighter> loaded = Fighter::getById(db, id);
        if (!loaded) {
            return nullptr;
        }
        fighter_cache.emplace(id, *loaded);
        return &fighter_cache.at(id);
    };

    for (const PrefightFightRecord& fight : fights_) {
        const Fighter* f1 = fighterFor(fight.fighter1_id);
        const Fighter* f2 = fighterFor(fight.fighter2_id);
        if (!f1 || !f2) {
            continue;
        }

        const FighterAttributes attrs1 = FighterAttributes::fromPrefight(
            *f1, career[fight.fighter1_id].toStats());
        const FighterAttributes attrs2 = FighterAttributes::fromPrefight(
            *f2, career[fight.fighter2_id].toStats());

        const int64_t low_id = std::min(fight.fighter1_id, fight.fighter2_id);
        const FighterAttributes& low_attrs =
            fight.fighter1_id == low_id ? attrs1 : attrs2;
        const FighterAttributes& high_attrs =
            fight.fighter1_id == low_id ? attrs2 : attrs1;
        vectors_by_fight_.emplace(
            fight.id, FighterAttributes::matchupVector(low_attrs, high_attrs));

        const auto stats_it = rounds_by_fight.find(fight.id);
        if (stats_it == rounds_by_fight.end()) {
            continue;
        }

        std::unordered_map<int64_t, std::unordered_map<int, const RoundStats*>> by_fighter_round;
        for (const RoundStats& row : stats_it->second) {
            by_fighter_round[row.fighter_id][row.round_number] = &row;
        }

        for (const auto& fighter_entry : by_fighter_round) {
            const int64_t fighter_id = fighter_entry.first;
            const int64_t opponent_id =
                fighter_id == fight.fighter1_id ? fight.fighter2_id : fight.fighter1_id;
            const auto opp_it = by_fighter_round.find(opponent_id);
            if (opp_it == by_fighter_round.end()) {
                continue;
            }
            for (const auto& round_entry : fighter_entry.second) {
                const int round_number = round_entry.first;
                const RoundStats* row = round_entry.second;
                const auto opp_round_it = opp_it->second.find(round_number);
                if (opp_round_it == opp_it->second.end()) {
                    continue;
                }
                career[fighter_id].addRound(*row, *opp_round_it->second);
            }
        }
    }

    return !fights_.empty();
}

const std::vector<double>* PrefightMatchupIndex::vectorForFight(int64_t fight_id) const {
    const auto it = vectors_by_fight_.find(fight_id);
    return it == vectors_by_fight_.end() ? nullptr : &it->second;
}

const PrefightMatchupIndex& getPrefightMatchupIndex(sqlite3* db) {
    static sqlite3* cached_db = nullptr;
    static PrefightMatchupIndex cached_index;
    if (db != cached_db) {
        cached_db = db;
        cached_index.build(db);
    }
    return cached_index;
}

}  // namespace ufc
