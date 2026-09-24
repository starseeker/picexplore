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
    bool deduplicate_flat_views = true;
    int sift_thumbnail_size = 512;
    std::string last_directory;
    std::string default_layout = "hierarchical-treemap";
    std::string default_treemap_metric = "file-size";
    std::string default_treemap_style = "thumbnails";
    double default_row_height = 150.0;
    std::string default_sort_order = "alphabetical-asc";

    static std::string get_cache_dir();
    static std::string get_settings_path();

    void load();
    void save() const;
};
