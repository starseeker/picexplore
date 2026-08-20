#pragma once

#include <vector>
#include <utility>
#include "../justified_layout.hpp"
#include "../treemap_layout.hpp"

class LayoutEngine {
public:
    enum class LayoutType {
        JUSTIFIED,
        TREEMAP
    };

    enum class TreemapMetric {
        FILE_SIZE,
        PIXEL_AREA,
        EQUAL_SIZE
    };

    struct LayoutResult {
        struct Box {
            size_t image_index;
            double x, y, w, h;
        };
        std::vector<Box> boxes;
        double total_height = 0.0;
        LayoutType layout_type = LayoutType::JUSTIFIED;
    };

    LayoutEngine();
    ~LayoutEngine();

    // Full recompute for Justified Layout. Each pair is {raw_store_index, aspect_ratio}.
    // box.image_index will equal the raw_store_index from each pair.
    LayoutResult compute(const std::vector<std::pair<size_t,double>>& indexed_aspects,
                         double viewport_width,
                         double target_row_height = 150.0);

    LayoutResult compute_justified(const std::vector<std::pair<size_t,double>>& indexed_aspects,
                                   double viewport_width,
                                   double target_row_height = 150.0);

    // Full recompute for Squarified Treemap Layout.
    LayoutResult compute_treemap(const std::vector<TreemapItem>& items,
                                double viewport_width,
                                double viewport_height,
                                double padding = 2.0);

    // Incremental append (Justified)
    LayoutResult append(const std::vector<std::pair<size_t,double>>& indexed_aspects);

private:
    LayoutCfg cfg_;
    double last_viewport_width_ = 0.0;
    std::vector<Item> items_scratch_;
};
