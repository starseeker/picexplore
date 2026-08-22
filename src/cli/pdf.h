/*
 * pdf.h - PDF generation and layout logic for picexplore
 *
 * Copyright (c) 2025-2026 Clifford Yapp
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <memory>
#include <algorithm>
#include <cmath>

#include "../database.h"
#include "../utils.h"
#include "../justified_layout.hpp"
#include "../treemap_layout.hpp"
#include "../gui/layout_engine.h"
#include "../gui/image_store.h"

enum class PaperSize {
    LETTER,
    LEGAL,
    TABLOID,
    A4,
    A3,
    A2,
    A1,
    CUSTOM
};

enum class PageOrientation {
    PORTRAIT,
    LANDSCAPE
};

enum class PDFTreemapRenderStyle {
    ALL_THUMBNAILS,
    CUSHION_TREEMAP,
    FILE_TYPE_COLORS
};

// PDF and layout configuration options
struct PDFOptions {
    PaperSize paper_size = PaperSize::LETTER;
    PageOrientation orientation = PageOrientation::PORTRAIT;

    // Page dimensions (inches)
    double page_width_inches = 8.5;
    double page_height_inches = 11.0;

    // DPI settings
    double page_dpi = 300.0;

    // Page margin (inches)
    double page_margin_inches = 0.5;

    // Layout Mode
    LayoutEngine::LayoutType layout_type = LayoutEngine::LayoutType::JUSTIFIED;

    // Justified layout settings (pixels at current DPI)
    int row_height = 150;          // Target row height
    int margin = 10;               // Spacing between images
    int pad_top = 0;
    int pad_bottom = 0;
    int pad_left = 0;
    int pad_right = 0;

    // Treemap layout settings
    LayoutEngine::TreemapMetric treemap_metric = LayoutEngine::TreemapMetric::FILE_SIZE;
    PDFTreemapRenderStyle treemap_render_style = PDFTreemapRenderStyle::ALL_THUMBNAILS;
    bool show_container_labels = true;
    std::string root_directory = "";
    std::string directory_filter = "";

    void set_paper_preset(PaperSize preset) {
        paper_size = preset;
        switch (preset) {
            case PaperSize::LETTER:  page_width_inches = 8.5;   page_height_inches = 11.0; break;
            case PaperSize::LEGAL:   page_width_inches = 8.5;   page_height_inches = 14.0; break;
            case PaperSize::TABLOID: page_width_inches = 11.0;  page_height_inches = 17.0; break;
            case PaperSize::A4:      page_width_inches = 8.268; page_height_inches = 11.693; break; // 210 x 297 mm
            case PaperSize::A3:      page_width_inches = 11.693;page_height_inches = 16.535; break; // 297 x 420 mm
            case PaperSize::A2:      page_width_inches = 16.535;page_height_inches = 23.386; break; // 420 x 594 mm
            case PaperSize::A1:      page_width_inches = 23.386;page_height_inches = 33.110; break; // 594 x 841 mm
            case PaperSize::CUSTOM:  break;
        }
    }

    double effective_width_inches() const {
        return (orientation == PageOrientation::PORTRAIT) ? page_width_inches : page_height_inches;
    }

    double effective_height_inches() const {
        return (orientation == PageOrientation::PORTRAIT) ? page_height_inches : page_width_inches;
    }

    // Computed values (pixels)
    int page_width_px() const { return std::max(10, static_cast<int>(effective_width_inches() * page_dpi)); }
    int page_height_px() const { return std::max(10, static_cast<int>(effective_height_inches() * page_dpi)); }
    int page_margin_px() const { return static_cast<int>(page_margin_inches * page_dpi); }
    int layout_width_px() const { return std::max(10, page_width_px() - (2 * page_margin_px())); }
    int layout_height_px() const { return std::max(10, page_height_px() - (2 * page_margin_px())); }
};

// PDF generator class
class PDFGenerator {
public:
    PDFGenerator();
    ~PDFGenerator();

    bool generate_pdf(const std::vector<ImageInfo>& images, const std::string& output_path,
                      Timer& timer, StatusReporter& reporter, const PDFOptions& options);

    bool generate_from_store(const ImageStore& store, const std::string& output_path,
                             const PDFOptions& options, DatabaseManager* db = nullptr,
                             std::function<void(int current_page, int total_pages, const std::string& msg)> progress_cb = nullptr,
                             std::atomic<bool>* stop_requested = nullptr);

    // Pagination and preview helpers
    static std::vector<std::vector<size_t>> calculate_justified_pagination(
        const std::vector<std::pair<size_t, double>>& indexed_aspects,
        std::vector<std::vector<Item>>& boxes_per_page,
        const PDFOptions& options);

    static LayoutEngine::LayoutResult calculate_treemap_layout(
        const ImageStore& store,
        const PDFOptions& options);

    static std::vector<uint8_t> render_preview_page(
        const ImageStore& store,
        const PDFOptions& options,
        size_t page_index,
        int preview_w,
        int preview_h,
        DatabaseManager* db = nullptr);

private:
    static std::vector<uint8_t> resize_image_to_fit(const uint8_t* pixels,
                                                    int src_width, int src_height,
                                                    int target_width, int target_height);

    static void composite_image(std::vector<uint8_t>& page_buffer, int page_width, int page_height,
                                const std::vector<uint8_t>& image_data, int img_width, int img_height,
                                int x, int y, int box_width, int box_height);

    static void draw_filled_rect(std::vector<uint8_t>& buffer, int buf_w, int buf_h,
                                 int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b);

    static void draw_rect(std::vector<uint8_t>& buffer, int buf_w, int buf_h,
                          int x, int y, int w, int h, int thickness, uint8_t r, uint8_t g, uint8_t b);

    static void draw_cushion(std::vector<uint8_t>& buffer, int buf_w, int buf_h,
                             int x, int y, int w, int h,
                             double ax, double bx, double ay, double by);

    static void draw_text_simple(std::vector<uint8_t>& buffer, int buf_w, int buf_h,
                                 int x, int y, const std::string& text, uint8_t r, uint8_t g, uint8_t b, int scale = 1);
};

int run_headless_pdf(const std::string& pdf_path, const std::string& directory,
                     const std::string& db_path, const PDFOptions& options, bool verbose);
