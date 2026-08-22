#pragma once

#include <vector>
#include <utility>
#include "../justified_layout.hpp"
#include "../treemap_layout.hpp"

class LayoutEngine {
public:
    enum class LayoutType {
        JUSTIFIED,
        TREEMAP,
        HIERARCHICAL_TREEMAP
    };

    enum class TreemapMetric {
        FILE_SIZE,
        PIXEL_AREA,
        DUPLICATE_COUNT,
        EQUAL_SIZE
    };

    struct LayoutResult {
        struct Box {
            size_t image_index;
            double x, y, w, h;
            double cushion_ax = 0.0;
            double cushion_bx = 0.0;
            double cushion_ay = 0.0;
            double cushion_by = 0.0;
        };
        std::vector<Box> boxes;
        std::vector<ContainerBox> container_boxes;
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

    // Full recompute for Squarified Treemap Layout (Flat).
    LayoutResult compute_treemap(const std::vector<TreemapItem>& items,
                                double viewport_width,
                                double viewport_height,
                                double padding = 2.0);

    // Full recompute for Hierarchical Squarified Treemap Layout (Nested).
    LayoutResult compute_hierarchical_treemap(const std::vector<HierarchicalTreemapItem>& items,
                                              const std::string& base_root_dir,
                                              const std::string& current_filter_dir,
                                              double viewport_width,
                                              double viewport_height,
                                              double item_padding = 1.5);

    // Incremental append (Justified)
    LayoutResult append(const std::vector<std::pair<size_t,double>>& indexed_aspects);

private:
    LayoutCfg cfg_;
    double last_viewport_width_ = 0.0;
    std::vector<Item> items_scratch_;
};
