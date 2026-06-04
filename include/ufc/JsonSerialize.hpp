#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace ufc {

class Fighter;
class Fight;
class RoundStats;
struct StatComparison;
class Matchup;
struct SimilarMatchupResults;

namespace json {

std::string escape(const std::string& value);
std::string nullOrString(const std::optional<std::string>& value);
std::string nullOrInt64(const std::optional<int64_t>& value);
std::string nullOrDouble(const std::optional<double>& value);

std::string toJson(const Fighter& fighter);
std::string toJson(const Fight& fight);
std::string toJson(const RoundStats& stats);
std::string toJson(const StatComparison& comparison);
std::string toJson(const Matchup& matchup);

std::string toJsonArray(const std::vector<Fight>& fights);
std::string toJsonArray(const std::vector<RoundStats>& stats);

std::string toJsonSimilarMatchups(sqlite3* db, int64_t fighter_a_id, int64_t fighter_b_id, const SimilarMatchupResults& results);

}  // namespace json
}  // namespace ufc
