/*
 * pdf.h - PDF generation logic for picscan
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

#pragma once

#include <string>
#include <vector>
#include "database.h"
#include "utils.h"
#include "justified_layout.hpp"

// PDF generator class
class PDFGenerator {
public:
    PDFGenerator();
    ~PDFGenerator();
    
    bool generate_pdf(const std::vector<ImageInfo>& images, const std::string& output_path,
                     Timer& timer, StatusReporter& reporter, 
                     int row_height = 150, int margin = 10,
                     int pad_top = 0, int pad_bottom = 0, int pad_left = 0, int pad_right = 0);

private:
    // Layout configuration
    static constexpr double PAGE_WIDTH_INCHES = 8.5;
    static constexpr double PAGE_HEIGHT_INCHES = 11.0;
    static constexpr double PAGE_DPI = 300.0;
    static constexpr double PAGE_MARGIN_INCHES = 0.5;
    
    static constexpr int PAGE_WIDTH_PX = static_cast<int>(PAGE_WIDTH_INCHES * PAGE_DPI);
    static constexpr int PAGE_HEIGHT_PX = static_cast<int>(PAGE_HEIGHT_INCHES * PAGE_DPI);
    static constexpr int PAGE_MARGIN_PX = static_cast<int>(PAGE_MARGIN_INCHES * PAGE_DPI);
    static constexpr int LAYOUT_WIDTH_PX = PAGE_WIDTH_PX - (2 * PAGE_MARGIN_PX);
    static constexpr int LAYOUT_HEIGHT_PX = PAGE_HEIGHT_PX - (2 * PAGE_MARGIN_PX);
    
    // Layout and rendering functions
    std::vector<std::vector<size_t>> calculate_pagination(const std::vector<ImageInfo>& images,
                                                        std::vector<std::vector<Item>>& boxes_per_page,
                                                        int row_height, int margin,
                                                        int pad_top, int pad_bottom, int pad_left, int pad_right);
    
    std::vector<uint8_t> resize_image_to_fit(const std::vector<uint8_t>& image_data,
                                           int src_width, int src_height,
                                           int target_width, int target_height);
                                           
    void composite_image(std::vector<uint8_t>& page_buffer, int page_width, int page_height,
                        const std::vector<uint8_t>& image_data, int img_width, int img_height,
                        int x, int y, int box_width, int box_height);
};