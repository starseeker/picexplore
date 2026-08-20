#include <iostream>
#include <string>
#include <filesystem>
#include <xxhash.h>
#include <FL/Fl.H>

#include "cxxopts.hpp"
#include "gui/main_window.h"
#include "cli/cli_scan.h"
#include "cli/pdf.h"

namespace fs = std::filesystem;

static std::string resolve_db_path(const std::string& explicit_db, const std::string& directory) {
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
        } else if (fs::exists("./images.db")) {
            return "./images.db";
        } else {
            const char* home = getenv("HOME");
            fs::path cache_dbs = home ? (fs::path(home) / ".cache" / "picexplore" / "databases") : fs::path("/tmp/picexplore/databases");
            try {
                fs::create_directories(cache_dbs);
            } catch (...) {}

            XXH128_hash_t h = XXH3_128bits(canon_dir.data(), canon_dir.size());
            char buf[64];
            snprintf(buf, sizeof(buf), "%016llx%016llx.db",
                     (unsigned long long)h.high64, (unsigned long long)h.low64);
            return (cache_dbs / buf).string();
        }
    }

    return "./images.db";
}

int main(int argc, char* argv[]) {
    try {
        cxxopts::Options options("picexplore", "PicExplore - Image Explorer, Batch Scanner & PDF Generator");

        options.add_options()
            ("h,help", "Print usage and options")
            ("d,directory", "Directory of images to view or scan", cxxopts::value<std::string>())
            ("s,scan", "Run batch scanner on directory without launching GUI")
            ("pdf", "Generate PDF gallery from database without launching GUI", cxxopts::value<std::string>())
            ("db,database", "Path to LMDB database file", cxxopts::value<std::string>())
            ("row-height", "Target row height in pixels for PDF layout", cxxopts::value<int>()->default_value("150"))
            ("margin", "Spacing between images in pixels for PDF", cxxopts::value<int>()->default_value("10"))
            ("layout-pad", "Layout padding for all sides in pixels", cxxopts::value<int>()->default_value("0"))
            ("layout-pad-top", "Layout padding top in pixels", cxxopts::value<int>())
            ("layout-pad-bottom", "Layout padding bottom in pixels", cxxopts::value<int>())
            ("layout-pad-left", "Layout padding left in pixels", cxxopts::value<int>())
            ("layout-pad-right", "Layout padding right in pixels", cxxopts::value<int>())
            ("v,verbose", "Enable verbose output")
            ;

        options.parse_positional({"directory"});
        options.positional_help("[directory]");

        auto result = options.parse(argc, argv);

        if (result.count("help")) {
            std::cout << options.help() << std::endl;
            std::cout << "\nUsage Examples:\n";
            std::cout << "  # Launch interactive GUI viewer for a directory:\n";
            std::cout << "  picexplore /path/to/photos\n\n";
            std::cout << "  # Batch scan directory headlessly into database:\n";
            std::cout << "  picexplore --scan /path/to/photos\n\n";
            std::cout << "  # Generate PDF gallery from database:\n";
            std::cout << "  picexplore --pdf gallery.pdf\n\n";
            std::cout << "  # Scan directory and generate PDF in one step:\n";
            std::cout << "  picexplore -d /path/to/photos --pdf gallery.pdf\n\n";
            return 0;
        }

        std::string directory = result.count("directory") ? result["directory"].as<std::string>() : "";
        std::string explicit_db = result.count("database") ? result["database"].as<std::string>() : "";
        std::string db_path = resolve_db_path(explicit_db, directory);
        bool verbose = result.count("verbose") > 0;

        // Mode 1: Headless Directory Scan
        if (result.count("scan")) {
            if (directory.empty()) {
                std::cerr << "Error: Must specify a directory to scan (e.g. picexplore --scan /path/to/photos)" << std::endl;
                return 1;
            }
            return run_headless_scan(directory, db_path, verbose);
        }

        // Mode 2: Headless PDF Export
        if (result.count("pdf")) {
            std::string pdf_path = result["pdf"].as<std::string>();

            int row_height = result["row-height"].as<int>();
            int margin = result["margin"].as<int>();
            int layout_pad_default = result["layout-pad"].as<int>();
            int pad_top = result.count("layout-pad-top") ? result["layout-pad-top"].as<int>() : layout_pad_default;
            int pad_bottom = result.count("layout-pad-bottom") ? result["layout-pad-bottom"].as<int>() : layout_pad_default;
            int pad_left = result.count("layout-pad-left") ? result["layout-pad-left"].as<int>() : layout_pad_default;
            int pad_right = result.count("layout-pad-right") ? result["layout-pad-right"].as<int>() : layout_pad_default;

            PDFOptions pdf_options;
            pdf_options.row_height = row_height;
            pdf_options.margin = margin;
            pdf_options.pad_top = pad_top;
            pdf_options.pad_bottom = pad_bottom;
            pdf_options.pad_left = pad_left;
            pdf_options.pad_right = pad_right;

            return run_headless_pdf(pdf_path, directory, db_path, pdf_options, verbose);
        }

        // Mode 3: Interactive GUI Viewer (Default)
        if (directory.empty()) {
            std::cout << options.help() << std::endl;
            std::cout << "\nError: Please provide a directory to explore, or specify --scan / --pdf for headless batch jobs.\n";
            return 1;
        }

        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            std::cerr << "Error: Directory does not exist: " << directory << std::endl;
            return 1;
        }

        Fl::visual(FL_DOUBLE | FL_RGB);
        Fl::background(40, 40, 40);
        Fl::foreground(220, 220, 220);
        Fl::background2(28, 28, 28);

        MainWindow win(1024, 768, "PicExplore", directory, db_path);
        win.show();
        win.start();

        return Fl::run();

    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
