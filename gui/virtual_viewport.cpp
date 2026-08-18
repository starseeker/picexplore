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

void VirtualViewport::set_selected_image(size_t raw_idx) {
    if (selected_idx_ != raw_idx) {
        selected_idx_ = raw_idx;
        redraw();
    }
}

std::vector<size_t> VirtualViewport::get_visible_indices(int margin_y) const {
    std::vector<size_t> visible;
    if (!layout_) return visible;

    int view_top = scroll_offset_ - margin_y;
    int view_bottom = scroll_offset_ + h() + margin_y;

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
    if (view_mode_ == ViewMode::SINGLE_IMAGE) {
        draw_single_image();
    } else {
        draw_grid();
    }
}

void VirtualViewport::draw_grid() {
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

        int draw_x = static_cast<int>(x() + box.x);
        int draw_y = static_cast<int>(y() + box.y - scroll_offset_);
        int draw_w = static_cast<int>(box.w);
        int draw_h = static_cast<int>(box.h);

        if (box.image_index == selected_idx_) {
            fl_color(fl_rgb_color(100, 180, 255));
            fl_rectf(draw_x - 2, draw_y - 2, draw_w + 4, draw_h + 4);
        }

        auto& entry = store_.get(box.image_index);
        const uint8_t* img_data = nullptr;
        
        if (entry.best_quality != ThumbQuality::NONE && !entry.scaled.rgb_data.empty()) {
            img_data = store_.get_scaled_image(box.image_index, draw_w, draw_h);
        }

        if (!img_data) {
            fl_color(FL_DARK3);
            fl_rectf(draw_x, draw_y, draw_w, draw_h);
            fl_color(FL_WHITE);
            fl_rect(draw_x, draw_y, draw_w, draw_h);

            // Draw placeholder text
            std::filesystem::path p(entry.filepath);
            std::string filename = p.filename().string();
            std::string text;
            
            if (entry.best_quality == ThumbQuality::FAILED) {
                text = "X\n[FAILED]\n" + filename;
                fl_color(fl_rgb_color(200, 50, 50)); // Red border for failed
                fl_rect(draw_x, draw_y, draw_w, draw_h);
            } else {
                std::string ext = p.extension().string();
                if (!ext.empty()) ext = ext.substr(1); // Remove leading dot
                for (auto& c : ext) c = toupper(c); // Convert to uppercase

                text = filename + "\n";
                if (entry.original_width > 0 && entry.original_height > 0) {
                    text += std::to_string(entry.original_width) + "x" + std::to_string(entry.original_height) + "\n";
                }
                if (!ext.empty()) {
                    text += ext;
                }
            }

            fl_font(FL_HELVETICA, 12);
            fl_color(FL_LIGHT2); // Slightly dimmed white
            
            // Draw text centered and wrapped inside the box, with 10px padding
            fl_draw(text.c_str(), draw_x + 10, draw_y + 10, draw_w - 20, draw_h - 20, FL_ALIGN_CENTER | FL_ALIGN_WRAP, nullptr, 0);
        } else {
            fl_draw_image(img_data, draw_x, draw_y, draw_w, draw_h, 3, 0);
        }
    }

    fl_pop_clip();
}

int VirtualViewport::handle(int event) {
    if (view_mode_ == ViewMode::SINGLE_IMAGE) {
        switch (event) {
            case FL_MOUSEWHEEL: {
                int dy = Fl::event_dy();
                float zoom_factor = (dy < 0) ? 1.1f : 0.9f;
                
                // Get cursor position relative to the image
                int mx = Fl::event_x() - x();
                int my = Fl::event_y() - y();

                if (zoom_ == 0.0f) {
                    // Switch from fit-to-window to explicit zoom
                    int img_w = (tile_manager_ && full_res_ready_) ? tile_orig_w_ : full_res_w_;
                    int img_h = (tile_manager_ && full_res_ready_) ? tile_orig_h_ : full_res_h_;
                    
                    if (img_w == 0 || img_h == 0) return 1;
                    float scale_x = (float)w() / img_w;
                    float scale_y = (float)h() / img_h;
                    zoom_ = std::min(scale_x, scale_y);
                    pan_x_ = (w() - img_w * zoom_) / 2.0f;
                    pan_y_ = (h() - img_h * zoom_) / 2.0f;
                }

                // Zoom towards mouse cursor
                float old_zoom = zoom_;
                zoom_ *= zoom_factor;
                
                // Clamp zoom
                if (zoom_ < 0.1f) zoom_ = 0.1f;
                if (zoom_ > 16.0f) zoom_ = 16.0f;

                // Adjust pan to keep cursor at same image pixel
                float fx = (mx - pan_x_) / old_zoom;
                float fy = (my - pan_y_) / old_zoom;
                pan_x_ = mx - fx * zoom_;
                pan_y_ = my - fy * zoom_;

                redraw();
                return 1;
            }
            case FL_PUSH:
                if (Fl::event_button() == FL_LEFT_MOUSE) {
                    last_drag_x_ = Fl::event_x();
                    last_drag_y_ = Fl::event_y();
                    return 1;
                }
                return 1;
            case FL_DRAG:
                if (zoom_ != 0.0f) {
                    pan_x_ += (Fl::event_x() - last_drag_x_);
                    pan_y_ += (Fl::event_y() - last_drag_y_);
                    last_drag_x_ = Fl::event_x();
                    last_drag_y_ = Fl::event_y();
                    redraw();
                    return 1;
                }
                return 1;
            case FL_KEYDOWN:
                if (Fl::event_key() == FL_Escape) {
                    if (on_exit_single_image) on_exit_single_image();
                    return 1;
                }
                return Fl_Widget::handle(event);
            default:
                return Fl_Widget::handle(event);
        }
    } else {
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
}

void VirtualViewport::enter_single_image(size_t raw_idx) {
    view_mode_ = ViewMode::SINGLE_IMAGE;
    single_idx_ = raw_idx;
    zoom_ = 0.0f;
    full_res_ready_ = false;
    full_res_rgb_.clear();
    full_res_w_ = 0;
    full_res_h_ = 0;
    redraw();
}

void VirtualViewport::exit_single_image() {
    view_mode_ = ViewMode::GRID;
    full_res_rgb_.clear();
    redraw();
}

void VirtualViewport::set_full_res_image(const std::vector<uint8_t>& rgb, int w, int h) {
    if (view_mode_ != ViewMode::SINGLE_IMAGE) return;
    full_res_rgb_ = rgb;
    full_res_w_ = w;
    full_res_h_ = h;
    full_res_ready_ = true;
    redraw();
}

void VirtualViewport::set_tile_manager(TileManager* tm, const std::string& hash, int orig_w, int orig_h) {
    tile_manager_ = tm;
    tile_hash_ = hash;
    tile_orig_w_ = orig_w;
    tile_orig_h_ = orig_h;
}

int VirtualViewport::scroll_to_image(size_t raw_idx) {
    if (!layout_) return scroll_offset_;
    
    for (const auto& box : layout_->boxes) {
        if (box.image_index == raw_idx) {
            // Target offset to center the image vertically
            int target_y = box.y - (h() / 2) + (box.h / 2);
            if (target_y < 0) target_y = 0;
            return target_y;
        }
    }
    return scroll_offset_;
}

void VirtualViewport::draw_single_image() {
    fl_color(FL_BLACK);
    fl_rectf(x(), y(), w(), h());

    int img_w = 0, img_h = 0;
    const uint8_t* img_data = nullptr;

    if (full_res_ready_ && !full_res_rgb_.empty()) {
        img_w = full_res_w_;
        img_h = full_res_h_;
        img_data = full_res_rgb_.data();
    } else {
        // Fallback to highest quality thumbnail available
        try {
            const auto& entry = store_.get(single_idx_);
            if (!entry.scaled.rgb_data.empty()) {
                img_data = entry.scaled.rgb_data.data();
                img_w = entry.scaled.width;
                img_h = entry.scaled.height;
            }
        } catch (...) {}
    }

    if (tile_manager_ && full_res_ready_ && zoom_ > 0.0f) {
        fl_push_clip(x(), y(), w(), h());
        
        float scale = zoom_;
        int ts = TileManager::TILE_SIZE;
        float scaled_ts = ts * scale;
        
        int start_tx = std::max(0, (int)(-pan_x_ / scaled_ts));
        int start_ty = std::max(0, (int)(-pan_y_ / scaled_ts));
        int end_tx = std::min((tile_orig_w_ + ts - 1) / ts - 1, (int)((w() - pan_x_) / scaled_ts));
        int end_ty = std::min((tile_orig_h_ + ts - 1) / ts - 1, (int)((h() - pan_y_) / scaled_ts));

        for (int ty = start_ty; ty <= end_ty; ty++) {
            for (int tx = start_tx; tx <= end_tx; tx++) {
                int tw, th;
                std::vector<uint8_t> rgb = tile_manager_->get_tile(tile_hash_, tx, ty, 0, tw, th);
                if (!rgb.empty()) {
                    int draw_x = x() + pan_x_ + tx * scaled_ts;
                    int draw_y = y() + pan_y_ + ty * scaled_ts;
                    int draw_w = tw * scale;
                    int draw_h = th * scale;
                    
                    if (scale != 1.0f) {
                        std::vector<uint8_t> scaled_data(draw_w * draw_h * 3);
                        stbir_resize_uint8_linear(
                            rgb.data(), tw, th, 0,
                            scaled_data.data(), draw_w, draw_h, 0,
                            (stbir_pixel_layout)3
                        );
                        fl_draw_image(scaled_data.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
                    } else {
                        fl_draw_image(rgb.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
                    }
                }
            }
        }
        fl_pop_clip();
    } else if (img_data && img_w > 0 && img_h > 0) {
        fl_push_clip(x(), y(), w(), h());

        if (zoom_ == 0.0f) {
            // Fit to window
            float scale_x = (float)w() / img_w;
            float scale_y = (float)h() / img_h;
            float scale = std::min(scale_x, scale_y);
            
            int draw_w = (int)(img_w * scale);
            int draw_h = (int)(img_h * scale);
            int draw_x = x() + (w() - draw_w) / 2;
            int draw_y = y() + (h() - draw_h) / 2;

            if (scale < 1.0f || scale > 1.0f) { // Need resize
                std::vector<uint8_t> scaled_data(draw_w * draw_h * 3);
                stbir_resize_uint8_linear(
                    img_data, img_w, img_h, 0,
                    scaled_data.data(), draw_w, draw_h, 0,
                    (stbir_pixel_layout)3
                );
                fl_draw_image(scaled_data.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
            } else {
                fl_draw_image(img_data, draw_x, draw_y, draw_w, draw_h, 3, 0);
            }
        } else {
            // Custom zoom & pan
            int draw_w = (int)(img_w * zoom_);
            int draw_h = (int)(img_h * zoom_);
            int draw_x = x() + (int)pan_x_;
            int draw_y = y() + (int)pan_y_;

            std::vector<uint8_t> scaled_data(draw_w * draw_h * 3);
            stbir_resize_uint8_linear(
                img_data, img_w, img_h, 0,
                scaled_data.data(), draw_w, draw_h, 0,
                (stbir_pixel_layout)3
            );
            fl_draw_image(scaled_data.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
        }

        fl_pop_clip();
    }

    // Draw loading overlay or zoom percentage
    fl_font(FL_HELVETICA_BOLD, 14);
    fl_color(FL_WHITE);
    if (!full_res_ready_) {
        fl_draw("Loading full resolution...", x() + 10, y() + h() - 10);
    } else {
        std::string text = "Zoom: ";
        if (zoom_ == 0.0f) text += "Fit";
        else text += std::to_string((int)(zoom_ * 100)) + "%";
        fl_draw(text.c_str(), x() + 10, y() + h() - 10);
    }
}
