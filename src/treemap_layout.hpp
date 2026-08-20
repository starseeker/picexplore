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

        // O(1) worst ratio evaluator for descending sorted areas
        auto worst_ratio = [&](size_t start_idx, size_t end_idx, double row_sum, double side_length) -> double {
            if (side_length <= 0.0 || row_sum <= 0.0) {
                return std::numeric_limits<double>::max();
            }
            double a_max = areas[start_idx];
            double a_min = areas[end_idx];
            if (a_min <= 0.0) return std::numeric_limits<double>::max();

            double s2 = side_length * side_length;
            double sum2 = row_sum * row_sum;
            double r1 = (s2 * a_max) / sum2;
            double r2 = sum2 / (s2 * a_min);
            return std::max(r1, r2);
        };

        auto layout_row = [&](size_t start_idx, size_t end_idx, double row_sum) {
            if (cur_w <= 0.0 || cur_h <= 0.0) return;

            bool vertical_strip = (cur_w >= cur_h);
            double side_length = vertical_strip ? cur_h : cur_w;
            double strip_thickness = (side_length > 0.0) ? (row_sum / side_length) : 0.0;

            if (vertical_strip) {
                double strip_w = std::min(strip_thickness, cur_w);
                double item_y = cur_y;
                for (size_t idx = start_idx; idx <= end_idx; ++idx) {
                    double item_h = (strip_w > 0.0) ? (areas[idx] / strip_w) : 0.0;
                    if (idx == end_idx) {
                        // Avoid rounding gaps at the strip end
                        item_h = std::max(0.0, (cur_y + cur_h) - item_y);
                    }

                    results.push_back(TreemapBox{
                        items[idx].id,
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
                for (size_t idx = start_idx; idx <= end_idx; ++idx) {
                    double item_w = (strip_h > 0.0) ? (areas[idx] / strip_h) : 0.0;
                    if (idx == end_idx) {
                        item_w = std::max(0.0, (cur_x + cur_w) - item_x);
                    }

                    results.push_back(TreemapBox{
                        items[idx].id,
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

        size_t row_start = 0;
        size_t row_end = 0;
        double row_area_sum = 0.0;
        bool in_row = false;

        for (size_t i = 0; i < items.size(); ++i) {
            double side = std::min(cur_w, cur_h);
            if (side <= 0.0) {
                if (in_row) {
                    layout_row(row_start, row_end, row_area_sum);
                    in_row = false;
                }
                results.push_back(TreemapBox{items[i].id, cur_x, cur_y, 0.0, 0.0});
                continue;
            }

            if (!in_row) {
                row_start = i;
                row_end = i;
                row_area_sum = areas[i];
                in_row = true;
            } else {
                double current_worst = worst_ratio(row_start, row_end, row_area_sum, side);
                double candidate_sum = row_area_sum + areas[i];
                double candidate_worst = worst_ratio(row_start, i, candidate_sum, side);

                if (candidate_worst <= current_worst) {
                    row_end = i;
                    row_area_sum = candidate_sum;
                } else {
                    layout_row(row_start, row_end, row_area_sum);
                    row_start = i;
                    row_end = i;
                    row_area_sum = areas[i];
                }
            }
        }

        if (in_row) {
            layout_row(row_start, row_end, row_area_sum);
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
