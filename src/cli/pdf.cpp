/*
 * pdf.cpp - PDF generation and layout logic for picexplore
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

#include "pdf.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <filesystem>
#include <cstring>
#include <thread>
#include <mutex>

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "pdfimg.hpp"
#include "../gui/file_type_colors.h"

namespace fs = std::filesystem;

namespace {
    // 5x7 minimal bitmap font for container label rendering
    static const uint8_t FONT_5X7[][5] = {
        {0x00, 0x00, 0x00, 0x00, 0x00}, // ' ' (32)
        {0x00, 0x00, 0x5F, 0x00, 0x00}, // '!'
        {0x00, 0x07, 0x00, 0x07, 0x00}, // '"'
        {0x14, 0x7F, 0x14, 0x7F, 0x14}, // '#'
        {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // '$'
        {0x23, 0x13, 0x08, 0x64, 0x62}, // '%'
        {0x36, 0x49, 0x55, 0x22, 0x50}, // '&'
        {0x00, 0x05, 0x03, 0x00, 0x00}, // '\''
        {0x00, 0x1C, 0x22, 0x41, 0x00}, // '('
        {0x00, 0x41, 0x22, 0x1C, 0x00}, // ')'
        {0x14, 0x08, 0x3E, 0x08, 0x14}, // '*'
        {0x08, 0x08, 0x3E, 0x08, 0x08}, // '+'
        {0x00, 0x50, 0x30, 0x00, 0x00}, // ','
        {0x08, 0x08, 0x08, 0x08, 0x08}, // '-'
        {0x00, 0x60, 0x60, 0x00, 0x00}, // '.'
        {0x20, 0x10, 0x08, 0x04, 0x02}, // '/'
        {0x3E, 0x51, 0x49, 0x45, 0x3E}, // '0'
        {0x00, 0x42, 0x7F, 0x40, 0x00}, // '1'
        {0x42, 0x61, 0x51, 0x49, 0x46}, // '2'
        {0x21, 0x41, 0x45, 0x4B, 0x31}, // '3'
        {0x18, 0x14, 0x12, 0x7F, 0x10}, // '4'
        {0x27, 0x45, 0x45, 0x45, 0x39}, // '5'
        {0x3C, 0x4A, 0x49, 0x49, 0x30}, // '6'
        {0x01, 0x71, 0x09, 0x05, 0x03}, // '7'
        {0x36, 0x49, 0x49, 0x49, 0x36}, // '8'
        {0x06, 0x49, 0x49, 0x29, 0x1E}, // '9'
        {0x00, 0x36, 0x36, 0x00, 0x00}, // ':'
        {0x00, 0x56, 0x36, 0x00, 0x00}, // ';'
        {0x08, 0x14, 0x22, 0x41, 0x00}, // '<'
        {0x14, 0x14, 0x14, 0x14, 0x14}, // '='
        {0x00, 0x41, 0x22, 0x14, 0x08}, // '>'
        {0x02, 0x01, 0x51, 0x09, 0x06}, // '?'
        {0x32, 0x49, 0x79, 0x41, 0x3E}, // '@'
        {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 'A'
        {0x7F, 0x49, 0x49, 0x49, 0x36}, // 'B'
        {0x3E, 0x41, 0x41, 0x41, 0x22}, // 'C'
        {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 'D'
        {0x7F, 0x49, 0x49, 0x49, 0x41}, // 'E'
        {0x7F, 0x09, 0x09, 0x09, 0x01}, // 'F'
        {0x3E, 0x41, 0x49, 0x49, 0x7A}, // 'G'
        {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 'H'
        {0x00, 0x41, 0x7F, 0x41, 0x00}, // 'I'
        {0x20, 0x40, 0x41, 0x3F, 0x01}, // 'J'
        {0x7F, 0x08, 0x14, 0x22, 0x41}, // 'K'
        {0x7F, 0x40, 0x40, 0x40, 0x40}, // 'L'
        {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // 'M'
        {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 'N'
        {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 'O'
        {0x7F, 0x09, 0x09, 0x09, 0x06}, // 'P'
        {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 'Q'
        {0x7F, 0x09, 0x19, 0x29, 0x46}, // 'R'
        {0x46, 0x49, 0x49, 0x49, 0x31}, // 'S'
        {0x01, 0x01, 0x7F, 0x01, 0x01}, // 'T'
        {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 'U'
        {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 'V'
        {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 'W'
        {0x63, 0x14, 0x08, 0x14, 0x63}, // 'X'
        {0x07, 0x08, 0x70, 0x08, 0x07}, // 'Y'
        {0x61, 0x51, 0x49, 0x45, 0x43}, // 'Z'
        {0x00, 0x7F, 0x41, 0x41, 0x00}, // '['
        {0x02, 0x04, 0x08, 0x10, 0x20}, // '\'
        {0x00, 0x41, 0x41, 0x7F, 0x00}, // ']'
        {0x04, 0x02, 0x01, 0x02, 0x04}, // '^'
        {0x40, 0x40, 0x40, 0x40, 0x40}, // '_'
    };
}

PDFGenerator::PDFGenerator() {}
PDFGenerator::~PDFGenerator() {}

void PDFGenerator::draw_filled_rect(std::vector<uint8_t>& buffer, int buf_w, int buf_h,
                                    int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    int x1 = std::clamp(x, 0, buf_w);
    int y1 = std::clamp(y, 0, buf_h);
    int x2 = std::clamp(x + w, 0, buf_w);
    int y2 = std::clamp(y + h, 0, buf_h);

    for (int cy = y1; cy < y2; ++cy) {
        uint8_t* ptr = buffer.data() + (cy * buf_w + x1) * 3;
        for (int cx = x1; cx < x2; ++cx) {
            *ptr++ = r;
            *ptr++ = g;
            *ptr++ = b;
        }
    }
}

void PDFGenerator::draw_rect(std::vector<uint8_t>& buffer, int buf_w, int buf_h,
                             int x, int y, int w, int h, int thickness, uint8_t r, uint8_t g, uint8_t b) {
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    draw_filled_rect(buffer, buf_w, buf_h, x, y, w, thickness, r, g, b);                         // Top
    draw_filled_rect(buffer, buf_w, buf_h, x, y + h - thickness, w, thickness, r, g, b);         // Bottom
    draw_filled_rect(buffer, buf_w, buf_h, x, y, thickness, h, r, g, b);                         // Left
    draw_filled_rect(buffer, buf_w, buf_h, x + w - thickness, y, thickness, h, r, g, b);         // Right
}

void PDFGenerator::draw_cushion(std::vector<uint8_t>& buffer, int buf_w, int buf_h,
                                int x, int y, int w, int h,
                                double ax, double bx, double ay, double by) {
    int x1 = std::clamp(x, 0, buf_w);
    int y1 = std::clamp(y, 0, buf_h);
    int x2 = std::clamp(x + w, 0, buf_w);
    int y2 = std::clamp(y + h, 0, buf_h);

    constexpr double lx = 0.57735;
    constexpr double ly = 0.57735;
    constexpr double lz = 0.57735;
    constexpr double Ia = 40.0;
    constexpr double Is = 215.0;

    for (int cy = y1; cy < y2; ++cy) {
        double py = cy - y;
        double ny = -(2.0 * ay * py + by);
        uint8_t* ptr = buffer.data() + (cy * buf_w + x1) * 3;

        for (int cx = x1; cx < x2; ++cx) {
            double px = cx - x;
            double nx = -(2.0 * ax * px + bx);
            double cosa = (nx * lx + ny * ly + lz) / std::sqrt(nx * nx + ny * ny + 1.0);
            cosa = std::max(0.0, cosa);
            double intensity = Ia + Is * cosa;
            uint8_t val = static_cast<uint8_t>(std::clamp(intensity, 0.0, 255.0));

            *ptr++ = val;
            *ptr++ = val;
            *ptr++ = val;
        }
    }
}

void PDFGenerator::draw_text_simple(std::vector<uint8_t>& buffer, int buf_w, int buf_h,
                                    int x, int y, const std::string& text, uint8_t r, uint8_t g, uint8_t b, int scale) {
    if (scale <= 0) scale = 1;
    int cur_x = x;

    for (char c : text) {
        char upper = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
        if (upper < 32 || upper > 95) upper = ' ';
        int idx = upper - 32;

        for (int col = 0; col < 5; ++col) {
            uint8_t line = FONT_5X7[idx][col];
            for (int row = 0; row < 7; ++row) {
                if (line & (1 << row)) {
                    draw_filled_rect(buffer, buf_w, buf_h,
                                     cur_x + col * scale, y + row * scale,
                                     scale, scale, r, g, b);
                }
            }
        }
        cur_x += 6 * scale; // 5 width + 1 spacing
    }
}

std::vector<std::vector<size_t>> PDFGenerator::calculate_justified_pagination(
    const std::vector<std::pair<size_t, double>>& indexed_aspects,
    std::vector<std::vector<Item>>& boxes_per_page,
    const PDFOptions& options)
{
    boxes_per_page.clear();
    std::vector<std::vector<size_t>> indices_per_page;
    if (indexed_aspects.empty()) return indices_per_page;

    LayoutCfg layout_cfg;
    layout_cfg.w = options.layout_width_px();
    layout_cfg.pl = layout_cfg.pr = layout_cfg.pt = layout_cfg.pb = 0;
    layout_cfg.sh = layout_cfg.sv = options.margin;
    layout_cfg.rh = options.row_height;
    layout_cfg.tol = 0.25;
    layout_cfg.widows = true;
    layout_cfg.ws = WidowStyle::Left;

    std::vector<Item> items;
    items.reserve(indexed_aspects.size());
    for (const auto& [idx, ar] : indexed_aspects) {
        Item item;
        item.ar = ar;
        items.push_back(item);
    }

    size_t image_idx = 0;
    while (image_idx < items.size()) {
        std::vector<Item> page_boxes;
        std::vector<size_t> page_indices;
        double current_y = 0;

        while (current_y < options.layout_height_px() && image_idx < items.size()) {
            std::vector<Item> remaining_items(items.begin() + image_idx, items.end());

            LayoutCfg temp_cfg = layout_cfg;
            temp_cfg.maxRows = std::max(1, static_cast<int>((options.layout_height_px() - current_y) / options.row_height));
            JustifiedLayout layout(remaining_items, temp_cfg);

            if (layout.boxes().empty()) break;

            double layout_h = layout.height();
            size_t nboxes = layout.boxes().size();

            if (current_y + layout_h <= options.layout_height_px()) {
                for (size_t b = 0; b < nboxes; ++b) {
                    Item box = layout.boxes()[b];
                    box.t += current_y;
                    page_boxes.push_back(box);
                    page_indices.push_back(indexed_aspects[image_idx + b].first);
                }
                image_idx += nboxes;
                current_y += layout_h;
            } else {
                size_t local_idx = 0;
                double test_y = current_y;
                std::vector<Item> fit_boxes;
                std::vector<size_t> fit_indices;

                while (test_y < options.layout_height_px() && (image_idx + local_idx) < items.size()) {
                    LayoutCfg row_cfg = layout_cfg;
                    row_cfg.maxRows = 1;
                    std::vector<Item> row_items(items.begin() + image_idx + local_idx, items.end());
                    JustifiedLayout row_layout(row_items, row_cfg);
                    if (row_layout.boxes().empty()) break;
                    double row_h = row_layout.height();
                    if (test_y + row_h > options.layout_height_px()) break;

                    for (size_t rb = 0; rb < row_layout.boxes().size(); ++rb) {
                        Item box = row_layout.boxes()[rb];
                        box.t += test_y;
                        fit_boxes.push_back(box);
                        fit_indices.push_back(indexed_aspects[image_idx + local_idx + rb].first);
                    }
                    local_idx += row_layout.boxes().size();
                    test_y += row_h;
                }

                if (!fit_boxes.empty()) {
                    for (size_t b = 0; b < fit_boxes.size(); ++b) {
                        page_boxes.push_back(fit_boxes[b]);
                        page_indices.push_back(fit_indices[b]);
                    }
                    image_idx += fit_boxes.size();
                } else if (page_boxes.empty() && image_idx < items.size()) {
                    // Force at least 1 image if page is empty to prevent infinite loop
                    Item box = layout.boxes()[0];
                    box.t += current_y;
                    page_boxes.push_back(box);
                    page_indices.push_back(indexed_aspects[image_idx].first);
                    image_idx++;
                }
                break;
            }
        }

        if (!page_boxes.empty()) {
            boxes_per_page.push_back(page_boxes);
            indices_per_page.push_back(page_indices);
        } else {
            break;
        }
    }

    return indices_per_page;
}

LayoutEngine::LayoutResult PDFGenerator::calculate_treemap_layout(
    const ImageStore& store,
    const PDFOptions& options)
{
    LayoutEngine engine;
    double target_w = options.layout_width_px();
    double target_h = options.layout_height_px();

    if (options.layout_type == LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP) {
        std::vector<HierarchicalTreemapItem> hitems;
        hitems.reserve(store.size());

        for (size_t i = 0; i < store.size(); ++i) {
            const auto& entry = store.get(i);
            double weight = 1.0;
            switch (options.treemap_metric) {
                case LayoutEngine::TreemapMetric::FILE_SIZE:
                    weight = static_cast<double>(entry.file_size);
                    break;
                case LayoutEngine::TreemapMetric::PIXEL_AREA:
                    weight = static_cast<double>(std::max(1, entry.original_width)) * std::max(1, entry.original_height);
                    break;
                case LayoutEngine::TreemapMetric::DUPLICATE_COUNT:
                    weight = static_cast<double>(std::max(1, entry.duplicate_count));
                    break;
                case LayoutEngine::TreemapMetric::EQUAL_SIZE:
                    weight = 1.0;
                    break;
            }
            hitems.push_back({entry.index, entry.filepath, weight});
        }

        return engine.compute_hierarchical_treemap(hitems, options.root_directory, options.directory_filter, target_w, target_h, 2.0);
    } else {
        std::vector<TreemapItem> titems;
        titems.reserve(store.size());

        for (size_t i = 0; i < store.size(); ++i) {
            const auto& entry = store.get(i);
            double weight = 1.0;
            switch (options.treemap_metric) {
                case LayoutEngine::TreemapMetric::FILE_SIZE:
                    weight = static_cast<double>(entry.file_size);
                    break;
                case LayoutEngine::TreemapMetric::PIXEL_AREA:
                    weight = static_cast<double>(std::max(1, entry.original_width)) * std::max(1, entry.original_height);
                    break;
                case LayoutEngine::TreemapMetric::DUPLICATE_COUNT:
                    weight = static_cast<double>(std::max(1, entry.duplicate_count));
                    break;
                case LayoutEngine::TreemapMetric::EQUAL_SIZE:
                    weight = 1.0;
                    break;
            }
            titems.push_back({entry.index, weight});
        }

        return engine.compute_treemap(titems, target_w, target_h, 2.0);
    }
}

std::vector<uint8_t> PDFGenerator::resize_image_to_fit(const uint8_t* pixels,
                                                       int src_width, int src_height,
                                                       int target_width, int target_height) {
    if (!pixels || src_width <= 0 || src_height <= 0 || target_width <= 0 || target_height <= 0) {
        return {};
    }

    double scale = std::min(static_cast<double>(target_width) / src_width,
                            static_cast<double>(target_height) / src_height);
    int new_w = std::max(1, static_cast<int>(src_width * scale));
    int new_h = std::max(1, static_cast<int>(src_height * scale));

    std::vector<uint8_t> resized(new_w * new_h * 3);
    if (!stbir_resize_uint8_linear(pixels, src_width, src_height, 0,
                                   resized.data(), new_w, new_h, 0, STBIR_RGB)) {
        return {};
    }
    return resized;
}

void PDFGenerator::composite_image(std::vector<uint8_t>& page_buffer, int page_width, int page_height,
                                   const std::vector<uint8_t>& image_data, int img_width, int img_height,
                                   int x, int y, int box_width, int box_height) {
    if (image_data.empty() || img_width <= 0 || img_height <= 0) return;

    int offset_x = x + (box_width - img_width) / 2;
    int offset_y = y + (box_height - img_height) / 2;

    for (int row = 0; row < img_height; ++row) {
        int page_y = offset_y + row;
        if (page_y < 0 || page_y >= page_height) continue;

        int page_row_start = page_y * page_width * 3;
        int img_row_start = row * img_width * 3;

        for (int col = 0; col < img_width; ++col) {
            int page_x = offset_x + col;
            if (page_x < 0 || page_x >= page_width) continue;

            int page_pixel = page_row_start + page_x * 3;
            int img_pixel = img_row_start + col * 3;

            page_buffer[page_pixel + 0] = image_data[img_pixel + 0];
            page_buffer[page_pixel + 1] = image_data[img_pixel + 1];
            page_buffer[page_pixel + 2] = image_data[img_pixel + 2];
        }
    }
}

std::vector<uint8_t> PDFGenerator::render_preview_page(
    const ImageStore& store,
    const PDFOptions& options,
    size_t page_index,
    int preview_w,
    int preview_h,
    DatabaseManager* db)
{
    if (preview_w <= 0 || preview_h <= 0) return {};

    std::vector<uint8_t> preview_buf(preview_w * preview_h * 3, 255);

    double page_w_in = options.effective_width_inches();
    double page_h_in = options.effective_height_inches();

    double scale_x = static_cast<double>(preview_w) / (page_w_in * options.page_dpi);
    double scale_y = static_cast<double>(preview_h) / (page_h_in * options.page_dpi);
    double scale = std::min(scale_x, scale_y);

    int margin_px = static_cast<int>(options.page_margin_px() * scale);
    int layout_w = static_cast<int>(options.layout_width_px() * scale);
    int layout_h = static_cast<int>(options.layout_height_px() * scale);

    if (options.layout_type == LayoutEngine::LayoutType::JUSTIFIED) {
        std::vector<std::pair<size_t, double>> indexed_aspects;
        indexed_aspects.reserve(store.size());
        for (size_t i = 0; i < store.size(); ++i) {
            const auto& entry = store.get(i);
            double ar = (entry.aspect_ratio > 0.0) ? entry.aspect_ratio : 1.0;
            indexed_aspects.push_back({entry.index, ar});
        }

        std::vector<std::vector<Item>> boxes_per_page;
        auto indices_per_page = calculate_justified_pagination(indexed_aspects, boxes_per_page, options);

        if (page_index >= boxes_per_page.size()) return preview_buf;

        const auto& p_boxes = boxes_per_page[page_index];
        const auto& p_indices = indices_per_page[page_index];

        for (size_t i = 0; i < p_boxes.size(); ++i) {
            const auto& box = p_boxes[i];
            size_t store_idx = p_indices[i];

            int bx = margin_px + static_cast<int>(box.l * scale);
            int by = margin_px + static_cast<int>(box.t * scale);
            int bw = std::max(1, static_cast<int>(box.w * scale));
            int bh = std::max(1, static_cast<int>(box.h * scale));

            const auto& entry = store.get(store_idx);
            if (!entry.decoded.rgb_data.empty()) {
                auto resized = resize_image_to_fit(entry.decoded.rgb_data.data(),
                                                   entry.decoded.width, entry.decoded.height,
                                                   bw, bh);
                composite_image(preview_buf, preview_w, preview_h, resized,
                                std::max(1, static_cast<int>(entry.decoded.width * scale)),
                                std::max(1, static_cast<int>(entry.decoded.height * scale)),
                                bx, by, bw, bh);
            } else {
                draw_filled_rect(preview_buf, preview_w, preview_h, bx, by, bw, bh, 220, 220, 220);
            }
            draw_rect(preview_buf, preview_w, preview_h, bx, by, bw, bh, 1, 200, 200, 200);
        }
    } else {
        // Treemap Preview
        auto layout = calculate_treemap_layout(store, options);

        if (options.treemap_render_style == PDFTreemapRenderStyle::CUSHION_TREEMAP) {
            draw_filled_rect(preview_buf, preview_w, preview_h, margin_px, margin_px, layout_w, layout_h, 20, 20, 20);
        } else if (options.treemap_render_style == PDFTreemapRenderStyle::FILE_TYPE_COLORS) {
            draw_filled_rect(preview_buf, preview_w, preview_h, margin_px, margin_px, layout_w, layout_h, 30, 30, 30);
        }

        // Draw containers
        for (const auto& cbox : layout.container_boxes) {
            int cx = margin_px + static_cast<int>(cbox.x * scale);
            int cy = margin_px + static_cast<int>(cbox.y * scale);
            int cw = std::max(1, static_cast<int>(cbox.w * scale));
            int ch = std::max(1, static_cast<int>(cbox.h * scale));

            draw_rect(preview_buf, preview_w, preview_h, cx, cy, cw, ch, 1, 80, 80, 80);
            if (options.show_container_labels && !cbox.dir_name.empty() && cw > 30 && ch > 15) {
                draw_filled_rect(preview_buf, preview_w, preview_h, cx + 1, cy + 1, cw - 2, 10, 50, 50, 50);
                draw_text_simple(preview_buf, preview_w, preview_h, cx + 3, cy + 2, cbox.dir_name, 230, 230, 230, 1);
            }
        }

        // Draw leaf boxes
        for (const auto& box : layout.boxes) {
            int bx = margin_px + static_cast<int>(box.x * scale);
            int by = margin_px + static_cast<int>(box.y * scale);
            int bw = std::max(1, static_cast<int>(box.w * scale));
            int bh = std::max(1, static_cast<int>(box.h * scale));

            const auto& entry = store.get(box.image_index);

            if (options.treemap_render_style == PDFTreemapRenderStyle::ALL_THUMBNAILS && !entry.decoded.rgb_data.empty()) {
                auto resized = resize_image_to_fit(entry.decoded.rgb_data.data(),
                                                   entry.decoded.width, entry.decoded.height,
                                                   bw, bh);
                composite_image(preview_buf, preview_w, preview_h, resized,
                                std::max(1, static_cast<int>(entry.decoded.width * scale)),
                                std::max(1, static_cast<int>(entry.decoded.height * scale)),
                                bx, by, bw, bh);
            } else if (options.treemap_render_style == PDFTreemapRenderStyle::CUSHION_TREEMAP) {
                draw_cushion(preview_buf, preview_w, preview_h, bx, by, bw, bh,
                             box.cushion_ax, box.cushion_bx, box.cushion_ay, box.cushion_by);
            } else if (options.treemap_render_style == PDFTreemapRenderStyle::FILE_TYPE_COLORS) {
                auto col = FileTypeColors::get_color_rgb(entry.filepath);
                draw_filled_rect(preview_buf, preview_w, preview_h, bx, by, bw, bh,
                                 col.r, col.g, col.b);
            } else {
                draw_filled_rect(preview_buf, preview_w, preview_h, bx, by, bw, bh, 200, 200, 200);
            }
            draw_rect(preview_buf, preview_w, preview_h, bx, by, bw, bh, 1, 40, 40, 40);
        }
    }

    return preview_buf;
}

bool PDFGenerator::generate_from_store(
    const ImageStore& store,
    const std::string& output_path,
    const PDFOptions& options,
    DatabaseManager* db,
    std::function<void(int current_count, int total_count, const std::string& msg)> progress_cb,
    std::atomic<bool>* stop_requested)
{
    if (store.size() == 0) return false;

    pdfimg::PDFDocument pdf;

    int page_w = options.page_width_px();
    int page_h = options.page_height_px();
    int margin_px = options.page_margin_px();
    int layout_w = options.layout_width_px();
    int layout_h = options.layout_height_px();

    unsigned int hw = std::thread::hardware_concurrency();
    int num_threads = std::clamp(static_cast<int>(hw), 1, 16);

    if (options.layout_type == LayoutEngine::LayoutType::JUSTIFIED) {
        if (progress_cb) {
            progress_cb(0, static_cast<int>(store.size()), "Calculating page layout...");
        }

        std::vector<std::pair<size_t, double>> indexed_aspects;
        indexed_aspects.reserve(store.size());
        for (size_t i = 0; i < store.size(); ++i) {
            const auto& entry = store.get(i);
            double ar = (entry.aspect_ratio > 0.0) ? entry.aspect_ratio : 1.0;
            indexed_aspects.push_back({entry.index, ar});
        }

        std::vector<std::vector<Item>> boxes_per_page;
        auto indices_per_page = calculate_justified_pagination(indexed_aspects, boxes_per_page, options);

        if (boxes_per_page.empty()) return false;
        int total_pages = static_cast<int>(boxes_per_page.size());
        size_t total_images = store.size();
        std::atomic<size_t> global_rendered_images{0};
        std::mutex progress_mutex;

        for (size_t p = 0; p < boxes_per_page.size(); ++p) {
            if (stop_requested && stop_requested->load(std::memory_order_relaxed)) {
                return false;
            }

            std::vector<uint8_t> page_buffer(page_w * page_h * 3, 255);
            const auto& p_boxes = boxes_per_page[p];
            const auto& p_indices = indices_per_page[p];
            size_t page_item_count = p_boxes.size();

            int threads_to_use = std::min<int>(num_threads, static_cast<int>(page_item_count));
            if (threads_to_use < 1) threads_to_use = 1;

            std::atomic<bool> thread_cancelled{false};
            std::vector<std::thread> workers;
            workers.reserve(threads_to_use);

            for (int t = 0; t < threads_to_use; ++t) {
                workers.emplace_back([&, t, threads_to_use]() {
                    size_t start_i = (page_item_count * t) / threads_to_use;
                    size_t end_i = (page_item_count * (t + 1)) / threads_to_use;

                    for (size_t i = start_i; i < end_i; ++i) {
                        if ((stop_requested && stop_requested->load(std::memory_order_relaxed)) ||
                            thread_cancelled.load(std::memory_order_relaxed)) {
                            thread_cancelled.store(true, std::memory_order_relaxed);
                            return;
                        }

                        const auto& box = p_boxes[i];
                        size_t store_idx = p_indices[i];
                        const auto& entry = store.get(store_idx);

                        int bx = margin_px + static_cast<int>(box.l);
                        int by = margin_px + static_cast<int>(box.t);
                        int bw = static_cast<int>(box.w);
                        int bh = static_cast<int>(box.h);

                        std::vector<uint8_t> raw_rgb;
                        int img_w = 0, img_h = 0;

                        if (!entry.decoded.rgb_data.empty()) {
                            raw_rgb = entry.decoded.rgb_data;
                            img_w = entry.decoded.width;
                            img_h = entry.decoded.height;
                        } else if (db && !entry.content_hash.empty()) {
                            std::vector<uint8_t> jpeg_bytes;
                            std::vector<int> candidate_sizes = {512, 256, 128, 64};
                            for (int sz : candidate_sizes) {
                                std::string cand_key = entry.content_hash + ":" + std::to_string(sz);
                                if (db->get_key_data_concurrent(cand_key, jpeg_bytes)) {
                                    int ch = 0;
                                    unsigned char* pix = stbi_load_from_memory(jpeg_bytes.data(), static_cast<int>(jpeg_bytes.size()), &img_w, &img_h, &ch, 3);
                                    if (pix) {
                                        raw_rgb.assign(pix, pix + img_w * img_h * 3);
                                        stbi_image_free(pix);
                                        break;
                                    }
                                }
                            }
                        }

                        if (!raw_rgb.empty()) {
                            auto resized = resize_image_to_fit(raw_rgb.data(), img_w, img_h, bw, bh);
                            double scale = std::min(static_cast<double>(bw) / img_w, static_cast<double>(bh) / img_h);
                            int rw = static_cast<int>(img_w * scale);
                            int rh = static_cast<int>(img_h * scale);
                            composite_image(page_buffer, page_w, page_h, resized, rw, rh, bx, by, bw, bh);
                        } else {
                            draw_filled_rect(page_buffer, page_w, page_h, bx, by, bw, bh, 220, 220, 220);
                        }

                        size_t cur = ++global_rendered_images;
                        if (progress_cb && (cur % 15 == 0 || cur == total_images)) {
                            std::lock_guard<std::mutex> lock(progress_mutex);
                            std::string msg = "Page " + std::to_string(p + 1) + " of " + std::to_string(total_pages) +
                                              " - Rendering image " + std::to_string(cur) + " of " + std::to_string(total_images) + "...";
                            progress_cb(static_cast<int>(cur), static_cast<int>(total_images), msg);
                        }
                    }
                });
            }

            for (auto& w : workers) {
                if (w.joinable()) w.join();
            }

            if (thread_cancelled.load() || (stop_requested && stop_requested->load(std::memory_order_relaxed))) {
                return false;
            }

            pdf.add_image_page(page_buffer.data(), page_w, page_h, page_w * 3, true, pdfimg::CompressionType::None, options.page_dpi);
        }
    } else {
        // Treemap View
        if (progress_cb) {
            progress_cb(0, static_cast<int>(store.size()), "Calculating Treemap layout for PDF...");
        }

        auto layout = calculate_treemap_layout(store, options);
        std::vector<uint8_t> page_buffer(page_w * page_h * 3, 255);

        if (options.treemap_render_style == PDFTreemapRenderStyle::CUSHION_TREEMAP) {
            draw_filled_rect(page_buffer, page_w, page_h, margin_px, margin_px, layout_w, layout_h, 20, 20, 20);
        } else if (options.treemap_render_style == PDFTreemapRenderStyle::FILE_TYPE_COLORS) {
            draw_filled_rect(page_buffer, page_w, page_h, margin_px, margin_px, layout_w, layout_h, 30, 30, 30);
        }

        // Draw containers
        for (const auto& cbox : layout.container_boxes) {
            int cx = margin_px + static_cast<int>(cbox.x);
            int cy = margin_px + static_cast<int>(cbox.y);
            int cw = static_cast<int>(cbox.w);
            int ch = static_cast<int>(cbox.h);

            draw_rect(page_buffer, page_w, page_h, cx, cy, cw, ch, 2, 80, 80, 80);
            if (options.show_container_labels && !cbox.dir_name.empty() && cw > 60 && ch > 24) {
                int banner_h = std::clamp(static_cast<int>(options.page_dpi * 0.06), 14, 28);
                draw_filled_rect(page_buffer, page_w, page_h, cx + 2, cy + 2, cw - 4, banner_h, 50, 50, 50);
                int font_scale = std::max(1, static_cast<int>(options.page_dpi / 200.0));
                draw_text_simple(page_buffer, page_w, page_h, cx + 5, cy + 4, cbox.dir_name, 240, 240, 240, font_scale);
            }
        }

        size_t total_boxes = layout.boxes.size();
        std::atomic<size_t> global_rendered_boxes{0};
        std::mutex progress_mutex;

        int threads_to_use = std::min<int>(num_threads, static_cast<int>(total_boxes));
        if (threads_to_use < 1) threads_to_use = 1;

        std::atomic<bool> thread_cancelled{false};
        std::vector<std::thread> workers;
        workers.reserve(threads_to_use);

        for (int t = 0; t < threads_to_use; ++t) {
            workers.emplace_back([&, t, threads_to_use]() {
                size_t start_i = (total_boxes * t) / threads_to_use;
                size_t end_i = (total_boxes * (t + 1)) / threads_to_use;

                for (size_t i = start_i; i < end_i; ++i) {
                    if ((stop_requested && stop_requested->load(std::memory_order_relaxed)) ||
                        thread_cancelled.load(std::memory_order_relaxed)) {
                        thread_cancelled.store(true, std::memory_order_relaxed);
                        return;
                    }

                    const auto& box = layout.boxes[i];
                    int bx = margin_px + static_cast<int>(box.x);
                    int by = margin_px + static_cast<int>(box.y);
                    int bw = static_cast<int>(box.w);
                    int bh = static_cast<int>(box.h);

                    const auto& entry = store.get(box.image_index);

                    if (options.treemap_render_style == PDFTreemapRenderStyle::ALL_THUMBNAILS) {
                        std::vector<uint8_t> raw_rgb;
                        int img_w = 0, img_h = 0;

                        if (!entry.decoded.rgb_data.empty()) {
                            raw_rgb = entry.decoded.rgb_data;
                            img_w = entry.decoded.width;
                            img_h = entry.decoded.height;
                        } else if (db && !entry.content_hash.empty()) {
                            std::vector<uint8_t> jpeg_bytes;
                            std::vector<int> candidate_sizes = {512, 256, 128, 64};
                            for (int sz : candidate_sizes) {
                                std::string cand_key = entry.content_hash + ":" + std::to_string(sz);
                                if (db->get_key_data_concurrent(cand_key, jpeg_bytes)) {
                                    int ch = 0;
                                    unsigned char* pix = stbi_load_from_memory(jpeg_bytes.data(), static_cast<int>(jpeg_bytes.size()), &img_w, &img_h, &ch, 3);
                                    if (pix) {
                                        raw_rgb.assign(pix, pix + img_w * img_h * 3);
                                        stbi_image_free(pix);
                                        break;
                                    }
                                }
                            }
                        }

                        if (!raw_rgb.empty() && bw >= 4 && bh >= 4) {
                            auto resized = resize_image_to_fit(raw_rgb.data(), img_w, img_h, bw, bh);
                            double scale = std::min(static_cast<double>(bw) / img_w, static_cast<double>(bh) / img_h);
                            int rw = static_cast<int>(img_w * scale);
                            int rh = static_cast<int>(img_h * scale);
                            composite_image(page_buffer, page_w, page_h, resized, rw, rh, bx, by, bw, bh);
                        } else {
                            draw_filled_rect(page_buffer, page_w, page_h, bx, by, bw, bh, 200, 200, 200);
                        }
                    } else if (options.treemap_render_style == PDFTreemapRenderStyle::CUSHION_TREEMAP) {
                        draw_cushion(page_buffer, page_w, page_h, bx, by, bw, bh,
                                     box.cushion_ax, box.cushion_bx, box.cushion_ay, box.cushion_by);
                    } else if (options.treemap_render_style == PDFTreemapRenderStyle::FILE_TYPE_COLORS) {
                        auto col = FileTypeColors::get_color_rgb(entry.filepath);
                        draw_filled_rect(page_buffer, page_w, page_h, bx, by, bw, bh,
                                         col.r, col.g, col.b);
                    }
                    draw_rect(page_buffer, page_w, page_h, bx, by, bw, bh, 1, 30, 30, 30);

                    size_t cur = ++global_rendered_boxes;
                    if (progress_cb && (cur % 25 == 0 || cur == total_boxes)) {
                        std::lock_guard<std::mutex> lock(progress_mutex);
                        std::string msg = "Rendering tile " + std::to_string(cur) + " of " + std::to_string(total_boxes) + "...";
                        progress_cb(static_cast<int>(cur), static_cast<int>(total_boxes), msg);
                    }
                }
            });
        }

        for (auto& w : workers) {
            if (w.joinable()) w.join();
        }

        if (thread_cancelled.load() || (stop_requested && stop_requested->load(std::memory_order_relaxed))) {
            return false;
        }

        pdf.add_image_page(page_buffer.data(), page_w, page_h, page_w * 3, true, pdfimg::CompressionType::None, options.page_dpi);
    }

    if (progress_cb) {
        progress_cb(1, 1, "Saving PDF document to disk...");
    }

    return pdf.save(output_path);
}

bool PDFGenerator::generate_pdf(const std::vector<ImageInfo>& images, const std::string& output_path,
                                Timer& timer, StatusReporter& reporter, const PDFOptions& options,
                                DatabaseManager* db) {
    if (images.empty()) return false;

    // Convert ImageInfo vector into lightweight ImageStore
    ImageStore store;
    for (size_t i = 0; i < images.size(); ++i) {
        store.add_image(images[i].path, images[i].aspect_ratio,
                        images[i].orig_width, images[i].orig_height,
                        images[i].file_size, images[i].file_timestamp);
    }

    timer.start("PDF Export");
    reporter.update_status("Rendering PDF pages...");

    bool ok = generate_from_store(store, output_path, options, db,
        [&reporter](int curr, int total, const std::string& msg) {
            reporter.set_current_count(curr);
            reporter.set_total_count(total);
            reporter.update_status(msg);
        });

    timer.stop("PDF Export");
    return ok;
}

int run_headless_pdf(const std::string& pdf_path, const std::string& directory,
                     const std::string& db_path, const PDFOptions& options, bool verbose) {
    Timer timer;
    StatusReporter reporter(10);
    reporter.start();

    DatabaseManager db;
    if (!db.open(db_path)) {
        std::cerr << "Error: Failed to open database: " << db_path << std::endl;
        reporter.stop();
        return 1;
    }

    if (verbose) {
        std::cout << "Using database: " << db_path << std::endl;
    }

    bool scan_needed = !directory.empty();
    if (scan_needed) {
        if (!fs::exists(directory) || !fs::is_directory(directory)) {
            std::cerr << "Error: Directory does not exist: " << directory << std::endl;
            reporter.stop();
            return 1;
        }
        std::cout << "Scanning directory: " << directory << std::endl;
        int processed = db.scan_directory_parallel(directory, timer, reporter);
        if (processed < 0) {
            std::cerr << "Error: Failed to scan directory" << std::endl;
            reporter.stop();
            return 1;
        }
        std::cout << "Processed " << processed << " images" << std::endl;
    }

    timer.start("Database Query");
    reporter.update_status("Loading images from database...");
    std::vector<ImageInfo> images = db.get_all_images();
    timer.stop("Database Query");

    if (!directory.empty()) {
        std::vector<ImageInfo> filtered_images;
        std::string prefix = fs::path(directory).lexically_normal().string();
        if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\') {
            prefix += '/';
        }
        for (const auto& img : images) {
            std::string norm_path = fs::path(img.path).lexically_normal().string();
            if (norm_path.find(prefix) == 0 || norm_path == fs::path(directory).lexically_normal().string()) {
                filtered_images.push_back(img);
            }
        }
        images = std::move(filtered_images);
    }

    if (images.empty()) {
        std::cerr << "Error: No images found in database" << std::endl;
        reporter.stop();
        return 1;
    }

    std::cout << "Generating PDF with " << images.size() << " images: " << pdf_path << std::endl;

    PDFGenerator pdf_gen;
    if (!pdf_gen.generate_pdf(images, pdf_path, timer, reporter, options, &db)) {
        std::cerr << "Error: Failed to generate PDF" << std::endl;
        reporter.stop();
        return 1;
    }

    std::cout << "Successfully generated PDF: " << pdf_path << std::endl;
    reporter.stop();
    timer.print_summary();
    std::cout << "\nPDF export completed successfully!" << std::endl;
    return 0;
}
