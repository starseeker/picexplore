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
    std::cout << "[PASS] 10,000 items flat layout in " << duration_ms << " ms\n";
    assert(duration_ms < 50); // Performance requirement: < 50ms for 10k items
}

void test_hierarchical_treemap() {
    std::vector<HierarchicalTreemapItem> items = {
        {0, "/photos/2023/vacation/pic1.jpg", 500.0, 1.5},
        {1, "/photos/2023/vacation/pic2.jpg", 300.0, 1.0},
        {2, "/photos/2023/work/project1.png", 400.0, 1.3},
        {3, "/photos/2024/summer/beach.jpg", 600.0, 1.0},
        {4, "/photos/2024/summer/trip/day1.jpg", 200.0, 1.0},
        {5, "/photos/2024/summer/trip/day2.jpg", 200.0, 1.0},
        {6, "/photos/root_file.jpg", 150.0, 1.0}
    };

    double W = 1200.0;
    double H = 800.0;
    auto res = HierarchicalTreemap::compute(items, "/photos", 0, 0, W, H, 2.0, 4.0, 18.0);

    assert(res.boxes.size() == items.size());
    assert(!res.container_boxes.empty());

    // Verify all leaf boxes within boundaries
    for (const auto& b : res.boxes) {
        assert(b.x >= 0.0);
        assert(b.y >= 0.0);
        assert(b.x + b.w <= W + 1e-3);
        assert(b.y + b.h <= H + 1e-3);
        assert(b.w > 0.0);
        assert(b.h > 0.0);
    }

    // Verify container boxes
    for (const auto& c : res.container_boxes) {
        assert(c.depth > 0);
        assert(!c.dir_name.empty());
        assert(c.x >= 0.0);
        assert(c.y >= 0.0);
        assert(c.x + c.w <= W + 1e-3);
        assert(c.y + c.h <= H + 1e-3);
    }

    // Export SVG for hierarchical visualization
    svg::Dimensions d(W, H);
    svg::Document doc("hierarchical_treemap_test.svg", svg::Layout(d, svg::Layout::TopLeft));

    // Draw background
    doc << svg::Rectangle(svg::Point(0, 0), W, H, svg::Fill(svg::Color(25, 25, 25)));

    // Draw container boxes
    for (const auto& c : res.container_boxes) {
        svg::Color border_col = (c.depth == 1) ? svg::Color(90, 95, 110) : svg::Color(130, 140, 160);
        svg::Color bg_col = (c.depth == 1) ? svg::Color(35, 38, 44) : svg::Color(45, 48, 56);
        doc << svg::Rectangle(svg::Point(c.x, c.y), c.w, c.h, svg::Fill(bg_col), svg::Stroke(1.5, border_col));
    }

    // Draw leaf boxes
    for (const auto& b : res.boxes) {
        doc << svg::Rectangle(svg::Point(b.x, b.y), b.w, b.h, svg::Fill(svg::Color(41, 128, 185)), svg::Stroke(1, svg::Color(20, 20, 20)));
    }
    doc.save();

    std::cout << "[PASS] Hierarchical treemap layout and SVG export\n";
}

void test_hierarchical_performance() {
    std::vector<HierarchicalTreemapItem> items;
    items.reserve(10000);
    for (size_t i = 0; i < 10000; ++i) {
        int year = 2015 + (i % 10);
        int month = 1 + (i % 12);
        int day = 1 + (i % 28);
        std::string path = "/gallery/" + std::to_string(year) + "/" + std::to_string(month) + "/" + std::to_string(day) + "/img_" + std::to_string(i) + ".jpg";
        double weight = 50.0 + (i % 1000);
        items.push_back({i, path, weight, 1.0});
    }

    auto start = std::chrono::steady_clock::now();
    auto res = HierarchicalTreemap::compute(items, "/gallery", 0, 0, 1920, 1080, 1.0, 3.0, 16.0);
    auto end = std::chrono::steady_clock::now();

    assert(res.boxes.size() == 10000);
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "[PASS] 10,000 items hierarchical tree build & layout in " << duration_ms << " ms\n";
    assert(duration_ms < 50);
}

int main() {
    try {
        test_empty();
        test_single_item();
        test_multiple_items_and_svg();
        test_performance_10k_items();
        test_hierarchical_treemap();
        test_hierarchical_performance();
        std::cout << "All Treemap layout unit tests passed successfully!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
