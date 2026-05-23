#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

namespace ufc {

/// Row from the `fighters` table (UfcDbSchema.sql).
class Fighter {
public:
    int64_t id = 0;
    std::string ufc_id;
    std::string name;
    std::optional<std::string> stance;
    std::optional<int64_t> reach_cm;
    std::optional<int64_t> height_cm;
    std::string weight_class;
    std::optional<int64_t> date_of_birth;  // Unix timestamp
    std::optional<std::string> archetype;
    std::optional<double> momentum_score;
    std::string profile_url;
    int64_t last_updated = 0;  // Unix timestamp

    static std::optional<Fighter> getById(sqlite3* db, int64_t id);
    static std::optional<Fighter> getByUfcId(sqlite3* db, const std::string& ufc_id);
    static std::optional<Fighter> getByName(sqlite3* db, const std::string& name);

private:
    static Fighter fromStatement(sqlite3_stmt* stmt);
};

}  // namespace ufc
