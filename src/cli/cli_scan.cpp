#include "cli_scan.h"
#include <iostream>
#include <filesystem>
#include "../database.h"
#include "../utils.h"

namespace fs = std::filesystem;

int run_headless_scan(const std::string& directory, const std::string& db_path, bool verbose) {
    if (directory.empty()) {
        std::cerr << "Error: Directory path cannot be empty for scanning." << std::endl;
        return 1;
    }

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        std::cerr << "Error: Directory does not exist: " << directory << std::endl;
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

    std::cout << "Scanning directory: " << directory << std::endl;

    int processed = db.scan_directory_parallel(directory, timer, reporter);
    if (processed < 0) {
        std::cerr << "Error: Failed to scan directory" << std::endl;
        reporter.stop();
        return 1;
    }

    std::cout << "Processed " << processed << " images" << std::endl;

    reporter.stop();
    timer.print_summary();
    std::cout << "\nScan completed successfully!" << std::endl;
    return 0;
}
