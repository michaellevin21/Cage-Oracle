#include "ufc/SimilaritySearch.hpp"

#include "ufc/Fighter.hpp"
#include "ufc/FighterAttributes.hpp"
#include "ufc/PrefightCareer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ufc {

namespace {

constexpr double kEpsilon = 1e-9;

bool featuresQueryFighter(const PrefightFightRecord& fight, int64_t fighter_a_id, int64_t fighter_b_id) {
    return fight.fighter1_id == fighter_a_id || fight.fighter2_id == fighter_a_id
        || fight.fighter1_id == fighter_b_id || fight.fighter2_id == fighter_b_id;
}

bool isPriorMeeting(const PrefightFightRecord& fight, int64_t fighter_a_id, int64_t fighter_b_id) {
    return (fight.fighter1_id == fighter_a_id && fight.fighter2_id == fighter_b_id)
        || (fight.fighter1_id == fighter_b_id && fight.fighter2_id == fighter_a_id);
}

SimilarMatchupHit makeHit(const PrefightFightRecord& fight, double similarity) {
    SimilarMatchupHit hit;
    hit.fight_id = fight.id;
    hit.fighter1_id = fight.fighter1_id;
    hit.fighter2_id = fight.fighter2_id;
    hit.event_id = fight.event_id;
    hit.event_date = fight.event_date;
    hit.winner_id = fight.winner_id;
    hit.event_name = fight.event_name;
    hit.similarity = similarity;
    return hit;
}

}  // namespace

AttributeNormalization fitNormalization(const std::vector<std::vector<double>>& corpus) {
    AttributeNormalization params;
    if (corpus.empty()) {
        return params;
    }

    const size_t dim = corpus.front().size();
    params.mean.assign(dim, 0.0);
    params.stddev.assign(dim, 1.0);

    std::vector<int> counts(dim, 0);
    for (const std::vector<double>& row : corpus) {
        for (size_t i = 0; i < dim && i < row.size(); ++i) {
            if (std::isfinite(row[i])) {
                params.mean[i] += row[i];
                counts[static_cast<int>(i)] += 1;
            }
        }
    }

    for (size_t i = 0; i < dim; ++i) {
        if (counts[i] > 0) {
            params.mean[i] /= static_cast<double>(counts[i]);
        }
    }

    std::vector<double> variance(dim, 0.0);
    for (const std::vector<double>& row : corpus) {
        for (size_t i = 0; i < dim && i < row.size(); ++i) {
            if (std::isfinite(row[i]) && counts[i] > 1) {
                const double delta = row[i] - params.mean[i];
                variance[i] += delta * delta;
            }
        }
    }

    for (size_t i = 0; i < dim; ++i) {
        if (counts[i] > 1) {
            variance[i] /= static_cast<double>(counts[i] - 1);
            params.stddev[i] = std::sqrt(variance[i]);
        }
        if (params.stddev[i] < kEpsilon) {
            params.stddev[i] = 1.0;
        }
    }

    return params;
}

std::vector<double> normalizeVector(const std::vector<double>& raw, const AttributeNormalization& params) {
    std::vector<double> out(raw.size(), 0.0);
    for (size_t i = 0; i < raw.size(); ++i) {
        const double value = raw[i];
        if (!std::isfinite(value) || i >= params.mean.size()) {
            out[i] = 0.0;
            continue;
        }
        const double mean = params.mean[i];
        const double stddev = i < params.stddev.size() ? params.stddev[i] : 1.0;
        out[i] = (value - mean) / stddev;
    }
    return out;
}

double cosineSimilarity(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size() || a.empty()) {
        return 0.0;
    }

    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }

    if (norm_a < kEpsilon || norm_b < kEpsilon) {
        return 0.0;
    }
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

std::vector<double> prepareMatchupVector(const std::vector<double>& raw, const AttributeNormalization& norm) {
    std::vector<double> vec = normalizeVector(raw, norm);
    FighterAttributes::applyMatchupFeatureWeights(vec);
    return vec;
}

SimilarMatchupResults findSimilarHistoricalMatchups(
    sqlite3* db,
    int64_t fighter_a_id,
    int64_t fighter_b_id,
    double min_similarity) {
    SimilarMatchupResults results;
    if (fighter_a_id == fighter_b_id || min_similarity < 0.0) {
        return results;
    }
    if (!Fighter::getById(db, fighter_a_id) || !Fighter::getById(db, fighter_b_id)) {
        return results;
    }

    const PrefightMatchupIndex& prefight_index = getPrefightMatchupIndex(db);
    if (prefight_index.empty()) {
        return results;
    }

    std::vector<std::vector<double>> matchup_corpus;
    matchup_corpus.reserve(prefight_index.fights().size());
    for (const PrefightFightRecord& fight : prefight_index.fights()) {
        if (!featuresQueryFighter(fight, fighter_a_id, fighter_b_id)) {
            continue;
        }
        const std::vector<double>* vec = prefight_index.vectorForFight(fight.id);
        if (vec && !vec->empty()) {
            matchup_corpus.push_back(*vec);
        }
    }

    const AttributeNormalization matchup_norm = fitNormalization(matchup_corpus);

    const FighterAttributes query_low = FighterAttributes::fromFighter(
        db, fighter_a_id < fighter_b_id ? fighter_a_id : fighter_b_id);
    const FighterAttributes query_high = FighterAttributes::fromFighter(
        db, fighter_a_id < fighter_b_id ? fighter_b_id : fighter_a_id);
    const std::vector<double> query_raw =
        FighterAttributes::matchupVector(query_low, query_high);
    if (query_raw.empty()) {
        return results;
    }
    std::vector<double> query_vec = prepareMatchupVector(query_raw, matchup_norm);

    for (const PrefightFightRecord& fight : prefight_index.fights()) {
        if (!featuresQueryFighter(fight, fighter_a_id, fighter_b_id)) {
            continue;
        }

        const std::vector<double>* raw = prefight_index.vectorForFight(fight.id);
        if (!raw || raw->empty()) {
            continue;
        }

        const double similarity =
            cosineSimilarity(query_vec, prepareMatchupVector(*raw, matchup_norm));
        const bool prior = isPriorMeeting(fight, fighter_a_id, fighter_b_id);
        SimilarMatchupHit hit = makeHit(fight, similarity);

        if (prior) {
            results.prior_meetings.push_back(std::move(hit));
        } else if (similarity >= min_similarity) {
            results.similar_matchups.push_back(std::move(hit));
        }
    }

    std::sort(results.similar_matchups.begin(), results.similar_matchups.end(),
        [](const SimilarMatchupHit& a, const SimilarMatchupHit& b) {
            if (a.similarity != b.similarity) {
                return a.similarity > b.similarity;
            }
            return a.fight_id < b.fight_id;
        });

    std::sort(results.prior_meetings.begin(), results.prior_meetings.end(),
        [](const SimilarMatchupHit& a, const SimilarMatchupHit& b) {
            return a.event_date > b.event_date;
        });

    return results;
}

}  // namespace ufc
