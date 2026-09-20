#include <iostream>
#include <string>
#include <filesystem>
#include <xxhash.h>
#include <FL/Fl.H>

#include "cxxopts.hpp"
#include "gui/main_window.h"
#include "gui/app_settings.h"
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
        }
    }

    if (fs::exists("./images.db")) {
        return "./images.db";
    }

    const char* home = getenv("HOME");
    fs::path cache_dir = home ? (fs::path(home) / ".cache" / "picexplore") : fs::path("/tmp/picexplore");
    try {
        fs::create_directories(cache_dir);
    } catch (...) {}

    return (cache_dir / "cache.db").string();
}

int main(int argc, char* argv[]) {
    Fl::lock();
    try {
        cxxopts::Options options("picexplore", "PicExplore - Image Explorer, Batch Scanner & PDF Generator");

        options.add_options()
            ("h,help", "Print usage and options")
            ("d,directory", "Directory of images to view or scan", cxxopts::value<std::string>())
            ("s,scan", "Run batch scanner on directory without launching GUI")
            ("pdf", "Generate PDF gallery from database without launching GUI", cxxopts::value<std::string>())
            ("db,database", "Path to LMDB database file", cxxopts::value<std::string>())
            ("l,layout", "PDF Layout: justified, treemap, hierarchical-treemap", cxxopts::value<std::string>()->default_value("justified"))
            ("m,metric", "Treemap Metric: file-size, pixel-area, duplicate-count, equal-size", cxxopts::value<std::string>()->default_value("file-size"))
            ("style", "Treemap Render Style: thumbnails, cushion, file-type-colors", cxxopts::value<std::string>()->default_value("thumbnails"))
            ("paper-size", "Paper Size: letter, legal, tabloid, a4, a3, a2, a1", cxxopts::value<std::string>()->default_value("letter"))
            ("orientation", "Page Orientation: portrait, landscape", cxxopts::value<std::string>()->default_value("portrait"))
            ("page-margin", "Page margin in inches", cxxopts::value<double>()->default_value("0.5"))
            ("dpi", "Print resolution DPI", cxxopts::value<double>()->default_value("300"))
            ("no-container-labels", "Disable directory container banners and borders in hierarchical treemap")
            ("root-dir", "Root directory for hierarchical treemap", cxxopts::value<std::string>())
            ("row-height", "Target row height in pixels for justified PDF layout", cxxopts::value<int>()->default_value("150"))
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
            std::cout << "  # Generate PDF gallery (Justified Grid) from database:\n";
            std::cout << "  picexplore --pdf gallery.pdf\n\n";
            std::cout << "  # Generate Flat Treemap PDF (with cushion shading):\n";
            std::cout << "  picexplore -d /path/to/photos --pdf treemap.pdf --layout treemap --style cushion\n\n";
            std::cout << "  # Generate Hierarchical Treemap PDF (Letter, Landscape, 300 DPI):\n";
            std::cout << "  picexplore -d /path/to/photos --pdf hier.pdf --layout hierarchical-treemap --orientation landscape\n\n";
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

            std::string layout_str = result["layout"].as<std::string>();
            std::string metric_str = result["metric"].as<std::string>();
            std::string style_str = result["style"].as<std::string>();
            std::string paper_str = result["paper-size"].as<std::string>();
            std::string orient_str = result["orientation"].as<std::string>();
            double page_margin = result["page-margin"].as<double>();
            double dpi = result["dpi"].as<double>();

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
            pdf_options.page_margin_inches = page_margin;
            pdf_options.page_dpi = dpi;

            // Paper Size
            std::string p_lower = paper_str;
            std::transform(p_lower.begin(), p_lower.end(), p_lower.begin(), ::tolower);
            if (p_lower == "legal") pdf_options.set_paper_preset(PaperSize::LEGAL);
            else if (p_lower == "tabloid") pdf_options.set_paper_preset(PaperSize::TABLOID);
            else if (p_lower == "a4") pdf_options.set_paper_preset(PaperSize::A4);
            else if (p_lower == "a3") pdf_options.set_paper_preset(PaperSize::A3);
            else if (p_lower == "a2") pdf_options.set_paper_preset(PaperSize::A2);
            else if (p_lower == "a1") pdf_options.set_paper_preset(PaperSize::A1);
            else pdf_options.set_paper_preset(PaperSize::LETTER);

            // Orientation
            std::string o_lower = orient_str;
            std::transform(o_lower.begin(), o_lower.end(), o_lower.begin(), ::tolower);
            if (o_lower == "landscape") pdf_options.orientation = PageOrientation::LANDSCAPE;
            else pdf_options.orientation = PageOrientation::PORTRAIT;

            // Layout
            std::string l_lower = layout_str;
            std::transform(l_lower.begin(), l_lower.end(), l_lower.begin(), ::tolower);
            if (l_lower == "treemap" || l_lower == "flat-treemap" || l_lower == "flat_treemap") {
                pdf_options.layout_type = LayoutEngine::LayoutType::TREEMAP;
            } else if (l_lower == "hierarchical-treemap" || l_lower == "hierarchical_treemap" || l_lower == "hierarchical" || l_lower == "hier" || l_lower == "hier-treemap") {
                pdf_options.layout_type = LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP;
            } else {
                pdf_options.layout_type = LayoutEngine::LayoutType::JUSTIFIED;
            }

            // Treemap Metric
            std::string m_lower = metric_str;
            std::transform(m_lower.begin(), m_lower.end(), m_lower.begin(), ::tolower);
            if (m_lower == "pixel-area" || m_lower == "pixel_area" || m_lower == "area" || m_lower == "pixels") {
                pdf_options.treemap_metric = LayoutEngine::TreemapMetric::PIXEL_AREA;
            } else if (m_lower == "duplicate-count" || m_lower == "duplicate_count" || m_lower == "duplicates" || m_lower == "dupes") {
                pdf_options.treemap_metric = LayoutEngine::TreemapMetric::DUPLICATE_COUNT;
            } else if (m_lower == "equal-size" || m_lower == "equal_size" || m_lower == "equal") {
                pdf_options.treemap_metric = LayoutEngine::TreemapMetric::EQUAL_SIZE;
            } else {
                pdf_options.treemap_metric = LayoutEngine::TreemapMetric::FILE_SIZE;
            }

            // Treemap Render Style
            std::string s_lower = style_str;
            std::transform(s_lower.begin(), s_lower.end(), s_lower.begin(), ::tolower);
            if (s_lower == "cushion" || s_lower == "cushion-treemap" || s_lower == "cushion_treemap") {
                pdf_options.treemap_render_style = PDFTreemapRenderStyle::CUSHION_TREEMAP;
            } else if (s_lower == "file-type-colors" || s_lower == "file_type_colors" || s_lower == "colors" || s_lower == "color") {
                pdf_options.treemap_render_style = PDFTreemapRenderStyle::FILE_TYPE_COLORS;
            } else {
                pdf_options.treemap_render_style = PDFTreemapRenderStyle::ALL_THUMBNAILS;
            }

            pdf_options.show_container_labels = (result.count("no-container-labels") == 0);
            if (result.count("root-dir")) {
                pdf_options.root_directory = result["root-dir"].as<std::string>();
            } else {
                pdf_options.root_directory = directory;
            }

            return run_headless_pdf(pdf_path, directory, db_path, pdf_options, verbose);
        }

        // Mode 3: Interactive GUI Viewer (Default)
        AppSettings settings;
        settings.load();

        if (directory.empty()) {
            if (!settings.last_directory.empty() && fs::exists(settings.last_directory) && fs::is_directory(settings.last_directory)) {
                directory = settings.last_directory;
            } else {
                directory = ".";
            }
        }

        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            std::cerr << "Error: Directory does not exist: " << directory << std::endl;
            return 1;
        }

        Fl::visual(FL_DOUBLE | FL_RGB);
        Fl::background(40, 40, 40);
        Fl::foreground(220, 220, 220);
        Fl::background2(28, 28, 28);

        int win_w = (settings.window_width >= 300) ? settings.window_width : 1024;
        int win_h = (settings.window_height >= 200) ? settings.window_height : 768;

        MainWindow win(win_w, win_h, "PicExplore", directory, db_path);
        if (settings.save_window_size && settings.window_x >= 0 && settings.window_y >= 0) {
            win.position(settings.window_x, settings.window_y);
        }
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
