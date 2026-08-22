/*
 * pdf_test.cpp - Unit and integration tests for PDF generation
 *
 * Copyright (c) 2026 Clifford Yapp
 */

#include <iostream>
#include <vector>
#include <cassert>
#include <filesystem>
#include <fstream>

#include "../src/cli/pdf.h"
#include "../src/gui/image_store.h"
#include "../src/gui/layout_engine.h"
#include "../src/gui/file_type_colors.h"

namespace fs = std::filesystem;

static ImageStore create_dummy_store(size_t count) {
    ImageStore store;
    for (size_t i = 0; i < count; ++i) {
        std::string folder = (i % 3 == 0) ? "photos/nature" : ((i % 3 == 1) ? "photos/city" : "scans");
        std::string filepath = "/tmp/test_dir/" + folder + "/img_" + std::to_string(i) + ".jpg";
        int w = (i % 2 == 0) ? 800 : 600;
        int h = (i % 2 == 0) ? 600 : 800;
        double ar = static_cast<double>(w) / h;
        uintmax_t fsz = 1024 * (100 + (i * 37) % 500);

        size_t idx = store.add_image(filepath, ar, w, h, fsz, 1700000000);
        auto& entry = store.get(idx);
        entry.content_hash = "hash_" + std::to_string(i);

        // Dummy 64x64 decoded RGB image
        entry.decoded.width = 64;
        entry.decoded.height = 64;
        entry.decoded.rgb_data.resize(64 * 64 * 3, static_cast<uint8_t>(i * 17));
    }
    return store;
}

void test_justified_pagination() {
    std::cout << "--- Testing Justified Layout PDF Pagination ---" << std::endl;

    auto store = create_dummy_store(50);
    PDFOptions options;
    options.set_paper_preset(PaperSize::LETTER);
    options.orientation = PageOrientation::PORTRAIT;
    options.page_dpi = 150.0;
    options.page_margin_inches = 0.5;
    options.row_height = 150;
    options.margin = 10;
    options.layout_type = LayoutEngine::LayoutType::JUSTIFIED;

    std::vector<std::pair<size_t, double>> indexed_aspects;
    for (size_t i = 0; i < store.size(); ++i) {
        const auto& e = store.get(i);
        indexed_aspects.push_back({e.index, e.aspect_ratio});
    }

    std::vector<std::vector<Item>> boxes_per_page;
    auto indices_per_page = PDFGenerator::calculate_justified_pagination(indexed_aspects, boxes_per_page, options);

    std::cout << "  Calculated " << boxes_per_page.size() << " pages for 50 images." << std::endl;
    assert(!boxes_per_page.empty());
    assert(boxes_per_page.size() == indices_per_page.size());

    size_t total_boxes = 0;
    for (const auto& page : boxes_per_page) {
        total_boxes += page.size();
    }
    assert(total_boxes == 50);
    std::cout << "  Pagination verified: 50/50 images accounted for across pages." << std::endl;
}

void test_pdf_document_generation() {
    std::cout << "--- Testing PDF File Export (Justified & Treemap) ---" << std::endl;

    auto store = create_dummy_store(30);
    PDFGenerator generator;

    std::string tmp_dir = fs::temp_directory_path().string() + "/picexplore_pdf_tests";
    fs::create_directories(tmp_dir);

    // 1. Test Justified Grid PDF
    {
        std::string pdf_path = tmp_dir + "/test_justified.pdf";
        PDFOptions options;
        options.set_paper_preset(PaperSize::LETTER);
        options.page_dpi = 150.0;
        options.layout_type = LayoutEngine::LayoutType::JUSTIFIED;
        options.row_height = 140;

        bool ok = generator.generate_from_store(store, pdf_path, options);
        assert(ok);
        assert(fs::exists(pdf_path));
        assert(fs::file_size(pdf_path) > 1000);

        // Verify PDF Magic header
        std::ifstream f(pdf_path, std::ios::binary);
        char header[5] = {0};
        f.read(header, 4);
        assert(std::string(header) == "%PDF");
        std::cout << "  Justified PDF exported successfully (" << fs::file_size(pdf_path) << " bytes)." << std::endl;
    }

    // 2. Test Flat Treemap PDF (Thumbnails & Cushion)
    {
        std::string pdf_path = tmp_dir + "/test_treemap_cushion.pdf";
        PDFOptions options;
        options.set_paper_preset(PaperSize::A4);
        options.orientation = PageOrientation::LANDSCAPE;
        options.page_dpi = 150.0;
        options.layout_type = LayoutEngine::LayoutType::TREEMAP;
        options.treemap_render_style = PDFTreemapRenderStyle::CUSHION_TREEMAP;

        bool ok = generator.generate_from_store(store, pdf_path, options);
        assert(ok);
        assert(fs::exists(pdf_path));
        assert(fs::file_size(pdf_path) > 1000);
        std::cout << "  Flat Treemap (Cushion) PDF exported successfully (" << fs::file_size(pdf_path) << " bytes)." << std::endl;
    }

    // 3. Test Hierarchical Treemap PDF (with Container Headers)
    {
        std::string pdf_path = tmp_dir + "/test_hierarchical_treemap.pdf";
        PDFOptions options;
        options.set_paper_preset(PaperSize::LETTER);
        options.orientation = PageOrientation::PORTRAIT;
        options.page_dpi = 150.0;
        options.layout_type = LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP;
        options.root_directory = "/tmp/test_dir";
        options.show_container_labels = true;
        options.treemap_render_style = PDFTreemapRenderStyle::ALL_THUMBNAILS;

        bool ok = generator.generate_from_store(store, pdf_path, options);
        assert(ok);
        assert(fs::exists(pdf_path));
        assert(fs::file_size(pdf_path) > 1000);
        std::cout << "  Hierarchical Treemap PDF exported successfully (" << fs::file_size(pdf_path) << " bytes)." << std::endl;
    }

    // 4. Test Treemap with File Type Colors
    {
        std::string pdf_path = tmp_dir + "/test_treemap_colors.pdf";
        PDFOptions options;
        options.set_paper_preset(PaperSize::TABLOID);
        options.orientation = PageOrientation::LANDSCAPE;
        options.page_dpi = 150.0;
        options.layout_type = LayoutEngine::LayoutType::TREEMAP;
        options.treemap_metric = LayoutEngine::TreemapMetric::PIXEL_AREA;
        options.treemap_render_style = PDFTreemapRenderStyle::FILE_TYPE_COLORS;

        bool ok = generator.generate_from_store(store, pdf_path, options);
        assert(ok);
        assert(fs::exists(pdf_path));
        assert(fs::file_size(pdf_path) > 1000);
        std::cout << "  Flat Treemap (File Type Colors) PDF exported successfully (" << fs::file_size(pdf_path) << " bytes)." << std::endl;
    }

    // 5. Test In-Dialog Preview Buffer Generation
    {
        PDFOptions options;
        options.set_paper_preset(PaperSize::LETTER);
        options.layout_type = LayoutEngine::LayoutType::JUSTIFIED;
        auto preview = PDFGenerator::render_preview_page(store, options, 0, 300, 400);
        assert(preview.size() == 300 * 400 * 3);
        std::cout << "  In-dialog preview buffer rendered successfully (300x400)." << std::endl;
    }

    // Cleanup
    fs::remove_all(tmp_dir);
}

int main() {
    std::cout << "=== Running PicExplore PDF Export Tests ===" << std::endl;
    test_justified_pagination();
    test_pdf_document_generation();
    std::cout << "All PDF tests passed successfully!" << std::endl;
    return 0;
}
