#pragma once

#include <vector>
#include <utility>
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

    // Full recompute. Each pair is {raw_store_index, aspect_ratio}.
    // box.image_index will equal the raw_store_index from each pair.
    LayoutResult compute(const std::vector<std::pair<size_t,double>>& indexed_aspects,
                         double viewport_width,
                         double target_row_height = 150.0);

    // Incremental append
    LayoutResult append(const std::vector<std::pair<size_t,double>>& indexed_aspects);

private:
    LayoutCfg cfg_;
    double last_viewport_width_ = 0.0;
};
