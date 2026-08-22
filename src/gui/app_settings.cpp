#include "app_settings.h"
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;

std::string AppSettings::get_cache_dir() {
    const char* home = getenv("HOME");
    fs::path cache_dir = home ? (fs::path(home) / ".cache" / "picexplore") : fs::path("/tmp/picexplore");
    try {
        fs::create_directories(cache_dir);
    } catch (...) {}
    return cache_dir.string();
}

std::string AppSettings::get_settings_path() {
    return (fs::path(get_cache_dir()) / "config.json").string();
}

void AppSettings::load() {
    std::string path = get_settings_path();
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        auto strip = [](std::string& s) {
            size_t start = s.find_first_not_of(" \t\r\n\"'");
            size_t end = s.find_last_not_of(" \t\r\n\"',");
            if (start == std::string::npos || end == std::string::npos || start > end) {
                s.clear();
            } else {
                s = s.substr(start, end - start + 1);
            }
        };
        strip(key);
        strip(val);

        if (key == "save_window_size") {
            save_window_size = (val == "true" || val == "1");
        } else if (key == "window_width") {
            try { int w = std::stoi(val); if (w >= 300) window_width = w; } catch (...) {}
        } else if (key == "window_height") {
            try { int h = std::stoi(val); if (h >= 200) window_height = h; } catch (...) {}
        } else if (key == "window_x") {
            try { window_x = std::stoi(val); } catch (...) {}
        } else if (key == "window_y") {
            try { window_y = std::stoi(val); } catch (...) {}
        } else if (key == "hierarchy_thumbnail_threshold") {
            try { double t = std::stod(val); if (t >= 1.0 && t <= 256.0) hierarchy_thumbnail_threshold = t; } catch (...) {}
        } else if (key == "deduplicate_flat_views") {
            deduplicate_flat_views = (val == "true" || val == "1");
        } else if (key == "sift_thumbnail_size") {
            try { int s = std::stoi(val); if (s == 128 || s == 256 || s == 512 || s == 1024) sift_thumbnail_size = s; } catch (...) {}
        } else if (key == "last_directory") {
            last_directory = val;
        }
    }
}

void AppSettings::save() const {
    std::string path = get_settings_path();
    std::ofstream out(path);
    if (!out.is_open()) return;

    out << "{\n";
    out << "  \"save_window_size\": " << (save_window_size ? "true" : "false") << ",\n";
    out << "  \"window_width\": " << window_width << ",\n";
    out << "  \"window_height\": " << window_height << ",\n";
    out << "  \"window_x\": " << window_x << ",\n";
    out << "  \"window_y\": " << window_y << ",\n";
    out << "  \"hierarchy_thumbnail_threshold\": " << hierarchy_thumbnail_threshold << ",\n";
    out << "  \"deduplicate_flat_views\": " << (deduplicate_flat_views ? "true" : "false") << ",\n";
    out << "  \"sift_thumbnail_size\": " << sift_thumbnail_size << ",\n";
    out << "  \"last_directory\": \"" << last_directory << "\"\n";
    out << "}\n";
}
