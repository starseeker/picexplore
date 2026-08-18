#include <FL/Fl.H>
#include "main_window.h"
#include <iostream>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [--db <db_path>] <directory>" << std::endl;
        return 1;
    }

    std::string directory;
    std::string db_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--db" || arg == "-db") && i + 1 < argc) {
            db_path = argv[++i];
        } else if (directory.empty() && arg[0] != '-') {
            directory = arg;
        }
    }

    if (directory.empty()) {
        std::cerr << "Usage: " << argv[0] << " [--db <db_path>] <directory>" << std::endl;
        return 1;
    }
    
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        std::cerr << "Error: Directory does not exist: " << directory << std::endl;
        return 1;
    }

    Fl::visual(FL_RGB);

    MainWindow win(1024, 768, "PicExplore", directory, db_path);
    win.show();
    win.start();

    return Fl::run();
}
