#pragma once

#include <string>

struct sqlite3;

namespace ufc {

/// Owns a SQLite connection (one per process / FFI handle).
class UfcDatabase {
public:
    explicit UfcDatabase(const std::string& path);
    ~UfcDatabase();

    UfcDatabase(const UfcDatabase&) = delete;
    UfcDatabase& operator=(const UfcDatabase&) = delete;

    sqlite3* handle() const noexcept { return db_; }
    const std::string& lastError() const noexcept { return last_error_; }

private:
    void applyPragmas();

    sqlite3* db_ = nullptr;
    std::string last_error_;
};

}  // namespace ufc
