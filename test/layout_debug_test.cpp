/*
 * Test incremental layout updates with debug output
 */

#include <iostream>
#include <filesystem>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include "Fl_JustifiedLayout.h"
#include "database.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    Fl::lock();

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <image_directory> [debug_output_dir]" << std::endl;
        return 1;
    }

    std::string image_dir = argv[1];
    std::string debug_dir = (argc > 2) ? argv[2] : "/tmp/layout_debug";

    // Create debug output directory
    fs::create_directories(debug_dir);

    // Create a simple window
    Fl_Window* window = new Fl_Window(800, 600, "Layout Debug Test");

    // Create layout widget
    Fl_JustifiedLayout* layout = new Fl_JustifiedLayout(10, 10, 780, 580);

    // Enable debug output
    layout->enable_debug_output(debug_dir, "svg");

    window->end();
    window->show();

    // Scan the directory to trigger incremental updates
    layout->set_directory_path(image_dir);

    std::cout << "Starting layout test with debug output enabled..." << std::endl;
    std::cout << "Debug output will be written to: " << debug_dir << std::endl;
    std::cout << "Press Ctrl+C to exit or close the window" << std::endl;

    // Run for a limited time to capture incremental updates
    Fl::run();

    return 0;
}