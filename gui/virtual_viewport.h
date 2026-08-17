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

    enum class ViewMode { GRID, SINGLE_IMAGE };
    void enter_single_image(size_t raw_idx);
    void exit_single_image();
    void set_full_res_image(const std::vector<uint8_t>& rgb, int w, int h);
    int scroll_to_image(size_t raw_idx);

    void set_selected_image(size_t raw_idx);

    void apply_updates(const std::vector<size_t>& changed_indices);

    std::vector<size_t> get_visible_indices() const;

    int content_width() const { return w(); }

    std::function<void(const std::string&)> on_image_clicked;
    std::function<void()> on_exit_single_image;

protected:
    void draw() override;
    int handle(int event) override;

private:
    ImageStore& store_;
    const LayoutEngine::LayoutResult* layout_ = nullptr;
    int scroll_offset_ = 0;

    size_t selected_idx_ = (size_t)-1;

    ViewMode view_mode_ = ViewMode::GRID;
    size_t   single_idx_ = 0;
    float    zoom_ = 0.0f;   // 0 = fit-to-window
    float    pan_x_ = 0.0f, pan_y_ = 0.0f;
    std::vector<uint8_t> full_res_rgb_;
    int      full_res_w_ = 0, full_res_h_ = 0;
    bool     full_res_ready_ = false;

    // Used for panning
    int last_drag_x_ = 0;
    int last_drag_y_ = 0;

    void draw_single_image();
    void draw_grid();
};
