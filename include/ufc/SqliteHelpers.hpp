#pragma once

#include <optional>
#include <string>

#include <sqlite3.h>

namespace ufc::db {

inline std::optional<std::string> columnTextOptional(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    const unsigned char* text = sqlite3_column_text(stmt, col);
    if (!text) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(text));
}

inline std::string columnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? std::string(reinterpret_cast<const char*>(text)) : std::string{};
}

inline std::optional<int64_t> columnInt64Optional(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_int64(stmt, col);
}

inline std::optional<double> columnDoubleOptional(sqlite3_stmt* stmt, int col) {
    if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
        return std::nullopt;
    }
    return sqlite3_column_double(stmt, col);
}

inline int64_t columnInt64(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_int64(stmt, col);
}

inline int columnInt(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_int(stmt, col);
}

inline double columnDouble(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_double(stmt, col);
}

inline bool columnBool(sqlite3_stmt* stmt, int col) {
    return sqlite3_column_int(stmt, col) != 0;
}

}  // namespace ufc::db
