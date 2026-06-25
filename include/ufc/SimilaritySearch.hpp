#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace ufc {

struct SimilarMatchupHit {
    int64_t fight_id = 0;
    int64_t fighter1_id = 0;
    int64_t fighter2_id = 0;
    int64_t event_id = 0;
    int64_t event_date = 0;
    int64_t winner_id = 0;
    std::string event_name;
    double similarity = 0.0;
};

struct SimilarMatchupResults {
    std::vector<SimilarMatchupHit> prior_meetings;
    std::vector<SimilarMatchupHit> similar_matchups;
};

struct AttributeNormalization {
    std::vector<double> mean;
    std::vector<double> stddev;
};

AttributeNormalization fitNormalization(const std::vector<std::vector<double>>& corpus);
std::vector<double> normalizeVector(const std::vector<double>& raw, const AttributeNormalization& params);
double cosineSimilarity(const std::vector<double>& a, const std::vector<double>& b);

SimilarMatchupResults findSimilarHistoricalMatchups(
    sqlite3* db,
    int64_t fighter_a_id,
    int64_t fighter_b_id,
    double min_similarity = 0.50);

}  // namespace ufc
