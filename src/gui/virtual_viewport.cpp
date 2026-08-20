#include "virtual_viewport.h"
#include <iostream>
#include <filesystem>
#include <cstring>
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

    if (layout_->layout_type == LayoutEngine::LayoutType::TREEMAP) {
        for (const auto& box : layout_->boxes) {
            if (box.w > 0 && box.h > 0) {
                visible.push_back(box.image_index);
            }
        }
        return visible;
    }

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
    } else if (layout_ && layout_->layout_type == LayoutEngine::LayoutType::TREEMAP) {
        draw_treemap();
    } else {
        draw_grid();
    }
}

static void fast_scale_image(const uint8_t* src, int sw, int sh,
                             uint8_t* dst, int dw, int dh,
                             std::vector<int>& x_coords_buf) {
    if (!src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;

    if (sw == dw && sh == dh) {
        std::memcpy(dst, src, (size_t)dw * dh * 3);
        return;
    }

    x_coords_buf.resize(dw);
    int* x_coords = x_coords_buf.data();
    for (int x = 0; x < dw; ++x) {
        x_coords[x] = ((x * sw) / dw) * 3;
    }

    int src_stride = sw * 3;
    int dst_stride = dw * 3;

    for (int y = 0; y < dh; ++y) {
        int sy = (y * sh) / dh;
        if (sy >= sh) sy = sh - 1;
        const uint8_t* src_row = src + sy * src_stride;
        uint8_t* dst_row = dst + y * dst_stride;

        for (int x = 0; x < dw; ++x) {
            int sx_off = x_coords[x];
            dst_row[x * 3 + 0] = src_row[sx_off + 0];
            dst_row[x * 3 + 1] = src_row[sx_off + 1];
            dst_row[x * 3 + 2] = src_row[sx_off + 2];
        }
    }
}

void VirtualViewport::draw_grid() {
    fl_color(fl_rgb_color(38, 38, 38)); 
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
            if (entry.scaled.width == draw_w && entry.scaled.height == draw_h) {
                img_data = entry.scaled.rgb_data.data();
            } else if (entry.scaled.width > 0 && entry.scaled.height > 0 && draw_w > 0 && draw_h > 0) {
                draw_tmp_buf_.resize(draw_w * draw_h * 3);
                fast_scale_image(entry.scaled.rgb_data.data(),
                                 entry.scaled.width, entry.scaled.height,
                                 draw_tmp_buf_.data(), draw_w, draw_h,
                                 x_coords_buf_);
                img_data = draw_tmp_buf_.data();
            }
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
                tint_tmp_buf_.resize(draw_w * draw_h * 3);
                for (size_t i = 0; i < (size_t)draw_w * draw_h; ++i) {
                    uint8_t r = img_data[i * 3 + 0];
                    uint8_t g = img_data[i * 3 + 1];
                    uint8_t b = img_data[i * 3 + 2];
                    tint_tmp_buf_[i * 3 + 0] = static_cast<uint8_t>((r * 3 + 120) / 4);
                    tint_tmp_buf_[i * 3 + 1] = static_cast<uint8_t>((g * 3 + 185) / 4);
                    tint_tmp_buf_[i * 3 + 2] = static_cast<uint8_t>((b * 3 + 255) / 4);
                }
                fl_draw_image(tint_tmp_buf_.data(), draw_x, draw_y, draw_w, draw_h, 3, 0);
            } else {
                fl_draw_image(img_data, draw_x, draw_y, draw_w, draw_h, 3, 0);
            }
        }
    }

    fl_pop_clip();
}

void VirtualViewport::draw_treemap() {
    fl_color(fl_rgb_color(28, 28, 28)); 
    fl_rectf(x(), y(), w(), h());

    if (!layout_ || layout_->boxes.empty()) {
        fl_font(FL_HELVETICA, 14);
        fl_color(fl_rgb_color(150, 150, 150));
        fl_draw("No images to display", x(), y(), w(), h(), FL_ALIGN_CENTER, nullptr, 0);
        return;
    }

    fl_push_clip(x(), y(), w(), h());

    for (const auto& box : layout_->boxes) {
        int draw_x = static_cast<int>(x() + box.x);
        int draw_y = static_cast<int>(y() + box.y);
        int draw_w = static_cast<int>(box.w);
        int draw_h = static_cast<int>(box.h);

        if (draw_w <= 0 || draw_h <= 0) continue;

        bool is_selected = (box.image_index == selected_idx_);
        auto& entry = store_.get(box.image_index);

        Fl_Color bg_col = FileTypeColors::get_fl_color(entry.filepath);

        // Outer highlight if selected
        if (is_selected) {
            fl_color(fl_rgb_color(60, 160, 255));
            fl_rectf(draw_x - 2, draw_y - 2, draw_w + 4, draw_h + 4);
        }

        // Card background
        fl_color(bg_col);
        fl_rectf(draw_x, draw_y, draw_w, draw_h);

        // 1px subtle dark outline
        fl_color(fl_rgb_color(20, 20, 20));
        fl_rect(draw_x, draw_y, draw_w, draw_h);

        // Threshold for drawing thumbnails: at least 36x36 px
        if (draw_w >= 36 && draw_h >= 36) {
            int pad = (draw_w >= 80 && draw_h >= 80) ? 4 : 2;
            int inner_w = draw_w - 2 * pad;
            int inner_h = draw_h - 2 * pad;

            const uint8_t* img_data = nullptr;
            int img_w = 0, img_h = 0;

            if (entry.best_quality != ThumbQuality::NONE && !entry.scaled.rgb_data.empty() &&
                entry.scaled.width > 0 && entry.scaled.height > 0) {
                
                // Aspect fit within inner box
                double ar = entry.aspect_ratio > 0.0 ? entry.aspect_ratio : 1.0;
                img_w = inner_w;
                img_h = inner_h;
                if ((double)inner_w / inner_h > ar) {
                    img_w = std::max(1, static_cast<int>(inner_h * ar));
                } else {
                    img_h = std::max(1, static_cast<int>(inner_w / ar));
                }

                if (img_w > 0 && img_h > 0) {
                    if (entry.scaled.width == img_w && entry.scaled.height == img_h) {
                        img_data = entry.scaled.rgb_data.data();
                    } else {
                        draw_tmp_buf_.resize(img_w * img_h * 3);
                        fast_scale_image(entry.scaled.rgb_data.data(),
                                         entry.scaled.width, entry.scaled.height,
                                         draw_tmp_buf_.data(), img_w, img_h,
                                         x_coords_buf_);
                        img_data = draw_tmp_buf_.data();
                    }
                }
            }

            if (img_data && img_w > 0 && img_h > 0) {
                int img_x = draw_x + pad + (inner_w - img_w) / 2;
                int img_y = draw_y + pad + (inner_h - img_h) / 2;

                if (is_selected) {
                    tint_tmp_buf_.resize(img_w * img_h * 3);
                    for (size_t i = 0; i < (size_t)img_w * img_h; ++i) {
                        uint8_t r = img_data[i * 3 + 0];
                        uint8_t g = img_data[i * 3 + 1];
                        uint8_t b = img_data[i * 3 + 2];
                        tint_tmp_buf_[i * 3 + 0] = static_cast<uint8_t>((r * 3 + 120) / 4);
                        tint_tmp_buf_[i * 3 + 1] = static_cast<uint8_t>((g * 3 + 185) / 4);
                        tint_tmp_buf_[i * 3 + 2] = static_cast<uint8_t>((b * 3 + 255) / 4);
                    }
                    fl_draw_image(tint_tmp_buf_.data(), img_x, img_y, img_w, img_h, 3, 0);
                } else {
                    fl_draw_image(img_data, img_x, img_y, img_w, img_h, 3, 0);
                }

                fl_color(fl_rgb_color(15, 15, 15));
                fl_rect(img_x, img_y, img_w, img_h);
            } else {
                // If thumbnail not available, draw clean filename / info text if space permits
                if (draw_w >= 48 && draw_h >= 24) {
                    std::filesystem::path p(entry.filepath);
                    std::string filename = p.filename().string();
                    
                    int font_sz = 10;
                    if (draw_w >= 120 && draw_h >= 60) font_sz = 12;
                    else if (draw_w < 70 || draw_h < 35) font_sz = 9;

                    fl_font(FL_HELVETICA_BOLD, font_sz);
                    fl_color(FL_WHITE);
                    fl_draw(filename.c_str(), draw_x + 4, draw_y + 4, draw_w - 8, draw_h - 8, FL_ALIGN_CENTER | FL_ALIGN_WRAP, nullptr, 0);
                }
            }
        } else if (draw_w >= 20 && draw_h >= 14) {
            // Small tile: draw extension abbreviation
            std::string cat = FileTypeColors::get_category_name(entry.filepath);
            fl_font(FL_HELVETICA_BOLD, 8);
            fl_color(FL_WHITE);
            fl_draw(cat.c_str(), draw_x, draw_y, draw_w, draw_h, FL_ALIGN_CENTER, nullptr, 0);
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
                    float target_orig_w = (tile_manager_ && tile_orig_w_ > 0) ? (float)tile_orig_w_ : ((full_res_ready_ && full_res_w_ > 0) ? (float)full_res_w_ : 0.0f);
                    if (target_orig_w == 0.0f) {
                        try {
                            const auto& entry = store_.get(single_idx_);
                            target_orig_w = entry.original_width > 0 ? entry.original_width : entry.scaled.width;
                        } catch (...) {}
                    }
                    float target_orig_h = (tile_manager_ && tile_orig_h_ > 0) ? (float)tile_orig_h_ : ((full_res_ready_ && full_res_h_ > 0) ? (float)full_res_h_ : 0.0f);
                    if (target_orig_h == 0.0f) {
                        try {
                            const auto& entry = store_.get(single_idx_);
                            target_orig_h = entry.original_height > 0 ? entry.original_height : entry.scaled.height;
                        } catch (...) {}
                    }
                    MinimapGeometry geom = compute_minimap_geometry(target_orig_w, target_orig_h);
                    int mx = Fl::event_x();
                    int my = Fl::event_y();
                    if (geom.is_valid && mx >= geom.box_x && mx <= geom.box_x + geom.box_w &&
                        my >= geom.box_y && my <= geom.box_y + geom.box_h) {
                        minimap_dragging_ = true;
                        if (zoom_ == 0.0f) {
                            float fit_scale = std::min((float)w() / target_orig_w, (float)h() / target_orig_h);
                            zoom_ = std::max(fit_scale * 2.0f, 1.0f);
                        }
                        float frac_x = std::max(0.0f, std::min(1.0f, (float)(mx - geom.img_x) / geom.img_w));
                        float frac_y = std::max(0.0f, std::min(1.0f, (float)(my - geom.img_y) / geom.img_h));
                        pan_x_ = (w() / 2.0f) - frac_x * target_orig_w * zoom_;
                        pan_y_ = (h() / 2.0f) - frac_y * target_orig_h * zoom_;
                        redraw();
                        return 1;
                    }
                    minimap_dragging_ = false;
                    last_drag_x_ = Fl::event_x();
                    last_drag_y_ = Fl::event_y();
                    return 1;
                }
                return 1;
            case FL_DRAG:
                if (minimap_dragging_) {
                    float target_orig_w = (tile_manager_ && tile_orig_w_ > 0) ? (float)tile_orig_w_ : ((full_res_ready_ && full_res_w_ > 0) ? (float)full_res_w_ : 0.0f);
                    if (target_orig_w == 0.0f) {
                        try {
                            const auto& entry = store_.get(single_idx_);
                            target_orig_w = entry.original_width > 0 ? entry.original_width : entry.scaled.width;
                        } catch (...) {}
                    }
                    float target_orig_h = (tile_manager_ && tile_orig_h_ > 0) ? (float)tile_orig_h_ : ((full_res_ready_ && full_res_h_ > 0) ? (float)full_res_h_ : 0.0f);
                    if (target_orig_h == 0.0f) {
                        try {
                            const auto& entry = store_.get(single_idx_);
                            target_orig_h = entry.original_height > 0 ? entry.original_height : entry.scaled.height;
                        } catch (...) {}
                    }
                    MinimapGeometry geom = compute_minimap_geometry(target_orig_w, target_orig_h);
                    if (geom.is_valid) {
                        int mx = Fl::event_x();
                        int my = Fl::event_y();
                        float frac_x = std::max(0.0f, std::min(1.0f, (float)(mx - geom.img_x) / geom.img_w));
                        float frac_y = std::max(0.0f, std::min(1.0f, (float)(my - geom.img_y) / geom.img_h));
                        pan_x_ = (w() / 2.0f) - frac_x * target_orig_w * zoom_;
                        pan_y_ = (h() / 2.0f) - frac_y * target_orig_h * zoom_;
                        redraw();
                        return 1;
                    }
                } else if (zoom_ != 0.0f) {
                    pan_x_ += (Fl::event_x() - last_drag_x_);
                    pan_y_ += (Fl::event_y() - last_drag_y_);
                    last_drag_x_ = Fl::event_x();
                    last_drag_y_ = Fl::event_y();
                    redraw();
                    return 1;
                }
                return 1;
            case FL_RELEASE:
                if (minimap_dragging_) {
                    minimap_dragging_ = false;
                    return 1;
                }
                return 1;
            case FL_KEYDOWN:
                if (Fl::event_key() == FL_Escape) {
                    if (on_exit_single_image) on_exit_single_image();
                    return 1;
                } else if (Fl::event_key() == FL_Left) {
                    if (on_navigate_single_image) on_navigate_single_image(-1);
                    return 1;
                } else if (Fl::event_key() == FL_Right) {
                    if (on_navigate_single_image) on_navigate_single_image(1);
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
                    int my = Fl::event_y() - y();
                    if (layout_->layout_type == LayoutEngine::LayoutType::JUSTIFIED) {
                        my += scroll_offset_;
                    }
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

void VirtualViewport::mark_full_res_ready() {
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
    if (!full_res_ready_ && !tile_manager_) {
        fl_draw("Loading full resolution...", x() + 10, y() + h() - 10);
    } else {
        std::string text = "Zoom: ";
        if (zoom_ == 0.0f) text += "Fit";
        else text += std::to_string((int)(zoom_ * 100)) + "%";
        fl_draw(text.c_str(), x() + 10, y() + h() - 10);
    }

    // Draw minimap / navigator overlay
    if (show_minimap_) {
        float target_orig_w = (orig_w > 0) ? (float)orig_w : (float)img_w;
        float target_orig_h = (orig_h > 0) ? (float)orig_h : (float)img_h;
        if (tile_manager_ && tile_orig_w_ > 0 && tile_orig_h_ > 0) {
            target_orig_w = (float)tile_orig_w_;
            target_orig_h = (float)tile_orig_h_;
        }
        draw_minimap(target_orig_w, target_orig_h, img_data, img_w, img_h);
    }
}

VirtualViewport::MinimapGeometry VirtualViewport::compute_minimap_geometry(float target_orig_w, float target_orig_h) const {
    MinimapGeometry geom;
    if (!show_minimap_ || target_orig_w <= 0.0f || target_orig_h <= 0.0f || w() <= 240 || h() <= 180) {
        return geom;
    }

    const int max_box_w = 180;
    const int max_box_h = 140;
    const int pad = 4;
    const int margin = 16;

    float aspect = target_orig_w / target_orig_h;
    int mini_w, mini_h;
    if (aspect >= (float)max_box_w / max_box_h) {
        mini_w = max_box_w;
        mini_h = std::max(1, (int)(max_box_w / aspect));
    } else {
        mini_h = max_box_h;
        mini_w = std::max(1, (int)(max_box_h * aspect));
    }

    geom.img_w = mini_w;
    geom.img_h = mini_h;
    geom.box_w = mini_w + pad * 2;
    geom.box_h = mini_h + pad * 2;
    geom.box_x = x() + w() - geom.box_w - margin;
    geom.box_y = y() + h() - geom.box_h - margin;
    geom.img_x = geom.box_x + pad;
    geom.img_y = geom.box_y + pad;

    if (zoom_ == 0.0f) {
        geom.proxy_x = geom.img_x;
        geom.proxy_y = geom.img_y;
        geom.proxy_w = geom.img_w;
        geom.proxy_h = geom.img_h;
    } else {
        float sub_x0 = -pan_x_ / zoom_;
        float sub_y0 = -pan_y_ / zoom_;
        float sub_x1 = sub_x0 + (float)w() / zoom_;
        float sub_y1 = sub_y0 + (float)h() / zoom_;

        float px0 = geom.img_x + (sub_x0 / target_orig_w) * geom.img_w;
        float py0 = geom.img_y + (sub_y0 / target_orig_h) * geom.img_h;
        float px1 = geom.img_x + (sub_x1 / target_orig_w) * geom.img_w;
        float py1 = geom.img_y + (sub_y1 / target_orig_h) * geom.img_h;

        int cx0 = std::max(geom.img_x, (int)std::floor(px0));
        int cy0 = std::max(geom.img_y, (int)std::floor(py0));
        int cx1 = std::min(geom.img_x + geom.img_w, (int)std::ceil(px1));
        int cy1 = std::min(geom.img_y + geom.img_h, (int)std::ceil(py1));

        geom.proxy_x = cx0;
        geom.proxy_y = cy0;
        geom.proxy_w = std::max(4, cx1 - cx0);
        geom.proxy_h = std::max(4, cy1 - cy0);
    }

    geom.is_valid = true;
    return geom;
}

void VirtualViewport::draw_minimap(float target_orig_w, float target_orig_h, const uint8_t* thumb_data, int thumb_w, int thumb_h) {
    MinimapGeometry geom = compute_minimap_geometry(target_orig_w, target_orig_h);
    if (!geom.is_valid) return;

    // 1. Draw container background (dark slate HUD box)
    fl_color(fl_rgb_color(15, 23, 42)); // Slate 900
    fl_rectf(geom.box_x, geom.box_y, geom.box_w, geom.box_h);

    // 2. Draw thumbnail image
    if (thumb_data && thumb_w > 0 && thumb_h > 0 && geom.img_w > 0 && geom.img_h > 0) {
        std::vector<uint8_t> mini_rgb(geom.img_w * geom.img_h * 3);
        stbir_resize_uint8_linear(
            thumb_data, thumb_w, thumb_h, 0,
            mini_rgb.data(), geom.img_w, geom.img_h, 0,
            (stbir_pixel_layout)3
        );
        fl_draw_image(mini_rgb.data(), geom.img_x, geom.img_y, geom.img_w, geom.img_h, 3, 0);
    }

    // 3. Draw container border
    fl_color(fl_rgb_color(71, 85, 105)); // Slate 600
    fl_rect(geom.box_x, geom.box_y, geom.box_w, geom.box_h);

    // 4. Draw Viewport Proxy Box (High-contrast Sky Blue outline with 2px stroke)
    fl_color(fl_rgb_color(56, 189, 248)); // Tailwind Sky-400 (#38BDF8)
    fl_line_style(FL_SOLID, 2);
    fl_rect(geom.proxy_x, geom.proxy_y, geom.proxy_w, geom.proxy_h);
    fl_line_style(0); // Reset line style
}
