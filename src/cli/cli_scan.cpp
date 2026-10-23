#include "cli_scan.h"
#include <iostream>
#include <filesystem>
#include "../database.h"
#include "../utils.h"

namespace fs = std::filesystem;

int run_headless_scan(const std::string& directory, const std::string& db_path, bool verbose, bool recursive) {
    if (directory.empty()) {
        std::cerr << "Error: Directory path cannot be empty for scanning." << std::endl;
        return 1;
    }

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "Error: Directory does not exist: " << directory << std::endl;
        return 1;
    }

    InputSelection sel;
    sel.root_directory = directory;
    sel.recursive = recursive;
    sel.explicit_selection = false;
    return run_headless_scan(sel, db_path, verbose);
}

int run_headless_scan(const InputSelection& selection, const std::string& db_path, bool verbose) {
    if (!selection.error.empty()) {
        std::cerr << "Error: " << selection.error << std::endl;
        return 1;
    }

    Timer timer;
    StatusReporter reporter(10); // Report every 10 seconds
    reporter.start();

    // Initialize database
    DatabaseManager db;
    if (!db.open(db_path)) {
        std::cerr << "Error: Failed to open database: " << db_path << std::endl;
        reporter.stop();
        return 1;
    }

    if (verbose) {
        std::cout << "Using database: " << db_path << std::endl;
    }

    int processed = -1;
    if (selection.explicit_selection) {
        std::cout << "Scanning " << selection.files.size() << " selected image file(s)..." << std::endl;
        processed = db.scan_files_parallel(selection.files, timer, reporter);
    } else {
        std::cout << "Scanning directory" << (selection.recursive ? ": " : " (non-recursive): ")
                  << selection.root_directory << std::endl;
        processed = db.scan_directory_parallel(selection.root_directory, timer, reporter, 0, selection.recursive);
    }
    if (processed < 0) {
        std::cerr << "Error: Failed to scan images" << std::endl;
        reporter.stop();
        return 1;
    }

    std::cout << "Processed " << processed << " images" << std::endl;

    reporter.stop();
    timer.print_summary();
    std::cout << "\nScan completed successfully!" << std::endl;
    return 0;
}
