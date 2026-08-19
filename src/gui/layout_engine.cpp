#include "layout_engine.h"

LayoutEngine::LayoutEngine() {
}

LayoutEngine::~LayoutEngine() {
}

LayoutEngine::LayoutResult LayoutEngine::compute(const std::vector<std::pair<size_t,double>>& indexed_aspects,
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

LayoutEngine::LayoutResult LayoutEngine::append(const std::vector<std::pair<size_t,double>>& indexed_aspects) {
    // JustifiedLayout is fast enough for <100k images. Just recompute for now.
    return compute(indexed_aspects, last_viewport_width_, cfg_.rh);
}
