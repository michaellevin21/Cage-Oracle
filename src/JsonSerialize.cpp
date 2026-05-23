#include "ufc/JsonSerialize.hpp"

#include "ufc/Fight.hpp"
#include "ufc/Fighter.hpp"
#include "ufc/Matchup.hpp"
#include "ufc/RoundStats.hpp"

#include <sstream>
#include <iomanip>

namespace ufc::json {

namespace {

std::string quote(const std::string& value) {
    return "\"" + escape(value) + "\"";
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

}  // namespace ufc::json
