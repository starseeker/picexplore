#include <FL/Fl.H>
#include "main_window.h"
#include <iostream>
#include <filesystem>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <directory>" << std::endl;
        return 1;
    }

    std::string directory = argv[1];
    
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        std::cerr << "Error: Directory does not exist: " << directory << std::endl;
        return 1;
    }

    Fl::visual(FL_RGB);

    MainWindow win(1024, 768, "PicExplore", directory);
    win.show();
    win.start();

    return Fl::run();
}
