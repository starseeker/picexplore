#include <iostream>
#include <cassert>
#include <filesystem>
#include "gui/app_settings.h"

int main() {
    std::cout << "Testing AppSettings..." << std::endl;

    AppSettings s;
    s.save_window_size = true;
    s.window_width = 1440;
    s.window_height = 900;
    s.window_x = 100;
    s.window_y = 150;
    s.hierarchy_thumbnail_threshold = 12.0;
    s.last_directory = "/tmp/test_photos";

    s.save();

    AppSettings loaded;
    loaded.load();

    assert(loaded.save_window_size == true);
    assert(loaded.window_width == 1440);
    assert(loaded.window_height == 900);
    assert(loaded.window_x == 100);
    assert(loaded.window_y == 150);
    assert(loaded.hierarchy_thumbnail_threshold == 12.0);
    assert(loaded.last_directory == "/tmp/test_photos");

    std::cout << "[PASS] AppSettings save and load test passed!" << std::endl;
    return 0;
}
