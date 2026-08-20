#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

struct TreemapItem {
    size_t id = 0;
    double weight = 1.0;
    double aspect_ratio = 1.0;
};

struct TreemapBox {
    size_t id = 0;
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

class SquarifiedTreemap {
public:
    static std::vector<TreemapBox> compute(const std::vector<TreemapItem>& input_items,
                                          double bounds_x, double bounds_y,
                                          double bounds_w, double bounds_h,
                                          double padding = 2.0) {
        std::vector<TreemapBox> results;
        if (input_items.empty() || bounds_w <= 0.0 || bounds_h <= 0.0) {
            return results;
        }

        // Copy and sort items descending by weight
        std::vector<TreemapItem> items = input_items;
        std::sort(items.begin(), items.end(), [](const TreemapItem& a, const TreemapItem& b) {
            return a.weight > b.weight;
        });

        // Compute total weight (ensuring positive weights)
        double total_weight = 0.0;
        for (const auto& item : items) {
            total_weight += std::max(1e-9, item.weight);
        }

        if (total_weight <= 0.0) {
            return results;
        }

        // Calculate normalized areas for each item matching total bounding area
        double total_area = bounds_w * bounds_h;
        std::vector<double> areas(items.size());
        for (size_t i = 0; i < items.size(); ++i) {
            areas[i] = total_area * (std::max(1e-9, items[i].weight) / total_weight);
        }

        results.reserve(items.size());

        double cur_x = bounds_x;
        double cur_y = bounds_y;
        double cur_w = bounds_w;
        double cur_h = bounds_h;

        std::vector<size_t> current_row;
        double row_area_sum = 0.0;

        auto worst_ratio = [](const std::vector<size_t>& row, double row_sum,
                                 const std::vector<double>& item_areas, double side_length) -> double {
            if (row.empty() || side_length <= 0.0 || row_sum <= 0.0) {
                return std::numeric_limits<double>::max();
            }
            double s2 = side_length * side_length;
            double sum2 = row_sum * row_sum;
            double worst = 0.0;
            for (size_t idx : row) {
                double a = item_areas[idx];
                if (a <= 0.0) continue;
                double r1 = (s2 * a) / sum2;
                double r2 = sum2 / (s2 * a);
                double r = std::max(r1, r2);
                if (r > worst) worst = r;
            }
            return worst;
        };

        auto layout_row = [&](const std::vector<size_t>& row, double row_sum) {
            if (row.empty() || cur_w <= 0.0 || cur_h <= 0.0) return;

            bool vertical_strip = (cur_w >= cur_h);
            double side_length = vertical_strip ? cur_h : cur_w;
            double strip_thickness = (side_length > 0.0) ? (row_sum / side_length) : 0.0;

            if (vertical_strip) {
                double strip_w = std::min(strip_thickness, cur_w);
                double item_y = cur_y;
                for (size_t i = 0; i < row.size(); ++i) {
                    size_t item_idx = row[i];
                    double item_h = (strip_w > 0.0) ? (areas[item_idx] / strip_w) : 0.0;
                    if (i == row.size() - 1) {
                        // Avoid rounding gaps at the strip end
                        item_h = std::max(0.0, (cur_y + cur_h) - item_y);
                    }

                    results.push_back(TreemapBox{
                        items[item_idx].id,
                        cur_x,
                        item_y,
                        strip_w,
                        item_h
                    });
                    item_y += item_h;
                }
                cur_x += strip_w;
                cur_w = std::max(0.0, cur_w - strip_w);
            } else {
                double strip_h = std::min(strip_thickness, cur_h);
                double item_x = cur_x;
                for (size_t i = 0; i < row.size(); ++i) {
                    size_t item_idx = row[i];
                    double item_w = (strip_h > 0.0) ? (areas[item_idx] / strip_h) : 0.0;
                    if (i == row.size() - 1) {
                        item_w = std::max(0.0, (cur_x + cur_w) - item_x);
                    }

                    results.push_back(TreemapBox{
                        items[item_idx].id,
                        item_x,
                        cur_y,
                        item_w,
                        strip_h
                    });
                    item_x += item_w;
                }
                cur_y += strip_h;
                cur_h = std::max(0.0, cur_h - strip_h);
            }
        };

        for (size_t i = 0; i < items.size(); ++i) {
            double side = std::min(cur_w, cur_h);
            if (side <= 0.0) {
                // If space is exhausted, append remaining with zero dimensions
                results.push_back(TreemapBox{items[i].id, cur_x, cur_y, 0.0, 0.0});
                continue;
            }

            if (current_row.empty()) {
                current_row.push_back(i);
                row_area_sum = areas[i];
            } else {
                double current_worst = worst_ratio(current_row, row_area_sum, areas, side);

                std::vector<size_t> candidate_row = current_row;
                candidate_row.push_back(i);
                double candidate_sum = row_area_sum + areas[i];
                double candidate_worst = worst_ratio(candidate_row, candidate_sum, areas, side);

                if (candidate_worst <= current_worst) {
                    current_row.push_back(i);
                    row_area_sum = candidate_sum;
                } else {
                    layout_row(current_row, row_area_sum);
                    current_row.clear();
                    current_row.push_back(i);
                    row_area_sum = areas[i];
                }
            }
        }

        if (!current_row.empty()) {
            layout_row(current_row, row_area_sum);
        }

        // Apply padding between rectangles if requested
        if (padding > 0.0) {
            double half_pad = padding / 2.0;
            for (auto& box : results) {
                if (box.w > padding && box.h > padding) {
                    box.x += half_pad;
                    box.y += half_pad;
                    box.w -= padding;
                    box.h -= padding;
                }
            }
        }

        return results;
    }
};
