#include "virtual_viewport.h"
#include <iostream>
#include <filesystem>
#include <FL/fl_draw.H>
#include <FL/Fl.H>
#include "../third_party/stb/stb_image_resize2.h"

VirtualViewport::VirtualViewport(int x, int y, int w, int h, ImageStore& store)
    : Fl_Widget(x, y, w, h), store_(store) {
}

VirtualViewport::~VirtualViewport() {}

void VirtualViewport::set_layout(const LayoutEngine::LayoutResult* layout) {
    layout_ = layout;
    redraw();
}

void VirtualViewport::set_scroll_offset(int y) {
    if (scroll_offset_ != y) {
        scroll_offset_ = y;
        redraw();
    }
}

void VirtualViewport::apply_updates(const std::vector<size_t>& changed_indices) {
    redraw();
}

std::vector<size_t> VirtualViewport::get_visible_indices() const {
    std::vector<size_t> visible;
    if (!layout_) return visible;

    int view_top = scroll_offset_;
    int view_bottom = scroll_offset_ + h();

    for (const auto& box : layout_->boxes) {
        if (box.y + box.h >= view_top && box.y <= view_bottom) {
            visible.push_back(box.image_index);
        } else if (box.y > view_bottom) {
            break; 
        }
    }
    return visible;
}

void VirtualViewport::draw() {
    fl_color(FL_DARK2); 
    fl_rectf(x(), y(), w(), h());

    if (!layout_) return;

    int view_top = scroll_offset_;
    int view_bottom = scroll_offset_ + h();

    std::cout << "VirtualViewport::draw() called! layout_->boxes.size() = " << layout_->boxes.size() << std::endl;

    fl_push_clip(x(), y(), w(), h());

    for (const auto& box : layout_->boxes) {
        if (box.y + box.h < view_top) continue;
        if (box.y > view_bottom) break;

        int draw_x = x() + static_cast<int>(box.x);
        int draw_y = y() + static_cast<int>(box.y) - scroll_offset_;
        int draw_w = static_cast<int>(box.w);
        int draw_h = static_cast<int>(box.h);

        std::cout << "Box " << box.image_index << " (y=" << box.y << ", h=" << box.h << ", vt=" << view_top << ", vb=" << view_bottom << ")" << std::endl;

        auto& entry = store_.get(box.image_index);

        if (entry.best_quality == ThumbQuality::NONE || entry.decoded.rgb_data.empty()) {
            fl_color(FL_DARK3);
            fl_rectf(draw_x, draw_y, draw_w, draw_h);
            fl_color(FL_WHITE);
            fl_rect(draw_x, draw_y, draw_w, draw_h);

            // Draw placeholder text
            std::filesystem::path p(entry.filepath);
            std::string filename = p.filename().string();
            std::string ext = p.extension().string();
            if (!ext.empty()) ext = ext.substr(1); // Remove leading dot
            for (auto& c : ext) c = toupper(c); // Convert to uppercase

            std::string text = filename + "\n";
            if (entry.original_width > 0 && entry.original_height > 0) {
                text += std::to_string(entry.original_width) + "x" + std::to_string(entry.original_height) + "\n";
            }
            if (!ext.empty()) {
                text += ext;
            }

            fl_font(FL_HELVETICA, 12);
            fl_color(FL_LIGHT2); // Slightly dimmed white
            
            // Draw text centered and wrapped inside the box, with 10px padding
            fl_draw(text.c_str(), draw_x + 10, draw_y + 10, draw_w - 20, draw_h - 20, FL_ALIGN_CENTER | FL_ALIGN_WRAP, nullptr, 0);
        } else {
            if (entry.scaled.layout_width != draw_w || entry.scaled.layout_height != draw_h) {
                entry.scaled.layout_width = draw_w;
                entry.scaled.layout_height = draw_h;
                entry.scaled.width = draw_w;
                entry.scaled.height = draw_h;
                entry.scaled.rgb_data.resize(draw_w * draw_h * 3);
                stbir_resize_uint8_linear(entry.decoded.rgb_data.data(), entry.decoded.width, entry.decoded.height, 0,
                                          entry.scaled.rgb_data.data(), draw_w, draw_h, 0, STBIR_RGB);
            }
            std::cout << "Drawing image for box " << box.image_index << std::endl;
            fl_draw_image(entry.scaled.rgb_data.data(), draw_x, draw_y, draw_w, draw_h, 3, draw_w * 3);
        }
    }

    fl_pop_clip();
}

int VirtualViewport::handle(int event) {
    switch (event) {
        case FL_PUSH:
            if (layout_ && on_image_clicked) {
                int mx = Fl::event_x() - x();
                int my = Fl::event_y() - y() + scroll_offset_;
                
                for (const auto& box : layout_->boxes) {
                    if (mx >= box.x && mx <= box.x + box.w &&
                        my >= box.y && my <= box.y + box.h) {
                        try {
                            const auto& entry = store_.get(box.image_index);
                            on_image_clicked(entry.filepath);
                        } catch (...) {}
                        return 1;
                    }
                }
            }
            return 1; // Consume clicks so they don't propagate incorrectly
        case FL_MOUSEWHEEL:
            return 0; 
        default:
            return Fl_Widget::handle(event);
    }
}
