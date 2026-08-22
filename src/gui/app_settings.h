#pragma once

#include <string>
#include <fstream>
#include <filesystem>

struct AppSettings {
    bool save_window_size = true;
    int window_width = 1024;
    int window_height = 768;
    int window_x = -1;
    int window_y = -1;
    double hierarchy_thumbnail_threshold = 8.0;
    bool deduplicate_flat_views = false;
    std::string last_directory;

    static std::string get_cache_dir();
    static std::string get_settings_path();

    void load();
    void save() const;
};
