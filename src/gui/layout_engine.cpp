#include "layout_engine.h"

LayoutEngine::LayoutEngine() {
}

LayoutEngine::~LayoutEngine() {
}

LayoutEngine::LayoutResult LayoutEngine::compute(const std::vector<std::pair<size_t,double>>& indexed_aspects,
                                                 double viewport_width,
                                                 double target_row_height) {
    return compute_justified(indexed_aspects, viewport_width, target_row_height);
}

LayoutEngine::LayoutResult LayoutEngine::compute_justified(const std::vector<std::pair<size_t,double>>& indexed_aspects,
                                                           double viewport_width,
                                                           double target_row_height) {
    last_viewport_width_ = viewport_width;
    
    cfg_.w = viewport_width;
    cfg_.rh = target_row_height;
    cfg_.sh = 5;
    cfg_.sv = 5;
    cfg_.pl = 5;
    cfg_.pr = 5;
    cfg_.pt = 5;
    cfg_.pb = 5;

    items_scratch_.clear();
    items_scratch_.reserve(indexed_aspects.size());
    for (const auto& [raw_idx, ar] : indexed_aspects) {
        Item item;
        item.ar = ar;
        items_scratch_.push_back(item);
    }

    LayoutResult result;
    result.layout_type = LayoutType::JUSTIFIED;
    if (items_scratch_.empty()) {
        result.total_height = 0;
        return result;
    }

    JustifiedLayout layout(items_scratch_, cfg_);
    
    result.boxes.reserve(layout.boxes().size());
    const auto& layout_boxes = layout.boxes();
    for (size_t i = 0; i < layout_boxes.size(); ++i) {
        const auto& box = layout_boxes[i];
        // Use raw_store_index from the input pair so downstream code always
        // sees real store indices, never filtered-space indices.
        result.boxes.push_back({indexed_aspects[i].first, box.l, box.t, box.w, box.h});
    }
    result.total_height = layout.height() + cfg_.pb;

    return result;
}

LayoutEngine::LayoutResult LayoutEngine::compute_treemap(const std::vector<TreemapItem>& items,
                                                         double viewport_width,
                                                         double viewport_height,
                                                         double padding) {
    LayoutResult result;
    result.layout_type = LayoutType::TREEMAP;
    result.total_height = viewport_height;

    if (items.empty() || viewport_width <= 0.0 || viewport_height <= 0.0) {
        return result;
    }

    auto tboxes = SquarifiedTreemap::compute(items, 0.0, 0.0, viewport_width, viewport_height, padding);
    result.boxes.reserve(tboxes.size());
    for (const auto& tb : tboxes) {
        result.boxes.push_back({tb.id, tb.x, tb.y, tb.w, tb.h, tb.cushion_ax, tb.cushion_bx, tb.cushion_ay, tb.cushion_by});
    }

    return result;
}

LayoutEngine::LayoutResult LayoutEngine::compute_hierarchical_treemap(const std::vector<HierarchicalTreemapItem>& items,
                                                                     const std::string& base_root_dir,
                                                                     const std::string& current_filter_dir,
                                                                     double viewport_width,
                                                                     double viewport_height,
                                                                     double item_padding) {
    LayoutResult result;
    result.layout_type = LayoutType::HIERARCHICAL_TREEMAP;
    result.total_height = viewport_height;

    if (items.empty() || viewport_width <= 0.0 || viewport_height <= 0.0) {
        return result;
    }

    auto hres = HierarchicalTreemap::compute(items, base_root_dir, current_filter_dir, 0.0, 0.0, viewport_width, viewport_height, item_padding);
    result.boxes.reserve(hres.boxes.size());
    for (const auto& tb : hres.boxes) {
        result.boxes.push_back({tb.id, tb.x, tb.y, tb.w, tb.h, tb.cushion_ax, tb.cushion_bx, tb.cushion_ay, tb.cushion_by});
    }
    result.container_boxes = std::move(hres.container_boxes);

    return result;
}

LayoutEngine::LayoutResult LayoutEngine::append(const std::vector<std::pair<size_t,double>>& indexed_aspects) {
    // JustifiedLayout is fast enough for <100k images. Just recompute for now.
    return compute_justified(indexed_aspects, last_viewport_width_, cfg_.rh);
}
