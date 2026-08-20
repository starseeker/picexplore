#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include "../src/treemap_layout.hpp"
#include "simple_svg_1.0.0.hpp"

void test_empty() {
    std::vector<TreemapItem> items;
    auto boxes = SquarifiedTreemap::compute(items, 0, 0, 800, 600, 2.0);
    assert(boxes.empty());
    std::cout << "[PASS] Empty items test\n";
}

void test_single_item() {
    std::vector<TreemapItem> items = {{1, 100.0, 1.5}};
    auto boxes = SquarifiedTreemap::compute(items, 0, 0, 800, 600, 0.0);
    assert(boxes.size() == 1);
    assert(std::abs(boxes[0].w - 800.0) < 1e-4);
    assert(std::abs(boxes[0].h - 600.0) < 1e-4);
    assert(boxes[0].id == 1);
    std::cout << "[PASS] Single item test\n";
}

void test_multiple_items_and_svg() {
    std::vector<TreemapItem> items = {
        {0, 600.0, 1.0},
        {1, 600.0, 1.0},
        {2, 400.0, 1.0},
        {3, 400.0, 1.0},
        {4, 200.0, 1.0},
        {5, 200.0, 1.0},
        {6, 100.0, 1.0},
        {7, 50.0,  1.0},
        {8, 50.0,  1.0},
        {9, 25.0,  1.0}
    };

    double W = 1000.0;
    double H = 600.0;
    double padding = 2.0;

    auto boxes = SquarifiedTreemap::compute(items, 0, 0, W, H, padding);
    assert(boxes.size() == items.size());

    // Verify all boxes are within bounds
    for (const auto& b : boxes) {
        assert(b.x >= 0.0);
        assert(b.y >= 0.0);
        assert(b.x + b.w <= W + 1e-3);
        assert(b.y + b.h <= H + 1e-3);
        assert(b.w > 0.0);
        assert(b.h > 0.0);
    }

    // Output SVG document for visual validation
    svg::Dimensions d(W, H);
    svg::Document doc("treemap_test.svg", svg::Layout(d, svg::Layout::TopLeft));

    std::vector<svg::Color> colors = {
        svg::Color(230, 126, 34),
        svg::Color(41, 128, 185),
        svg::Color(39, 174, 96),
        svg::Color(142, 68, 173),
        svg::Color(74, 105, 189),
        svg::Color(87, 96, 111),
        svg::Color(26, 188, 156),
        svg::Color(231, 76, 60),
        svg::Color(22, 160, 133),
        svg::Color(116, 125, 140)
    };

    for (size_t i = 0; i < boxes.size(); ++i) {
        const auto& b = boxes[i];
        svg::Color fill = colors[b.id % colors.size()];
        doc << svg::Rectangle(
            svg::Point(b.x, b.y),
            b.w,
            b.h,
            svg::Fill(fill),
            svg::Stroke(1, svg::Color(20, 20, 20))
        );
    }
    doc.save();

    std::cout << "[PASS] Multiple items and SVG export test\n";
}

void test_performance_10k_items() {
    std::vector<TreemapItem> items;
    items.reserve(10000);
    for (size_t i = 0; i < 10000; ++i) {
        double weight = 100.0 + (i % 500) * 10.0 + (i % 17);
        items.push_back({i, weight, 1.0});
    }

    auto start = std::chrono::steady_clock::now();
    auto boxes = SquarifiedTreemap::compute(items, 0, 0, 1920, 1080, 1.0);
    auto end = std::chrono::steady_clock::now();

    assert(boxes.size() == 10000);
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "[PASS] 10,000 items layout in " << duration_ms << " ms\n";
    assert(duration_ms < 50); // Performance requirement: < 50ms for 10k items
}

int main() {
    try {
        test_empty();
        test_single_item();
        test_multiple_items_and_svg();
        test_performance_10k_items();
        std::cout << "All Treemap layout unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
