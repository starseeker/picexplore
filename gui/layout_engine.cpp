#include "layout_engine.h"

LayoutEngine::LayoutEngine() {
}

LayoutEngine::~LayoutEngine() {
}

LayoutEngine::LayoutResult LayoutEngine::compute(const std::vector<double>& aspect_ratios,
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

    std::vector<Item> items;
    items.reserve(aspect_ratios.size());
    for (double ar : aspect_ratios) {
        Item item;
        item.ar = ar;
        items.push_back(item);
    }

    LayoutResult result;
    if (items.empty()) {
        result.total_height = 0;
        return result;
    }

    JustifiedLayout layout(items, cfg_);
    
    result.boxes.reserve(layout.boxes().size());
    const auto& layout_boxes = layout.boxes();
    for (size_t i = 0; i < layout_boxes.size(); ++i) {
        const auto& box = layout_boxes[i];
        result.boxes.push_back({i, box.l, box.t, box.w, box.h});
    }
    result.total_height = layout.height() + cfg_.pb;

    return result;
}

LayoutEngine::LayoutResult LayoutEngine::append(const std::vector<double>& all_aspect_ratios) {
    // JustifiedLayout is fast enough for <100k images. Just recompute for now.
    return compute(all_aspect_ratios, last_viewport_width_, cfg_.rh);
}
