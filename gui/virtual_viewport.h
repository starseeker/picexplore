#pragma once

#include <FL/Fl_Widget.H>
#include <FL/Fl_Scrollbar.H>
#include <functional>
#include "image_store.h"
#include "layout_engine.h"

class VirtualViewport : public Fl_Widget {
public:
    VirtualViewport(int x, int y, int w, int h, ImageStore& store);
    ~VirtualViewport();

    void set_layout(const LayoutEngine::LayoutResult* layout);
    void set_scroll_offset(int y);
    int scroll_offset() const { return scroll_offset_; }

    void apply_updates(const std::vector<size_t>& changed_indices);

    std::vector<size_t> get_visible_indices() const;

    int content_width() const { return w(); }

    std::function<void(const std::string&)> on_image_clicked;

protected:
    void draw() override;
    int handle(int event) override;

private:
    ImageStore& store_;
    const LayoutEngine::LayoutResult* layout_ = nullptr;
    int scroll_offset_ = 0;
};
