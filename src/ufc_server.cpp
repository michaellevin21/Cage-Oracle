#include "ufc/MatchupAnalysis.hpp"
#include "ufc/JsonSerialize.hpp"
#include "ufc/UfcDatabase.hpp"

#include "../third_party/httplib.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

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
        try {
            const ufc::MatchupResponse response = ufc::analyzeMatchup(db, fighter_a, fighter_b);
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

    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Failed to listen on port " << port << '\n';
        return 1;
    }
    return 0;
}
