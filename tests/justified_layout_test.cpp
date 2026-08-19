/*
 * justified_layout_test.cpp - Example justified layout test with SVG output
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
 *
 * (Note - this is a Github Copilot GPT-4.1 reworking, updating and refactor of
 * the original jltest.cxx file to use the new C++ conversion of the flickr
 * justified layout algorithm also handled with Copilot.)
 */

#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <sstream>
#include <limits>
#include <memory> // Needed for simple-svg
#include "simple_svg_1.0.0.hpp"
#include "justified_layout.hpp"

namespace {

std::vector<double> parse_args(int argc, const char* argv[]) {
    static const std::vector<double> default_sizes{
        400, 200, 100, 300, 500, 500, 190, 20, 111, 55, 900, 600, 300, 250, 500, 300
    };
    if (argc == 1) {
        return default_sizes;
    }
    if (argc % 2 != 1) {
        throw std::invalid_argument("Format is: jltest x1 y1 [x2 y2]...");
    }
    std::vector<double> sizes;
    sizes.reserve(argc - 1);
    for (int i = 1; i < argc; ++i) {
        try {
            size_t idx = 0;
            std::string s(argv[i]);
            double val = std::stod(s, &idx);
            if (idx != s.size()) {
                throw std::invalid_argument("Invalid number: " + s);
            }
            if (val < 0) {
                throw std::invalid_argument("Negative value not supported: " + s);
            }
            sizes.push_back(val);
        } catch (const std::exception& e) {
            throw std::invalid_argument("Argument " + std::to_string(i) + " (" + argv[i] + "): " + e.what());
        }
    }
    return sizes;
}

std::vector<Item> make_items(const std::vector<double>& sizes) {
    if (sizes.size() % 2 != 0) {
        throw std::invalid_argument("You need to provide an even number of sizes (pairs of width/height).");
    }
    std::vector<Item> items;
    items.reserve(sizes.size() / 2);
    for (size_t i = 0; i < sizes.size(); i += 2) {
        double w = sizes[i];
        double h = sizes[i + 1];
        if (h == 0.0) {
            throw std::invalid_argument("Height for item " + std::to_string(i/2) + " is zero.");
        }
        items.push_back(Item{w / h});
    }
    return items;
}

void output_layout_svg(const std::vector<Item>& boxes, double container_width, double container_height, const std::string& filename) {
    svg::Dimensions d(container_width, container_height);
    svg::Document svg_out(filename, svg::Layout(d, svg::Layout::TopLeft));
    double last_top = -1;
    int row_idx = 0, item_idx_in_row = 0;
    for (const auto& item : boxes) {
        if (item.t != last_top) {
            if (row_idx > 0) std::cout << "\n";
            std::cout << "Row " << row_idx << ":\n";
            std::cout << "  Pos(top, left, width, height): " << item.t << "," << item.l << "," << (container_width) << "," << item.h << "\n";
            item_idx_in_row = 0;
            last_top = item.t;
            ++row_idx;
        }
        std::cout << "     Item " << item_idx_in_row << "(top, left, width, height): " << item.t << "," << item.l << "," << item.w << "," << item.h << "\n";
        svg_out << svg::Rectangle(
            svg::Point(item.l, item.t),
            item.w,
            item.h,
            svg::Fill(svg::Color::Yellow),
            svg::Stroke(1, svg::Color::Black) // add a border
        );
        ++item_idx_in_row;
    }
    svg_out.save();
}

} // namespace

int main(int argc, const char* argv[]) {
    try {
        auto start = std::chrono::steady_clock::now();

        auto sizes = parse_args(argc, argv);
        auto items = make_items(sizes);

        LayoutCfg cfg;
        cfg.w = 1060;
        cfg.rh = 320;

        auto layout_start = std::chrono::steady_clock::now();
        JustifiedLayout jl(items, cfg);
        auto layout_end = std::chrono::steady_clock::now();

        std::cout << "Layout time: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(layout_end - layout_start).count()
                  << " ms.\n";

        output_layout_svg(jl.boxes(), cfg.w, jl.height(), "jrl.svg");

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << '\n';
        return 1;
    }
}
