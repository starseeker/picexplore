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
        const auto& e = store.at_position(i);
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

    // Verify padding options constrain box placement within the layout region
    options.pad_left = 25;
    options.pad_right = 30;
    options.pad_top = 20;
    options.pad_bottom = 40;
    std::vector<std::vector<Item>> padded_boxes;
    PDFGenerator::calculate_justified_pagination(indexed_aspects, padded_boxes, options);
    assert(!padded_boxes.empty());
    for (const auto& page : padded_boxes) {
        for (const auto& box : page) {
            assert(box.l >= options.pad_left - 1e-3);
            assert(box.l + box.w <= options.layout_width_px() - options.pad_right + 1e-3);
            assert(box.t >= options.pad_top - 1e-3);
        }
        if (page.size() > 1) {
            for (const auto& box : page) {
                assert(box.t + box.h <= options.layout_height_px() - options.pad_bottom + 1e-3);
            }
        }
    }

    // Verify directory_filter restricts flat treemap and justified layouts
    PDFOptions filtered_opts = options;
    filtered_opts.layout_type = LayoutEngine::LayoutType::TREEMAP;
    filtered_opts.directory_filter = "/tmp/test_dir/scans";
    auto filtered_tm = PDFGenerator::calculate_treemap_layout(store, filtered_opts);
    size_t expected_scans = 0;
    for (size_t i = 0; i < store.size(); ++i) {
        if (store.at_position(i).filepath.rfind("/tmp/test_dir/scans/", 0) == 0) {
            expected_scans++;
        }
    }
    assert(expected_scans > 0 && expected_scans < store.size());
    assert(filtered_tm.boxes.size() == expected_scans);
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

        // Verify PDF Magic header and FlateDecode stream filter
        std::ifstream f(pdf_path, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        assert(content.rfind("%PDF", 0) == 0);
        assert(content.find("/Filter /FlateDecode") != std::string::npos);
        assert(fs::file_size(pdf_path) < 500000);
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

    // 5. Test In-Dialog Preview Buffer Generation (decoded & DB snapshot)
    {
        PDFOptions options;
        options.set_paper_preset(PaperSize::LETTER);
        options.layout_type = LayoutEngine::LayoutType::JUSTIFIED;

        // Large decoded buffer (512x512) to verify resize dimensions passed to composite_image
        ImageStore large_store;
        size_t lidx = large_store.add_image("/tmp/test_dir/large.jpg", 1.0, 512, 512, 102400, 1700000000);
        auto& lentry = large_store.get(lidx);
        lentry.content_hash = "hash_large";
        lentry.decoded.width = 512;
        lentry.decoded.height = 512;
        lentry.decoded.rgb_data.resize(512 * 512 * 3, 0);
        for (size_t p = 0; p < 512 * 512; ++p) {
            lentry.decoded.rgb_data[p * 3 + 0] = 20;
            lentry.decoded.rgb_data[p * 3 + 1] = 180;
            lentry.decoded.rgb_data[p * 3 + 2] = 40;
        }

        auto preview = PDFGenerator::render_preview_page(large_store, options, 0, 300, 400);
        assert(preview.size() == 300 * 400 * 3);

        // Snapshot strips decoded RGB buffers (as in PDFExportDialog); verify DB fallback renders thumbnails
        DatabaseManager db;
        assert(db.open(tmp_dir + "/preview_cache.db"));
        auto thumb_jpeg = encode_jpeg(lentry.decoded.rgb_data.data(), 64, 64, 95);
        assert(db.begin_transaction());
        assert(db.store_key_data("hash_large:256", thumb_jpeg));
        assert(db.commit_transaction());

        ImageStore snap = large_store.snapshot();
        assert(snap.get(lidx).decoded.rgb_data.empty());

        auto snap_preview = PDFGenerator::render_preview_page(snap, options, 0, 300, 400, &db);
        assert(snap_preview.size() == 300 * 400 * 3);
        bool found_green = false;
        for (size_t p = 0; p < 300 * 400; ++p) {
            uint8_t r = snap_preview[p * 3 + 0];
            uint8_t g = snap_preview[p * 3 + 1];
            uint8_t b = snap_preview[p * 3 + 2];
            if (g > 150 && r < 60 && b < 80) {
                found_green = true;
                break;
            }
        }
        assert(found_green && "Snapshot preview failed to render thumbnail from database");

        options.layout_type = LayoutEngine::LayoutType::TREEMAP;
        options.treemap_render_style = PDFTreemapRenderStyle::ALL_THUMBNAILS;
        auto snap_tm_preview = PDFGenerator::render_preview_page(snap, options, 0, 300, 400, &db);
        bool found_tm_green = false;
        for (size_t p = 0; p < 300 * 400; ++p) {
            uint8_t r = snap_tm_preview[p * 3 + 0];
            uint8_t g = snap_tm_preview[p * 3 + 1];
            uint8_t b = snap_tm_preview[p * 3 + 2];
            if (g > 150 && r < 60 && b < 80) {
                found_tm_green = true;
                break;
            }
        }
        assert(found_tm_green && "Treemap snapshot preview failed to render thumbnail from database");

        options.treemap_render_style = PDFTreemapRenderStyle::CUSHION_TREEMAP;
        auto cushion_preview = PDFGenerator::render_preview_page(store, options, 0, 300, 400);
        auto layout = PDFGenerator::calculate_treemap_layout(store, options);
        assert(layout.boxes.size() >= 2);
        // Find a box with x > 0 or y > 0 to verify layout-coordinate cushion ridges and file-type tinting
        bool checked_offset_box = false;
        double scale = std::min(300.0 / options.page_width_px(), 400.0 / options.page_height_px());
        int margin_px = static_cast<int>(options.page_margin_px() * scale);
        for (const auto& box : layout.boxes) {
            if (box.x > 10.0 || box.y > 10.0) {
                int bx = margin_px + static_cast<int>(box.x * scale);
                int by = margin_px + static_cast<int>(box.y * scale);
                int bw = std::max(1, static_cast<int>(box.w * scale));
                int bh = std::max(1, static_cast<int>(box.h * scale));
                if (bw >= 10 && bh >= 10) {
                    int tl_idx = ((by + 2) * 300 + (bx + 2)) * 3;
                    int br_idx = ((by + bh - 3) * 300 + (bx + bw - 3)) * 3;
                    int tl_lum = cushion_preview[tl_idx] + cushion_preview[tl_idx + 1] + cushion_preview[tl_idx + 2];
                    int br_lum = cushion_preview[br_idx] + cushion_preview[br_idx + 1] + cushion_preview[br_idx + 2];
                    assert(tl_lum > br_lum && "Cushion ridge should illuminate top-left brighter than bottom-right");
                    assert((cushion_preview[tl_idx] != cushion_preview[tl_idx + 1] ||
                            cushion_preview[tl_idx + 1] != cushion_preview[tl_idx + 2]) &&
                           "Cushion shading should tint with file-type colors");
                    checked_offset_box = true;
                    break;
                }
            }
        }
        assert(checked_offset_box);

        db.close();
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
