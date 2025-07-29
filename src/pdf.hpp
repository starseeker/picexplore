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
#include "database.hpp"
#include "utils.hpp"
#include "justified_layout.hpp"

// PDF and layout configuration options
struct PDFOptions {
    // Page dimensions (inches)
    double page_width_inches = 8.5;
    double page_height_inches = 11.0;

    // DPI settings
    double page_dpi = 300.0;

    // Page margin (inches)
    double page_margin_inches = 0.5;

    // Layout settings (pixels)
    int row_height = 150;          // Target row height
    int margin = 10;               // Spacing between images
    int pad_top = 0;               // Layout padding top
    int pad_bottom = 0;            // Layout padding bottom
    int pad_left = 0;              // Layout padding left
    int pad_right = 0;             // Layout padding right

    // Computed values (pixels)
    int page_width_px() const { return static_cast<int>(page_width_inches * page_dpi); }
    int page_height_px() const { return static_cast<int>(page_height_inches * page_dpi); }
    int page_margin_px() const { return static_cast<int>(page_margin_inches * page_dpi); }
    int layout_width_px() const { return page_width_px() - (2 * page_margin_px()); }
    int layout_height_px() const { return page_height_px() - (2 * page_margin_px()); }
};

// PDF generator class
class PDFGenerator {
    public:
	PDFGenerator();
	~PDFGenerator();

	bool generate_pdf(const std::vector<ImageInfo>& images, const std::string& output_path,
		Timer& timer, StatusReporter& reporter, const PDFOptions& options);

    private:
	// Layout and rendering functions
	std::vector<std::vector<size_t>> calculate_pagination(const std::vector<ImageInfo>& images,
		std::vector<std::vector<Item>>& boxes_per_page,
		const PDFOptions& options);

	std::vector<uint8_t> resize_image_to_fit(const std::vector<uint8_t>& image_data,
		int src_width, int src_height,
		int target_width, int target_height);

	void composite_image(std::vector<uint8_t>& page_buffer, int page_width, int page_height,
		const std::vector<uint8_t>& image_data, int img_width, int img_height,
		int x, int y, int box_width, int box_height);
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
