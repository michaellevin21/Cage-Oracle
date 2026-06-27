#include "ufc/JsonSerialize.hpp"

#include "ufc/AnalysisTypes.hpp"
#include "ufc/Fight.hpp"
#include "ufc/Fighter.hpp"
#include "ufc/Matchup.hpp"
#include "ufc/RoundStats.hpp"
#include "ufc/SimilaritySearch.hpp"

#include <sstream>
#include <iomanip>

namespace ufc::json {

namespace {

std::string quote(const std::string& value) {
    return "\"" + escape(value) + "\"";
}

std::string optString(const std::optional<std::string>& v) {
    return v ? quote(*v) : "null";
}

std::string optDouble(const std::optional<double>& v) {
    if (!v) return "null";
    std::ostringstream oss;
    oss << std::setprecision(17) << *v;
    return oss.str();
}

}  // namespace

std::string escape(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    std::ostringstream oss;
                    oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(static_cast<unsigned char>(ch));
                    out += oss.str();
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

std::string nullOrString(const std::optional<std::string>& value) {
    return value ? quote(*value) : "null";
}

std::string nullOrInt64(const std::optional<int64_t>& value) {
    return value ? std::to_string(*value) : "null";
}

std::string nullOrDouble(const std::optional<double>& value) {
    if (!value) {
        return "null";
    }
    std::ostringstream oss;
    oss << std::setprecision(17) << *value;
    return oss.str();
}

std::string toJson(const Fighter& f) {
    std::ostringstream oss;
    oss << '{'
        << "\"id\":" << f.id << ','
        << "\"ufc_id\":" << quote(f.ufc_id) << ','
        << "\"name\":" << quote(f.name) << ','
        << "\"stance\":" << nullOrString(f.stance) << ','
        << "\"reach_cm\":" << nullOrInt64(f.reach_cm) << ','
        << "\"height_cm\":" << nullOrInt64(f.height_cm) << ','
        << "\"weight_class\":" << quote(f.weight_class) << ','
        << "\"date_of_birth\":" << nullOrInt64(f.date_of_birth) << ','
        << "\"archetype\":" << nullOrString(f.archetype) << ','
        << "\"momentum_score\":" << nullOrDouble(f.momentum_score) << ','
        << "\"resume_score\":" << nullOrDouble(f.resume_score) << ','
        << "\"profile_url\":" << quote(f.profile_url) << ','
        << "\"last_updated\":" << f.last_updated
        << '}';
    return oss.str();
}

std::string toJson(const Fight& f) {
    std::ostringstream oss;
    oss << '{'
        << "\"id\":" << f.id << ','
        << "\"ufc_fight_id\":" << quote(f.ufc_fight_id) << ','
        << "\"fighter1_id\":" << f.fighter1_id << ','
        << "\"fighter2_id\":" << f.fighter2_id << ','
        << "\"event_id\":" << f.event_id << ','
        << "\"winner_id\":" << f.winner_id << ','
        << "\"result_method\":" << quote(f.result_method) << ','
        << "\"result_method_detail\":" << nullOrString(f.result_method_detail) << ','
        << "\"result_round\":" << f.result_round << ','
        << "\"result_time_seconds\":" << std::setprecision(17) << f.result_time_seconds << ','
        << "\"weight_class\":" << quote(f.weight_class) << ','
        << "\"is_title_fight\":" << (f.is_title_fight ? "true" : "false")
        << '}';
    return oss.str();
}

std::string toJson(const RoundStats& r) {
    std::ostringstream oss;
    oss << std::setprecision(17);
    oss << '{'
        << "\"id\":" << r.id << ','
        << "\"fight_id\":" << r.fight_id << ','
        << "\"fighter_id\":" << r.fighter_id << ','
        << "\"round_number\":" << r.round_number << ','
        << "\"sig_strikes_landed\":" << r.sig_strikes_landed << ','
        << "\"sig_strikes_attempted\":" << r.sig_strikes_attempted << ','
        << "\"total_strikes_landed\":" << r.total_strikes_landed << ','
        << "\"total_strikes_attempted\":" << r.total_strikes_attempted << ','
        << "\"takedowns_landed\":" << r.takedowns_landed << ','
        << "\"takedowns_attempted\":" << r.takedowns_attempted << ','
        << "\"sub_attempts\":" << r.sub_attempts << ','
        << "\"reversals\":" << r.reversals << ','
        << "\"knockdowns\":" << r.knockdowns << ','
        << "\"control_time_seconds\":" << r.control_time_seconds << ','
        << "\"head_strikes_landed\":" << r.head_strikes_landed << ','
        << "\"body_strikes_landed\":" << r.body_strikes_landed << ','
        << "\"leg_strikes_landed\":" << r.leg_strikes_landed << ','
        << "\"distance_strikes_landed\":" << r.distance_strikes_landed << ','
        << "\"clinch_strikes_landed\":" << r.clinch_strikes_landed << ','
        << "\"ground_strikes_landed\":" << r.ground_strikes_landed
        << '}';
    return oss.str();
}

std::string toJson(const StatComparison& c) {
    std::ostringstream oss;
    oss << std::setprecision(17);
    oss << '{'
        << "\"metric\":" << quote(c.metric) << ','
        << "\"fighter_a\":" << nullOrDouble(c.fighter_a) << ','
        << "\"fighter_b\":" << nullOrDouble(c.fighter_b) << ','
        << "\"delta\":" << nullOrDouble(c.delta) << ','
        << "\"advantage\":" << quote(toString(c.advantage)) << ','
        << "\"fighter_a_label\":" << nullOrString(c.fighter_a_label) << ','
        << "\"fighter_b_label\":" << nullOrString(c.fighter_b_label)
        << '}';
    return oss.str();
}

std::string toJson(const Matchup& m) {
    std::ostringstream oss;
    oss << '{'
        << "\"fighter_a\":" << toJson(m.fighter_a) << ','
        << "\"fighter_b\":" << toJson(m.fighter_b) << ','
        << "\"comparisons\":[";
    const auto& rows = m.comparisons();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << toJson(rows[i]);
    }
    oss << "]}";
    return oss.str();
}

std::string toJsonArray(const std::vector<Fight>& fights) {
    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < fights.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << toJson(fights[i]);
    }
    oss << ']';
    return oss.str();
}

std::string toJsonArray(const std::vector<RoundStats>& stats) {
    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < stats.size(); ++i) {
        if (i > 0) {
            oss << ',';
        }
        oss << toJson(stats[i]);
    }
    oss << ']';
    return oss.str();
}

std::string toJsonSimilarMatchups(sqlite3* db, int64_t fighter_a_id, int64_t fighter_b_id, const SimilarMatchupResults& results) {
    auto writeHit = [&](std::ostringstream& oss, const SimilarMatchupHit& hit, bool include_similarity) {
        std::string fighter1_name;
        std::string fighter2_name;
        if (db) {
            if (const std::optional<Fighter> f1 = Fighter::getById(db, hit.fighter1_id)) {
                fighter1_name = f1->name;
            }
            if (const std::optional<Fighter> f2 = Fighter::getById(db, hit.fighter2_id)) {
                fighter2_name = f2->name;
            }
        }
        oss << '{'
            << "\"fight_id\":" << hit.fight_id << ','
            << "\"fighter1_id\":" << hit.fighter1_id << ','
            << "\"fighter2_id\":" << hit.fighter2_id << ','
            << "\"fighter1_name\":" << quote(fighter1_name) << ','
            << "\"fighter2_name\":" << quote(fighter2_name) << ','
            << "\"event_id\":" << hit.event_id << ','
            << "\"event_name\":" << quote(hit.event_name) << ','
            << "\"event_date\":" << hit.event_date << ','
            << "\"winner_id\":" << (hit.winner_id > 0 ? std::to_string(hit.winner_id) : "null");
        if (include_similarity) {
            oss << ",\"similarity\":" << hit.similarity;
        }
        oss << '}';
    };

    auto writeArray = [&](const char* key, const std::vector<SimilarMatchupHit>& hits, bool include_similarity) {
        std::ostringstream section;
        section << '"' << key << "\":[";
        for (size_t i = 0; i < hits.size(); ++i) {
            if (i > 0) {
                section << ',';
            }
            writeHit(section, hits[i], include_similarity);
        }
        section << ']';
        return section.str();
    };

    std::ostringstream oss;
    oss << std::setprecision(17);
    oss << "{\"fighter_a_id\":" << fighter_a_id << ','
        << "\"fighter_b_id\":" << fighter_b_id << ','
        << writeArray("prior_meetings", results.prior_meetings, true) << ','
        << writeArray("similar_matchups", results.similar_matchups, true)
        << '}';
    return oss.str();
}

std::string toJson(const ComparisonRow& row) {
    std::ostringstream oss;
    oss << '{'
        << "\"metric\":" << quote(row.metric) << ','
        << "\"label\":" << quote(row.label) << ','
        << "\"fighter_a\":" << quote(row.fighter_a) << ','
        << "\"fighter_b\":" << quote(row.fighter_b) << ','
        << "\"advantage\":" << optString(row.advantage) << ','
        << "\"edge\":" << quote(row.edge)
        << '}';
    return oss.str();
}

std::string toJson(const Prediction& p) {
    std::ostringstream oss;
    oss << std::setprecision(17);
    oss << '{'
        << "\"type\":" << quote(p.type) << ','
        << "\"winner_name\":" << optString(p.winner_name) << ','
        << "\"p_fighter_a\":" << p.p_fighter_a << ','
        << "\"p_fighter_b\":" << p.p_fighter_b << ','
        << "\"certainty_pct\":" << p.certainty_pct
        << '}';
    return oss.str();
}

std::string toJson(const TaleOfTheTape& tape) {
    auto writeRows = [](const std::vector<ComparisonRow>& rows) {
        std::ostringstream oss;
        oss << '[';
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i > 0) oss << ',';
            oss << toJson(rows[i]);
        }
        oss << ']';
        return oss.str();
    };
    auto writeSummaries = [](const std::vector<std::string>& items) {
        std::ostringstream oss;
        oss << '[';
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) oss << ',';
            oss << quote(items[i]);
        }
        oss << ']';
        return oss.str();
    };
    std::ostringstream oss;
    oss << '{'
        << "\"fighter_a\":{\"name\":" << quote(tape.fighter_a.name)
        << ",\"weight_class\":" << optString(tape.fighter_a.weight_class) << "},"
        << "\"fighter_b\":{\"name\":" << quote(tape.fighter_b.name)
        << ",\"weight_class\":" << optString(tape.fighter_b.weight_class) << "},"
        << "\"profile\":" << writeRows(tape.profile) << ','
        << "\"career\":" << writeRows(tape.career) << ','
        << "\"archetype_summaries\":" << writeSummaries(tape.archetype_summaries) << ','
        << "\"prediction\":" << (tape.prediction ? toJson(*tape.prediction) : "null")
        << '}';
    return oss.str();
}

std::string toJson(const HistoryFight& h) {
    std::ostringstream oss;
    oss << std::setprecision(17);
    oss << '{'
        << "\"fighter1_name\":" << quote(h.fighter1_name) << ','
        << "\"fighter2_name\":" << quote(h.fighter2_name) << ','
        << "\"fighter1_id\":" << h.fighter1_id << ','
        << "\"fighter2_id\":" << h.fighter2_id << ','
        << "\"winner_id\":" << (h.winner_id > 0 ? std::to_string(h.winner_id) : "null") << ','
        << "\"fighter1_won\":" << (h.fighter1_won ? "true" : "false") << ','
        << "\"fighter2_won\":" << (h.fighter2_won ? "true" : "false") << ','
        << "\"event_name\":" << quote(h.event_name) << ','
        << "\"event_date\":" << optString(h.event_date) << ','
        << "\"similarity\":" << optDouble(h.similarity) << ','
        << "\"similarity_pct\":" << optDouble(h.similarity_pct)
        << '}';
    return oss.str();
}

std::string toJson(const MomentumBreakdown& m) {
    std::ostringstream oss;
    oss << std::setprecision(17);
    oss << '{'
        << "\"status\":" << quote(m.status) << ','
        << "\"score\":" << optDouble(m.score) << ','
        << "\"days_since_last_fight\":" << optDouble(m.days_since_last_fight) << ','
        << "\"min_decisive_fights\":" << m.min_decisive_fights << ','
        << "\"inactivity_days\":" << m.inactivity_days << ','
        << "\"fights\":[";
    for (size_t i = 0; i < m.fights.size(); ++i) {
        if (i > 0) oss << ',';
        const MomentumFight& f = m.fights[i];
        oss << '{'
            << "\"event_date\":" << quote(f.event_date) << ','
            << "\"event_name\":" << quote(f.event_name) << ','
            << "\"opponent_name\":" << quote(f.opponent_name) << ','
            << "\"result\":" << quote(f.result) << ','
            << "\"result_method\":" << quote(f.result_method) << ','
            << "\"opponent_rank_label\":" << quote(f.opponent_rank_label) << ','
            << "\"recency\":" << f.recency << ','
            << "\"opp_quality\":" << f.opp_quality << ','
            << "\"finish_mult\":" << f.finish_mult << ','
            << "\"contribution\":" << f.contribution << ','
            << "\"weighted_contribution\":" << f.weighted_contribution
            << '}';
    }
    oss << ']';
    if (m.weighted_average) {
        oss << ",\"weighted_average\":" << *m.weighted_average;
    }
    if (m.finish_rate) {
        oss << ",\"finish_rate\":" << *m.finish_rate;
    }
    if (m.finish_boost) {
        oss << ",\"finish_boost\":" << *m.finish_boost;
    }
    if (m.adjusted) {
        oss << ",\"adjusted\":" << *m.adjusted;
    }
    if (m.neutral_score) {
        oss << ",\"neutral_score\":" << *m.neutral_score;
    }
    if (m.max_fight_contribution) {
        oss << ",\"max_fight_contribution\":" << *m.max_fight_contribution;
    }
    oss << '}';
    return oss.str();
}

std::string toJson(const ResumeBreakdown& r) {
    std::ostringstream oss;
    oss << '{'
        << "\"score\":" << r.score << ','
        << "\"ranked_wins\":[";
    for (size_t i = 0; i < r.ranked_wins.size(); ++i) {
        if (i > 0) oss << ',';
        const ResumeWin& w = r.ranked_wins[i];
        oss << '{'
            << "\"event_date\":" << quote(w.event_date) << ','
            << "\"event_name\":" << quote(w.event_name) << ','
            << "\"opponent_name\":" << quote(w.opponent_name) << ','
            << "\"result_method\":" << quote(w.result_method) << ','
            << "\"opponent_rank_label\":" << quote(w.opponent_rank_label) << ','
            << "\"points\":" << w.points << ','
            << "\"running_total\":" << w.running_total
            << '}';
    }
    oss << "],\"unranked_win_count\":" << r.unranked_win_count
        << ",\"skipped_non_decisive\":" << r.skipped_non_decisive
        << '}';
    return oss.str();
}

std::string toJson(const MatchupResponse& response) {
    std::ostringstream oss;
    oss << '{'
        << "\"tape\":" << toJson(response.tape) << ','
        << "\"history\":{"
        << "\"prior_meetings\":[";
    for (size_t i = 0; i < response.history.prior_meetings.size(); ++i) {
        if (i > 0) oss << ',';
        oss << toJson(response.history.prior_meetings[i]);
    }
    oss << "],\"similar_matchups\":[";
    for (size_t i = 0; i < response.history.similar_matchups.size(); ++i) {
        if (i > 0) oss << ',';
        oss << toJson(response.history.similar_matchups[i]);
    }
    oss << "]}," 
        << "\"no_prediction_reason\":" << optString(response.no_prediction_reason) << ','
        << "\"resume_breakdown\":{"
        << "\"fighter_a\":" << toJson(response.resume_breakdown.fighter_a) << ','
        << "\"fighter_b\":" << toJson(response.resume_breakdown.fighter_b)
        << "},"
        << "\"momentum_breakdown\":{"
        << "\"fighter_a\":" << toJson(response.momentum_breakdown.fighter_a) << ','
        << "\"fighter_b\":" << toJson(response.momentum_breakdown.fighter_b)
        << "}}";
    return oss.str();
}

std::string toJsonArray(const std::vector<FighterSummary>& fighters) {
    std::ostringstream oss;
    oss << '[';
    for (size_t i = 0; i < fighters.size(); ++i) {
        if (i > 0) oss << ',';
        oss << '{'
            << "\"name\":" << quote(fighters[i].name) << ','
            << "\"weight_class\":" << optString(fighters[i].weight_class)
            << '}';
    }
    oss << ']';
    return oss.str();
}

}  // namespace ufc::json
