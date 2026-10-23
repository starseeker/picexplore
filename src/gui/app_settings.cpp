#include "app_settings.h"
#include <iostream>
#include <cstdlib>

namespace fs = std::filesystem;

std::string AppSettings::get_cache_dir() {
    const char* pic_cache = getenv("PICEXPLORE_CACHE_DIR");
    if (pic_cache && pic_cache[0] != '\0') {
        try { fs::create_directories(pic_cache); } catch (...) {}
        return pic_cache;
    }
    const char* xdg = getenv("XDG_CACHE_HOME");
    if (xdg && xdg[0] != '\0') {
        fs::path cache_dir = fs::path(xdg) / "picexplore";
        try { fs::create_directories(cache_dir); } catch (...) {}
        return cache_dir.string();
    }
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

std::string AppSettings::resolve_db_path(const std::string& explicit_db, const std::string& directory) {
    if (!explicit_db.empty()) {
        return explicit_db;
    }

    if (!directory.empty()) {
        std::string canon_dir = directory;
        try {
            canon_dir = fs::canonical(directory).string();
        } catch (...) {}

        fs::path local_db = fs::path(canon_dir) / "images.db";
        if (fs::exists(local_db)) {
            return local_db.string();
        }
    }

    if (fs::exists("./images.db")) {
        return "./images.db";
    }

    return (fs::path(get_cache_dir()) / "cache.db").string();
}

namespace {
std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

bool parse_json_key_value(const std::string& line, std::string& key_out, std::string& val_out) {
    size_t q1 = line.find('"');
    if (q1 == std::string::npos) return false;
    size_t q2 = q1 + 1;
    while (q2 < line.size()) {
        if (line[q2] == '\\') {
            q2 += 2;
            continue;
        }
        if (line[q2] == '"') break;
        ++q2;
    }
    if (q2 >= line.size()) return false;
    key_out = line.substr(q1 + 1, q2 - q1 - 1);

    size_t colon = line.find(':', q2 + 1);
    if (colon == std::string::npos) return false;

    size_t vstart = line.find_first_not_of(" \t\r\n", colon + 1);
    if (vstart == std::string::npos) return false;

    if (line[vstart] == '"') {
        std::string unescaped;
        size_t i = vstart + 1;
        while (i < line.size()) {
            if (line[i] == '\\' && i + 1 < line.size()) {
                char next = line[i + 1];
                switch (next) {
                    case '"':  unescaped += '"'; break;
                    case '\\': unescaped += '\\'; break;
                    case 'n':  unescaped += '\n'; break;
                    case 'r':  unescaped += '\r'; break;
                    case 't':  unescaped += '\t'; break;
                    default:   unescaped += next; break;
                }
                i += 2;
                continue;
            }
            if (line[i] == '"') break;
            unescaped += line[i];
            ++i;
        }
        val_out = std::move(unescaped);
        return true;
    }

    size_t vend = line.find_last_not_of(" \t\r\n,");
    if (vend == std::string::npos || vend < vstart) return false;
    val_out = line.substr(vstart, vend - vstart + 1);
    return true;
}
} // namespace

void AppSettings::load() {
    std::string path = get_settings_path();
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line)) {
        std::string key, val;
        if (!parse_json_key_value(line, key, val)) continue;

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
        } else if (key == "default_layout") {
            default_layout = val;
        } else if (key == "default_treemap_metric") {
            default_treemap_metric = val;
        } else if (key == "default_treemap_style") {
            default_treemap_style = val;
        } else if (key == "default_row_height") {
            try { double rh = std::stod(val); if (rh >= 50.0 && rh <= 800.0) default_row_height = rh; } catch (...) {}
        } else if (key == "default_sort_order" || key == "default_sort" || key == "sort_order") {
            if (!val.empty()) default_sort_order = val;
        } else if (key == "external_editor") {
            external_editor = val;
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
    out << "  \"last_directory\": \"" << escape_json_string(last_directory) << "\",\n";
    out << "  \"default_layout\": \"" << escape_json_string(default_layout) << "\",\n";
    out << "  \"default_treemap_metric\": \"" << escape_json_string(default_treemap_metric) << "\",\n";
    out << "  \"default_treemap_style\": \"" << escape_json_string(default_treemap_style) << "\",\n";
    out << "  \"default_row_height\": " << default_row_height << ",\n";
    out << "  \"default_sort_order\": \"" << escape_json_string(default_sort_order) << "\",\n";
    out << "  \"external_editor\": \"" << escape_json_string(external_editor) << "\"\n";
    out << "}\n";
}
