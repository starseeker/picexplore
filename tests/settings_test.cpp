#include <iostream>
#include <cassert>
#include <filesystem>
#include "gui/app_settings.h"

int main() {
    std::cout << "Testing AppSettings..." << std::endl;
    std::string test_cache = "/tmp/picexplore_test_settings";
    setenv("PICEXPLORE_CACHE_DIR", test_cache.c_str(), 1);
    std::filesystem::remove_all(test_cache);

    AppSettings s;
    s.save_window_size = true;
    s.window_width = 1440;
    s.window_height = 900;
    s.window_x = 100;
    s.window_y = 150;
    s.hierarchy_thumbnail_threshold = 12.0;
    s.deduplicate_flat_views = false;
    s.last_directory = "/tmp/test_photos";
    s.default_layout = "justified";
    s.default_treemap_metric = "pixel-area";
    s.default_treemap_style = "cushion";
    s.default_row_height = 180.0;

    s.save();

    AppSettings loaded;
    loaded.load();

    assert(loaded.save_window_size == true);
    assert(loaded.window_width == 1440);
    assert(loaded.window_height == 900);
    assert(loaded.window_x == 100);
    assert(loaded.window_y == 150);
    assert(loaded.hierarchy_thumbnail_threshold == 12.0);
    assert(loaded.deduplicate_flat_views == false);
    assert(loaded.last_directory == "/tmp/test_photos");
    assert(loaded.default_layout == "justified");
    assert(loaded.default_treemap_metric == "pixel-area");
    assert(loaded.default_treemap_style == "cushion");
    assert(loaded.default_row_height == 180.0);

    std::cout << "[PASS] AppSettings save and load test passed!" << std::endl;
    std::filesystem::remove_all(test_cache);
    return 0;
}
