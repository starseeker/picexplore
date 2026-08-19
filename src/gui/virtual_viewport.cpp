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

    fl_push_clip(x(), y(), w(), h());

    for (const auto& box : layout_->boxes) {
        if (box.y + box.h < view_top) continue;
        if (box.y > view_bottom) break;

        int draw_x = static_cast<int>(x() + box.x);
        int draw_y = static_cast<int>(y() + box.y - scroll_offset_);
        int draw_w = static_cast<int>(box.w);
        int draw_h = static_cast<int>(box.h);

        bool is_selected = (box.image_index == selected_idx_);
        if (is_selected) {
            fl_color(fl_rgb_color(60, 160, 255));
            fl_rectf(draw_x - 3, draw_y - 3, draw_w + 6, draw_h + 6);
        }

        auto& entry = store_.get(box.image_index);
        const uint8_t* img_data = nullptr;
        
        if (entry.best_quality != ThumbQuality::NONE && !entry.scaled.rgb_data.empty()) {
            img_data = store_.get_scaled_image(box.image_index, draw_w, draw_h);
        }

        if (!img_data) {
            if (is_selected) {
                fl_color(fl_rgb_color(35, 75, 125));
                fl_rectf(draw_x, draw_y, draw_w, draw_h);
                fl_color(fl_rgb_color(100, 200, 255));
                fl_rect(draw_x, draw_y, draw_w, draw_h);
            } else {
                fl_color(FL_DARK3);
                fl_rectf(draw_x, draw_y, draw_w, draw_h);
                fl_color(FL_WHITE);
                fl_rect(draw_x, draw_y, draw_w, draw_h);
            }

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
            if (is_selected) {
                // Tint whole image with a light blue overlay (blend 25% light blue: 120, 185, 255)
                std::vector<uint8_t> tinted(draw_w * draw_h * 3);
                for (size_t i = 0; i < (size_t)draw_w * draw_h; ++i) {
                    uint8_t r = img_data[i * 3 + 0];
                    uint8_t g = img_data[i * 3 + 1];
                    uint8_t b = img_data[i * 3 + 2];
                    tinted[i * 3 + 0] = static_cast<uint8_t>((r * 3 + 120) / 4);
                    tinted[i * 3 + 1] = static_cast<uint8_t>((g * 3 + 185) / 4);
                    tinted[i * 3 + 2] = static_cast<uint8_t>((b * 3 + 255) / 4);
                }
                fl_draw_image(tinted.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
            } else {
                fl_draw_image(img_data, draw_x, draw_y, draw_w, draw_h, 3, 0);
            }
        }
    }

    fl_pop_clip();
}

int VirtualViewport::handle(int event) {
    if (view_mode_ == ViewMode::SINGLE_IMAGE) {
        switch (event) {
            case FL_FOCUS:
            case FL_UNFOCUS:
                return 1;
            case FL_MOUSEWHEEL: {
                int dy = Fl::event_dy();
                float zoom_factor = (dy < 0) ? 1.1f : 0.9f;
                
                // Get cursor position relative to the image
                int mx = Fl::event_x() - x();
                int my = Fl::event_y() - y();

                if (zoom_ == 0.0f) {
                    // Switch from fit-to-window to explicit zoom
                    int img_w = 0, img_h = 0;
                    if (tile_manager_ && tile_orig_w_ > 0 && tile_orig_h_ > 0) {
                        img_w = tile_orig_w_;
                        img_h = tile_orig_h_;
                    } else if (full_res_ready_ && full_res_w_ > 0 && full_res_h_ > 0) {
                        img_w = full_res_w_;
                        img_h = full_res_h_;
                    } else {
                        try {
                            const auto& entry = store_.get(single_idx_);
                            img_w = entry.original_width > 0 ? entry.original_width : entry.scaled.width;
                            img_h = entry.original_height > 0 ? entry.original_height : entry.scaled.height;
                        } catch (...) {}
                    }
                    
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
                float min_zoom = 0.1f;
                int img_w = 0, img_h = 0;
                if (tile_manager_ && tile_orig_w_ > 0 && tile_orig_h_ > 0) {
                    img_w = tile_orig_w_;
                    img_h = tile_orig_h_;
                } else if (full_res_ready_ && full_res_w_ > 0 && full_res_h_ > 0) {
                    img_w = full_res_w_;
                    img_h = full_res_h_;
                } else {
                    try {
                        const auto& entry = store_.get(single_idx_);
                        img_w = entry.original_width > 0 ? entry.original_width : entry.scaled.width;
                        img_h = entry.original_height > 0 ? entry.original_height : entry.scaled.height;
                    } catch (...) {}
                }
                if (img_w > 0 && img_h > 0) {
                    float fit_zoom = std::min((float)w() / img_w, (float)h() / img_h);
                    if (fit_zoom < min_zoom) min_zoom = fit_zoom;
                }
                if (zoom_ < min_zoom) zoom_ = min_zoom;
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
            case FL_FOCUS:
            case FL_UNFOCUS:
                return 1;
            case FL_PUSH:
                take_focus();
                if (layout_ && on_image_clicked) {
                    int mx = Fl::event_x() - x();
                    int my = Fl::event_y() - y() + scroll_offset_;
                    bool hit = false;
                    for (const auto& box : layout_->boxes) {
                        if (mx >= box.x && mx <= box.x + box.w &&
                            my >= box.y && my <= box.y + box.h) {
                            hit = true;
                            try {
                                const auto& entry = store_.get(box.image_index);
                                if (Fl::event_clicks() > 0 && on_image_double_clicked) {
                                    on_image_double_clicked(entry.filepath);
                                } else {
                                    on_image_clicked(entry.filepath);
                                }
                            } catch (...) {}
                            return 1;
                        }
                    }
                    if (!hit) {
                        on_image_clicked("");
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

    int orig_w = 0, orig_h = 0;
    try {
        const auto& entry = store_.get(single_idx_);
        orig_w = entry.original_width;
        orig_h = entry.original_height;
        if (!full_res_ready_ || full_res_rgb_.empty()) {
            if (!entry.scaled.rgb_data.empty()) {
                img_data = entry.scaled.rgb_data.data();
                img_w = entry.scaled.width;
                img_h = entry.scaled.height;
            }
        }
    } catch (...) {}

    if (full_res_ready_ && !full_res_rgb_.empty()) {
        img_w = full_res_w_;
        img_h = full_res_h_;
        img_data = full_res_rgb_.data();
    }

    bool draw_tiles = (tile_manager_ && full_res_ready_ && zoom_ >= 0.14f);

    if (img_data && img_w > 0 && img_h > 0 && (!draw_tiles || !tile_manager_)) {
        fl_push_clip(x(), y(), w(), h());

        float target_orig_w = (orig_w > 0) ? (float)orig_w : (float)img_w;
        float target_orig_h = (orig_h > 0) ? (float)orig_h : (float)img_h;
        if (tile_manager_ && tile_orig_w_ > 0 && tile_orig_h_ > 0) {
            target_orig_w = (float)tile_orig_w_;
            target_orig_h = (float)tile_orig_h_;
        }

        if (zoom_ == 0.0f) {
            // Fit to window
            float scale_x = (float)w() / target_orig_w;
            float scale_y = (float)h() / target_orig_h;
            float scale = std::min(scale_x, scale_y);
            
            int draw_w = std::max(1, (int)(target_orig_w * scale));
            int draw_h = std::max(1, (int)(target_orig_h * scale));
            int draw_x = x() + (w() - draw_w) / 2;
            int draw_y = y() + (h() - draw_h) / 2;

            if (draw_w > 0 && draw_h > 0 && (draw_w != img_w || draw_h != img_h)) { // Need resize
                std::vector<uint8_t> scaled_data(draw_w * draw_h * 3);
                stbir_resize_uint8_linear(
                    img_data, img_w, img_h, 0,
                    scaled_data.data(), draw_w, draw_h, 0,
                    (stbir_pixel_layout)3
                );
                fl_draw_image(scaled_data.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
            } else if (draw_w > 0 && draw_h > 0) {
                fl_draw_image(img_data, draw_x, draw_y, draw_w, draw_h, 3, 0);
            }
        } else {
            // Custom zoom & pan: only resample the visible viewport sub-region!
            
            int draw_w = (int)(target_orig_w * zoom_);
            int draw_h = (int)(target_orig_h * zoom_);
            int screen_x = x() + (int)pan_x_;
            int screen_y = y() + (int)pan_y_;

            // Intersection between viewport [x(), y(), w(), h()] and drawn image rect [screen_x, screen_y, draw_w, draw_h]
            int clip_x0 = std::max(x(), screen_x);
            int clip_y0 = std::max(y(), screen_y);
            int clip_x1 = std::min(x() + w(), screen_x + draw_w);
            int clip_y1 = std::min(y() + h(), screen_y + draw_h);

            if (clip_x1 > clip_x0 && clip_y1 > clip_y0 && draw_w > 0 && draw_h > 0) {
                int out_w = clip_x1 - clip_x0;
                int out_h = clip_y1 - clip_y0;

                int subx = clip_x0 - screen_x;
                int suby = clip_y0 - screen_y;

                double s0 = (double)subx / draw_w;
                double t0 = (double)suby / draw_h;
                double s1 = (double)(subx + out_w) / draw_w;
                double t1 = (double)(suby + out_h) / draw_h;

                std::vector<uint8_t> scaled_data(out_w * out_h * 3);
                STBIR_RESIZE resize;
                stbir_resize_init(&resize,
                                  img_data, img_w, img_h, 0,
                                  scaled_data.data(), out_w, out_h, 0,
                                  STBIR_RGB, STBIR_TYPE_UINT8);
                stbir_set_input_subrect(&resize, s0, t0, s1, t1);
                stbir_resize_extended(&resize);

                fl_draw_image(scaled_data.data(), clip_x0, clip_y0, out_w, out_h, 3, 0);
            }
        }

        fl_pop_clip();
    }

    if (draw_tiles) {
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
                    
                    if (draw_w > 0 && draw_h > 0 && scale != 1.0f) {
                        std::vector<uint8_t> scaled_data(draw_w * draw_h * 3);
                        stbir_resize_uint8_linear(
                            rgb.data(), tw, th, 0,
                            scaled_data.data(), draw_w, draw_h, 0,
                            (stbir_pixel_layout)3
                        );
                        fl_draw_image(scaled_data.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
                    } else if (draw_w > 0 && draw_h > 0) {
                        fl_draw_image(rgb.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
                    }
                }
            }
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
