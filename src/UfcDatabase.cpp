#include "ufc/UfcDatabase.hpp"

#include <sqlite3.h>

namespace ufc {

UfcDatabase::UfcDatabase(const std::string& path) {
    const int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX;
    const int rc = sqlite3_open_v2(path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        if (db_) {
            last_error_ = sqlite3_errmsg(db_);
            sqlite3_close(db_);
            db_ = nullptr;
        } else {
            last_error_ = "sqlite3_open_v2 failed";
        }
        return;
    }
    applyPragmas();
}

UfcDatabase::~UfcDatabase() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void UfcDatabase::applyPragmas() {
    char* err = nullptr;
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON", nullptr, nullptr, &err);
    if (err) {
        sqlite3_free(err);
    }
}

}  // namespace ufc
