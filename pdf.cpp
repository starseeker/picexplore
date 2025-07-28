/*
 * pdf.cpp - PDF generation logic for picscan
 *
 * Copyright (c) 2025 Clifford Yapp
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

#include "stb_image.h"
#include "stb_image_resize2.h"
#include "pdfimg.hpp"

PDFGenerator::PDFGenerator() {
}

PDFGenerator::~PDFGenerator() {
}

std::vector<std::vector<size_t>> PDFGenerator::calculate_pagination(const std::vector<ImageInfo>& images,
                                                                   std::vector<std::vector<Item>>& boxes_per_page,
                                                                   int row_height, int margin) {
    // Setup layout configuration
    LayoutCfg layout_cfg;
    layout_cfg.w = LAYOUT_WIDTH_PX;  // Available width
    layout_cfg.pl = layout_cfg.pr = layout_cfg.pt = layout_cfg.pb = 0; // No padding (we handle margins ourselves)
    layout_cfg.sh = layout_cfg.sv = margin; // Spacing between images
    layout_cfg.rh = row_height; // Target row height
    layout_cfg.tol = 0.25; // ±25% tolerance
    layout_cfg.widows = true;
    layout_cfg.ws = WidowStyle::Left;

    // Convert images to layout items
    std::vector<Item> items;
    for (const auto& img : images) {
        Item item;
        item.ar = img.aspect_ratio;
        items.push_back(item);
    }

    // Track exact mapping from boxes to images to keep indices in sync
    std::vector<std::vector<size_t>> image_indices_per_page;
    boxes_per_page.clear();

    size_t image_idx = 0;
    while (image_idx < items.size()) {
        std::vector<Item> page_boxes; 
        std::vector<size_t> page_indices;
        double current_y = 0;

        while (current_y < LAYOUT_HEIGHT_PX && image_idx < items.size()) {
            size_t remaining = items.size() - image_idx;
            std::vector<Item> remaining_items(items.begin() + image_idx, items.end());

            // Use maxRows to be as greedy as possible
            LayoutCfg temp_cfg = layout_cfg;
            temp_cfg.maxRows = std::max(1, static_cast<int>((LAYOUT_HEIGHT_PX - current_y) / row_height));
            JustifiedLayout layout(remaining_items, temp_cfg);

            if (layout.boxes().empty()) break;

            // Determine how many boxes (i.e., images) will actually fit in the current page vertically
            double layout_h = layout.height();
            size_t nboxes = layout.boxes().size();

            if (current_y + layout_h <= LAYOUT_HEIGHT_PX) {
                // All rows fit
                for (size_t b = 0; b < nboxes; ++b) {
                    Item box = layout.boxes()[b];
                    box.t += current_y;
                    page_boxes.push_back(box);
                    page_indices.push_back(image_idx + b);
                }
                image_idx += nboxes;
                current_y += layout_h;
            } else {
                // Try to fit rows one at a time
                size_t rows_fit = 0;
                double h_fit = 0;
                std::vector<Item> fit_boxes;
                std::vector<size_t> fit_indices;

                // We'll need to build up row-by-row, using maxRows=1 for each
                size_t local_idx = 0;
                double test_y = current_y;
                while (test_y < LAYOUT_HEIGHT_PX && (image_idx + local_idx) < items.size()) {
                    LayoutCfg row_cfg = layout_cfg;
                    row_cfg.maxRows = 1;
                    std::vector<Item> row_items(items.begin() + image_idx + local_idx, items.end());
                    JustifiedLayout row_layout(row_items, row_cfg);
                    if (row_layout.boxes().empty()) break;
                    double row_h = row_layout.height();
                    if (test_y + row_h > LAYOUT_HEIGHT_PX) break;
                    // Add this row
                    for (size_t rb = 0; rb < row_layout.boxes().size(); ++rb) {
                        Item box = row_layout.boxes()[rb];
                        box.t += test_y;
                        fit_boxes.push_back(box);
                        fit_indices.push_back(image_idx + local_idx + rb);
                    }
                    local_idx += row_layout.boxes().size();
                    test_y += row_h;
                    rows_fit++;
                }
                if (!fit_boxes.empty()) {
                    for (size_t b = 0; b < fit_boxes.size(); ++b) {
                        page_boxes.push_back(fit_boxes[b]);
                        page_indices.push_back(fit_indices[b]);
                    }
                    image_idx += fit_boxes.size();
                    current_y = test_y;
                }
                break; // Done with this page
            }
        }

        if (!page_boxes.empty() && !page_indices.empty()) {
            boxes_per_page.push_back(page_boxes);
            image_indices_per_page.push_back(page_indices);
        }
    }

    return image_indices_per_page;
}

std::vector<uint8_t> PDFGenerator::resize_image_to_fit(const std::vector<uint8_t>& image_data,
                                                      int src_width, int src_height,
                                                      int target_width, int target_height) {
    // Load original image
    int width, height, channels;
    stbi_uc* pixels = stbi_load_from_memory(image_data.data(), image_data.size(),
                                            &width, &height, &channels, 3);
    if (!pixels) {
        return {};
    }

    // Calculate scale to fit within target bounds
    double scale_x = static_cast<double>(target_width) / width;
    double scale_y = static_cast<double>(target_height) / height;
    double scale = std::min(scale_x, scale_y);

    int new_width = static_cast<int>(width * scale);
    int new_height = static_cast<int>(height * scale);

    // Resize image
    std::vector<uint8_t> resized(new_width * new_height * 3);
    if (!stbir_resize_uint8_linear(pixels, width, height, 0,
                                   resized.data(), new_width, new_height, 0, STBIR_RGB)) {
        stbi_image_free(pixels);
        return {};
    }

    stbi_image_free(pixels);
    return resized;
}

void PDFGenerator::composite_image(std::vector<uint8_t>& page_buffer, int page_width, int page_height,
                                  const std::vector<uint8_t>& image_data, int img_width, int img_height,
                                  int x, int y, int box_width, int box_height) {
    // Center image within the box
    int offset_x = x + (box_width - img_width) / 2;
    int offset_y = y + (box_height - img_height) / 2;

    for (int row = 0; row < img_height; row++) {
        int page_y = offset_y + row;
        if (page_y < 0 || page_y >= page_height) continue;

        int page_row_start = page_y * page_width * 3;
        int img_row_start = row * img_width * 3;

        for (int col = 0; col < img_width; col++) {
            int page_x = offset_x + col;
            if (page_x < 0 || page_x >= page_width) continue;

            int page_pixel = page_row_start + page_x * 3;
            int img_pixel = img_row_start + col * 3;

            page_buffer[page_pixel] = image_data[img_pixel];         // R
            page_buffer[page_pixel + 1] = image_data[img_pixel + 1]; // G
            page_buffer[page_pixel + 2] = image_data[img_pixel + 2]; // B
        }
    }
}

bool PDFGenerator::generate_pdf(const std::vector<ImageInfo>& images, const std::string& output_path,
                               Timer& timer, StatusReporter& reporter, 
                               int row_height, int margin) {
    if (images.empty()) {
        return false;
    }

    timer.start("PDF Layout");
    reporter.update_status("Calculating page layout...");

    std::vector<std::vector<Item>> boxes_per_page;
    std::vector<std::vector<size_t>> image_indices_per_page = calculate_pagination(images, boxes_per_page, row_height, margin);

    timer.stop("PDF Layout");

    if (boxes_per_page.empty()) {
        return false;
    }

    reporter.set_total_count(boxes_per_page.size());
    reporter.update_status("Rendering PDF pages...");

    timer.start("PDF Rendering");

    pdfimg::PDFDocument pdf;

    for (size_t page_idx = 0; page_idx < boxes_per_page.size(); ++page_idx) {
        reporter.set_current_count(page_idx + 1);

        // Create page buffer (white background)
        std::vector<uint8_t> page_buffer(PAGE_WIDTH_PX * PAGE_HEIGHT_PX * 3, 255);

        for (size_t item_idx = 0; item_idx < boxes_per_page[page_idx].size(); ++item_idx) {
            size_t img_idx = image_indices_per_page[page_idx][item_idx];
            if (img_idx >= images.size()) {
                continue;
            }
            const Item& box = boxes_per_page[page_idx][item_idx];
            const ImageInfo& img = images[img_idx];

            int x = PAGE_MARGIN_PX + static_cast<int>(box.l);
            int y = PAGE_MARGIN_PX + static_cast<int>(box.t);
            int w = static_cast<int>(box.w);
            int h = static_cast<int>(box.h);

            std::vector<uint8_t> resized = resize_image_to_fit(
                img.thumb_data, img.thumb_width, img.thumb_height, w, h
            );
            if (!resized.empty()) {
                double scale = std::min(static_cast<double>(w) / img.thumb_width,
                                        static_cast<double>(h) / img.thumb_height);
                int resized_w = static_cast<int>(img.thumb_width * scale);
                int resized_h = static_cast<int>(img.thumb_height * scale);

                composite_image(page_buffer, PAGE_WIDTH_PX, PAGE_HEIGHT_PX,
                                resized, resized_w, resized_h, x, y, w, h);
            }
        }

        pdf.add_image_page(page_buffer.data(), PAGE_WIDTH_PX, PAGE_HEIGHT_PX,
                           PAGE_WIDTH_PX * 3, true, pdfimg::CompressionType::None, PAGE_DPI);
    }

    timer.stop("PDF Rendering");
    timer.start("PDF Writing");

    bool success = pdf.save(output_path);

    timer.stop("PDF Writing");

    if (success) {
        reporter.update_status("PDF generation complete");
    }

    return success;
}