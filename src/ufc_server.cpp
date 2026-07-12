#include "ufc/ArchetypeMatchupHistory.hpp"
#include "ufc/MatchupAnalysis.hpp"
#include "ufc/JsonSerialize.hpp"
#include "ufc/PrefightCareer.hpp"
#include "ufc/UfcDatabase.hpp"

#include "../third_party/httplib.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = value[i + 1];
            const int lo = value[i + 2];
            auto hex = [](int c) {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                return -1;
            };
            const int h = hex(hi);
            const int l = hex(lo);
            if (h >= 0 && l >= 0) {
                out.push_back(static_cast<char>((h << 4) | l));
                i += 2;
                continue;
            }
        } else if (value[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(value[i]);
    }
    return out;
}

void printUsage() {
    std::cerr << "Usage: ufc_server [--db PATH] [--port N] [--static DIR]\n"
              << "  --db PATH     SQLite database (default: ufc.db in cwd)\n"
              << "  --port N      HTTP port (default: 8000)\n"
              << "  --static DIR  Frontend dist directory (default: frontend/dist)\n";
}

std::string readTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to read file: " + path.string());
    }
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

void appendUtf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

unsigned int parseHex4(const std::string& s, size_t pos) {
    unsigned int value = 0;
    for (int i = 0; i < 4 && pos + static_cast<size_t>(i) < s.size(); ++i) {
        const char c = s[pos + static_cast<size_t>(i)];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= static_cast<unsigned int>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= static_cast<unsigned int>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= static_cast<unsigned int>(c - 'A' + 10);
        }
    }
    return value;
}

// Minimal, targeted extraction of the string values for a given JSON key.
// The upcoming cache is produced by Python's json.dumps (ensure_ascii=True),
// so non-ASCII names arrive as \uXXXX escapes that must be decoded to UTF-8
// to match the UTF-8 fighter names sent by the browser.
std::vector<std::string> extractJsonStringValues(const std::string& json, const std::string& key) {
    std::vector<std::string> out;
    const std::string needle = "\"" + key + "\"";
    auto skipWs = [&](size_t& p) {
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t'
                                   || json[p] == '\n' || json[p] == '\r')) {
            ++p;
        }
    };

    size_t pos = 0;
    while ((pos = json.find(needle, pos)) != std::string::npos) {
        pos += needle.size();
        skipWs(pos);
        if (pos >= json.size() || json[pos] != ':') {
            continue;
        }
        ++pos;
        skipWs(pos);
        if (pos >= json.size() || json[pos] != '"') {
            continue;
        }
        ++pos;

        std::string value;
        while (pos < json.size() && json[pos] != '"') {
            const char c = json[pos];
            if (c == '\\' && pos + 1 < json.size()) {
                const char esc = json[pos + 1];
                switch (esc) {
                    case '"': value.push_back('"'); pos += 2; break;
                    case '\\': value.push_back('\\'); pos += 2; break;
                    case '/': value.push_back('/'); pos += 2; break;
                    case 'n': value.push_back('\n'); pos += 2; break;
                    case 't': value.push_back('\t'); pos += 2; break;
                    case 'r': value.push_back('\r'); pos += 2; break;
                    case 'b': value.push_back('\b'); pos += 2; break;
                    case 'f': value.push_back('\f'); pos += 2; break;
                    case 'u': {
                        unsigned int cp = parseHex4(json, pos + 2);
                        pos += 6;
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos + 1 < json.size()
                            && json[pos] == '\\' && json[pos + 1] == 'u') {
                            const unsigned int lo = parseHex4(json, pos + 2);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                pos += 6;
                            }
                        }
                        appendUtf8(value, cp);
                        break;
                    }
                    default: value.push_back(esc); pos += 2; break;
                }
            } else {
                value.push_back(c);
                ++pos;
            }
        }
        if (pos < json.size() && json[pos] == '"') {
            ++pos;
        }
        out.push_back(std::move(value));
    }
    return out;
}

std::string normalizeName(std::string s) {
    size_t start = 0;
    size_t end = s.size();
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r';
    };
    while (start < end && isSpace(s[start])) {
        ++start;
    }
    while (end > start && isSpace(s[end - 1])) {
        --end;
    }
    s = s.substr(start, end - start);
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return s;
}

// Order-independent key so (A, B) and (B, A) map to the same matchup.
std::string matchupPairKey(const std::string& a, const std::string& b) {
    std::string na = normalizeName(a);
    std::string nb = normalizeName(b);
    if (nb < na) {
        std::swap(na, nb);
    }
    na.push_back('\x1f');
    na.append(nb);
    return na;
}

struct UpcomingLookup {
    bool is_upcoming = false;
    // Opaque token identifying the current upcoming-cache contents; changes
    // whenever the cache file is regenerated. Empty when no cache exists.
    std::string version;
};

// Thread-safe cache of the fighter pairs listed on upcoming cards, reloaded
// whenever the underlying cache file changes on disk.
class UpcomingMatchupSet {
public:
    UpcomingLookup lookup(const std::string& a, const std::string& b) {
        std::lock_guard<std::mutex> lock(mutex_);
        refreshLocked();
        UpcomingLookup result;
        result.is_upcoming = pairs_.count(matchupPairKey(a, b)) > 0;
        result.version = version_;
        return result;
    }

private:
    void refreshLocked() {
        const fs::path path = fs::current_path() / ".upcoming_matchups_cache.json";
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            pairs_.clear();
            version_.clear();
            loaded_ = false;
            return;
        }
        const fs::file_time_type mtime = fs::last_write_time(path, ec);
        if (ec) {
            return;
        }
        if (loaded_ && mtime == mtime_) {
            return;
        }

        pairs_.clear();
        try {
            const std::string json = readTextFile(path);
            const std::vector<std::string> as = extractJsonStringValues(json, "fighter_a");
            const std::vector<std::string> bs = extractJsonStringValues(json, "fighter_b");
            const size_t n = std::min(as.size(), bs.size());
            for (size_t i = 0; i < n; ++i) {
                pairs_.insert(matchupPairKey(as[i], bs[i]));
            }
        } catch (const std::exception&) {
            // Leave the set empty on failure; matchups simply won't be tagged.
        }
        mtime_ = mtime;
        version_ = std::to_string(
            mtime.time_since_epoch().count());
        loaded_ = true;
    }

    std::mutex mutex_;
    std::set<std::string> pairs_;
    fs::file_time_type mtime_{};
    std::string version_;
    bool loaded_ = false;
};

std::string trimCopy(const std::string& s) {
    size_t start = 0;
    size_t end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        --end;
    }
    return s.substr(start, end - start);
}

// Render and other reverse proxies set X-Forwarded-For; fall back to the socket peer.
std::string clientIp(const httplib::Request& req) {
    if (req.has_header("X-Forwarded-For")) {
        const std::string xff = req.get_header_value("X-Forwarded-For");
        const size_t comma = xff.find(',');
        const std::string ip = trimCopy(
            comma == std::string::npos ? xff : xff.substr(0, comma));
        if (!ip.empty()) {
            return ip;
        }
    }
    return req.remote_addr;
}

// Fixed-window per-IP limiter for the expensive matchup endpoint.
class MatchupRateLimiter {
public:
    struct Result {
        bool allowed = true;
        int retry_after_seconds = 0;
    };

    Result tryAcquire(const std::string& client_ip) {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex_);
        pruneStale(now);

        Bucket& bucket = buckets_[client_ip];
        if (bucket.window_start == std::chrono::steady_clock::time_point{}
            || now - bucket.window_start >= kWindow) {
            bucket.window_start = now;
            bucket.count = 0;
        }

        if (bucket.count >= kMaxRequests) {
            const auto remaining = kWindow - (now - bucket.window_start);
            const int retry = std::max(
                1,
                static_cast<int>(
                    std::chrono::duration_cast<std::chrono::seconds>(remaining).count()));
            return {false, retry};
        }

        ++bucket.count;
        return {true, 0};
    }

private:
    struct Bucket {
        std::chrono::steady_clock::time_point window_start{};
        int count = 0;
    };

    void pruneStale(const std::chrono::steady_clock::time_point now) {
        if (now - last_prune_ < std::chrono::minutes(5)) {
            return;
        }
        last_prune_ = now;
        for (auto it = buckets_.begin(); it != buckets_.end();) {
            if (it->second.window_start == std::chrono::steady_clock::time_point{}
                || now - it->second.window_start > kWindow * 2) {
                it = buckets_.erase(it);
            } else {
                ++it;
            }
        }
    }

    static constexpr int kMaxRequests = 15;
    static constexpr std::chrono::seconds kWindow{60};

    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
    std::chrono::steady_clock::time_point last_prune_{};
};

}  // namespace

int main(int argc, char* argv[]) {
    fs::path db_path = "ufc.db";
    fs::path static_dir = "frontend/dist";
    int port = 8000;
    bool port_from_args = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
            port_from_args = true;
        } else if (arg == "--static" && i + 1 < argc) {
            static_dir = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage();
            return 1;
        }
    }

    if (!port_from_args) {
        if (const char* port_env = std::getenv("PORT")) {
            try {
                port = std::stoi(port_env);
            } catch (...) {
                std::cerr << "Invalid PORT environment variable: " << port_env << '\n';
                return 1;
            }
        }
    }

    if (!fs::exists(db_path)) {
        std::cerr << "Database not found: " << db_path << '\n';
        return 1;
    }

    ufc::UfcDatabase database(db_path.string());
    if (!database.handle()) {
        std::cerr << "Failed to open database: " << database.lastError() << '\n';
        return 1;
    }

    sqlite3* db = database.handle();
    const std::string db_path_str = fs::absolute(db_path).string();
    const bool static_exists = fs::is_directory(static_dir);

    httplib::Server server;
    UpcomingMatchupSet upcoming_pairs;
    MatchupRateLimiter matchup_limiter;

    server.set_default_headers({
        {"Access-Control-Allow-Origin", "*"},
        {"Access-Control-Allow-Methods", "GET, OPTIONS"},
        {"Access-Control-Allow-Headers", "*"},
    });

    server.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
        std::ostringstream oss;
        oss << "{\"status\":\"ok\",\"database\":\"" << ufc::json::escape(db_path_str)
            << "\",\"database_exists\":\"" << (fs::exists(db_path) ? "True" : "False") << "\"}";
        res.set_content(oss.str(), "application/json");
    });

    server.Get("/api/fighters/search", [&](const httplib::Request& req, httplib::Response& res) {
        std::string q;
        int limit = 20;
        if (req.has_param("q")) {
            q = urlDecode(req.get_param_value("q"));
        }
        if (req.has_param("limit")) {
            try {
                limit = std::stoi(req.get_param_value("limit"));
            } catch (...) {
                limit = 20;
            }
            limit = std::max(1, std::min(limit, 50));
        }
        const auto fighters = ufc::searchFighters(db, q, limit);
        res.set_content(ufc::json::toJsonArray(fighters), "application/json");
    });

    server.Get("/api/upcoming/matchups", [&](const httplib::Request&, httplib::Response& res) {
        const fs::path cache_path = fs::current_path() / ".upcoming_matchups_cache.json";
        if (!fs::exists(cache_path)) {
            res.status = 503;
            res.set_content(
                "{\"detail\":\"Upcoming matchups cache not found. Run: python upcoming_matchups.py "
                "--json --output .upcoming_matchups_cache.json\",\"events\":[]}",
                "application/json");
            return;
        }
        try {
            res.set_content(readTextFile(cache_path), "application/json");
        } catch (const std::exception& ex) {
            res.status = 503;
            std::ostringstream oss;
            oss << "{\"detail\":\"" << ufc::json::escape(ex.what()) << "\",\"events\":[]}";
            res.set_content(oss.str(), "application/json");
        }
    });

    server.Get("/api/matchup", [&](const httplib::Request& req, httplib::Response& res) {
        if (!req.has_param("fighter_a") || !req.has_param("fighter_b")) {
            res.status = 422;
            res.set_content("{\"detail\":\"fighter_a and fighter_b are required\"}", "application/json");
            return;
        }
        const std::string fighter_a = urlDecode(req.get_param_value("fighter_a"));
        const std::string fighter_b = urlDecode(req.get_param_value("fighter_b"));
        if (fighter_a.empty() || fighter_b.empty()) {
            res.status = 422;
            res.set_content("{\"detail\":\"fighter names cannot be empty\"}", "application/json");
            return;
        }

        // Matchups shown in the Upcoming Fights section are a small, stable set
        // that many visitors open. The analysis is deterministic for a given
        // upcoming-cache version, so tag responses with an ETag and let the
        // browser keep its copy indefinitely, revalidating cheaply. When the
        // upcoming cache is regenerated the ETag changes and clients refetch.
        const UpcomingLookup up = upcoming_pairs.lookup(fighter_a, fighter_b);
        std::string etag;
        if (up.is_upcoming && !up.version.empty()) {
            etag = "\"" + up.version + "\"";
            if (req.has_header("If-None-Match")
                && req.get_header_value("If-None-Match") == etag) {
                res.status = 304;
                res.set_header("ETag", etag);
                res.set_header("Cache-Control", "no-cache");
                return;
            }
        }

        // Cheap ETag revalidations (304 above) are exempt; full analyses are capped
        // per client IP so one caller cannot monopolize the single-process server.
        const MatchupRateLimiter::Result limit = matchup_limiter.tryAcquire(clientIp(req));
        if (!limit.allowed) {
            res.status = 429;
            res.set_header("Retry-After", std::to_string(limit.retry_after_seconds));
            res.set_content(
                "{\"detail\":\"Too many matchup requests. Please try again shortly.\"}",
                "application/json");
            return;
        }

        try {
            const ufc::MatchupResponse response = ufc::analyzeMatchup(db, fighter_a, fighter_b);
            if (!etag.empty()) {
                res.set_header("ETag", etag);
                res.set_header("Cache-Control", "no-cache");
            }
            res.set_content(ufc::json::toJson(response), "application/json");
        } catch (const std::runtime_error& ex) {
            res.status = 404;
            std::ostringstream oss;
            oss << "{\"detail\":\"" << ufc::json::escape(ex.what()) << "\"}";
            res.set_content(oss.str(), "application/json");
        } catch (const std::exception& ex) {
            res.status = 500;
            std::ostringstream oss;
            oss << "{\"detail\":\"" << ufc::json::escape(ex.what()) << "\"}";
            res.set_content(oss.str(), "application/json");
        }
    });

    // Static files: mount only when frontend/dist exists. API paths are not files,
    // so handle_file_request returns false and falls through to API handlers.
    if (static_exists) {
        server.set_mount_point("/", static_dir.string());
    }

    std::cout << "Cage Oracle server on http://0.0.0.0:" << port << '\n';
    std::cout << "Database: " << db_path_str << '\n';
    if (static_exists) {
        std::cout << "Serving static files from: " << fs::absolute(static_dir) << '\n';
    } else {
        std::cout << "Static dir not found (" << static_dir << "); API only.\n";
    }

    std::cout << "Warming matchup indexes...\n";
    ufc::getPrefightMatchupIndex(db);
    ufc::getArchetypeIndex(db);
    std::cout << "Indexes ready.\n";

    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to listen on port " << port << '\n';
        return 1;
    }
    return 0;
}
