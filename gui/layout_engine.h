#pragma once

#include <vector>
#include "../justified_layout.hpp"

class LayoutEngine {
public:
    struct LayoutResult {
        struct Box {
            size_t image_index;
            double x, y, w, h;
        };
        std::vector<Box> boxes;
        double total_height = 0.0;
    };

    LayoutEngine();
    ~LayoutEngine();

    // Full recompute
    LayoutResult compute(const std::vector<double>& aspect_ratios,
                         double viewport_width,
                         double target_row_height = 150.0);

    // Incremental append
    LayoutResult append(const std::vector<double>& all_aspect_ratios);

private:
    LayoutCfg cfg_;
    double last_viewport_width_ = 0.0;
};
