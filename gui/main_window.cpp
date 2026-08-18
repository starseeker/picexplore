#include "main_window.h"
#include "inotify_watcher.h"
#include "../database.h"
#include <FL/Fl.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/fl_ask.H>
#include <filesystem>
#include <thread>

static constexpr int MENU_H   = 25;
static constexpr int STATUS_H = 20;
static constexpr int SCROLL_W = 20;
static constexpr int INFO_W   = 250;

MainWindow::MainWindow(int w, int h, const char* title, const std::string& directory)
    : Fl_Double_Window(w, h, title), directory_(directory) {

    // Create ~/.cache/picexplore for tiles
    const char* home = getenv("HOME");
    std::string cache_dir = home ? std::string(home) + "/.cache/picexplore" : "/tmp/picexplore";
    tile_manager_ = new TileManager(update_queue_);
    tile_manager_->init(cache_dir);

    int vp_h = h - MENU_H - STATUS_H;

    menubar_ = new Fl_Menu_Bar(0, 0, w, MENU_H);
    menubar_->add("Sort/Alphabetical (A-Z)",    0, menu_cb, (void*)1);
    menubar_->add("Sort/Alphabetical (Z-A)",    0, menu_cb, (void*)2);
    menubar_->add("Sort/File Size (Smallest)",  0, menu_cb, (void*)3);
    menubar_->add("Sort/File Size (Largest)",   0, menu_cb, (void*)4);
    menubar_->add("Sort/Date (Oldest)",         0, menu_cb, (void*)5);
    menubar_->add("Sort/Date (Newest)",         0, menu_cb, (void*)6);

    menubar_->add("View/Zoom In (Ctrl+Wheel Up)",    FL_CTRL | '=', menu_cb, (void*)7);
    menubar_->add("View/Zoom Out (Ctrl+Wheel Down)", FL_CTRL | '-', menu_cb, (void*)8);
    menubar_->add("View/Reset Zoom",                 FL_CTRL | '0', menu_cb, (void*)9);
    menubar_->add("View/Information Panel",          0,             menu_cb, (void*)10, FL_MENU_TOGGLE);

    menubar_->add("View/Info Panel Font Size/Small (11pt)",   0, menu_cb, (void*)11, FL_MENU_RADIO);
    menubar_->add("View/Info Panel Font Size/Medium (14pt)",  0, menu_cb, (void*)12, FL_MENU_RADIO);
    menubar_->add("View/Info Panel Font Size/Large (18pt)",   0, menu_cb, (void*)13, FL_MENU_RADIO);
    menubar_->add("View/Info Panel Font Size/X-Large (24pt)", 0, menu_cb, (void*)14, FL_MENU_RADIO);
    menubar_->add("View/Info Panel Font Size/Custom...",      0, menu_cb, (void*)15, 0);

    menubar_->add("View/Reset Directory Filter", FL_CTRL | 'r', menu_cb, (void*)16, 0);

    viewport_  = new VirtualViewport(0, MENU_H, w - SCROLL_W, vp_h, store_);
    scrollbar_ = new Fl_Scrollbar(w - SCROLL_W, MENU_H, SCROLL_W, vp_h);
    scrollbar_->type(FL_VERTICAL);
    scrollbar_->callback(scroll_cb, this);

    info_panel_ = new InfoPanel(w, MENU_H, INFO_W, vp_h);
    info_panel_->hide();
    info_panel_->set_root_dir(directory_);

    // Status bar at the very bottom
    statusbar_ = new Fl_Box(0, MENU_H + vp_h, w, STATUS_H, "");
    statusbar_->box(FL_FLAT_BOX);
    statusbar_->color(fl_darker(FL_DARK2));
    statusbar_->labelcolor(fl_rgb_color(180, 180, 180));
    statusbar_->labelsize(12);
    statusbar_->labelfont(FL_HELVETICA);
    statusbar_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    // Wire info panel breadcrumb → directory filter
    // Only fires when info panel is already visible (panel must be shown to click it).
    info_panel_->on_dir_clicked = [this](const std::string& dir) {
        if (!scrollbar_->visible()) {
            exit_single_image_mode();
        }
        apply_directory_filter(dir);
    };

    info_panel_->on_file_clicked = [this](const std::string& filepath) {
        size_t idx = store_.find_by_filepath(filepath);
        if (idx != (size_t)-1) {
            enter_single_image_mode(idx, filepath);
        }
    };

    info_panel_->on_scroll_to_image = [this](const std::string& filepath) {
        size_t idx = store_.find_by_filepath(filepath);
        if (idx != (size_t)-1) {
            int target_y = viewport_->scroll_to_image(idx);
            scrollbar_->value(target_y);
            viewport_->set_scroll_offset(target_y);
            reprioritize_thumbnails();
        }
    };

    viewport_->on_image_clicked = [this](const std::string& path) {
        current_selected_filepath_ = path;
        size_t idx = store_.find_by_filepath(path);
        if (idx != (size_t)-1) {
            info_panel_->display_info(store_.get(idx));
            viewport_->set_selected_image(idx);
        }
    };

    viewport_->on_exit_single_image = [this]() {
        exit_single_image_mode();
    };

    end();
    resizable(viewport_);
}

MainWindow::~MainWindow() {
    if (scanner_) { scanner_->stop(); delete scanner_; }
    if (pipeline_) { pipeline_->stop(); delete pipeline_; }
    if (full_res_loader_) { delete full_res_loader_; }
    if (watcher_) { watcher_->stop(); delete watcher_; }
    if (db_) { delete db_; }
    Fl::remove_timeout(timer_cb, this);
}

void MainWindow::start() {
    std::string db_path = "./images.db";
    
    db_ = new DatabaseManager();
    db_->open(db_path);

    full_res_loader_ = new FullResLoader(update_queue_);

    pipeline_ = new ThumbnailPipeline(update_queue_, db_path);
    int num_threads = std::thread::hardware_concurrency();
    if (num_threads < 4) num_threads = 4;
    pipeline_->start(num_threads);

    scanner_ = new ScanCoordinator(directory_, update_queue_, db_path);
    scanner_->start();
    
    watcher_ = new InotifyWatcher();
    watcher_->start(directory_, update_queue_);

    Fl::add_timeout(0.016, timer_cb, this);
}

// ── filter helpers ─────────────────────────────────────────────────────────

void MainWindow::enter_single_image_mode(size_t raw_idx, const std::string& filepath) {
    pre_viewer_filter_ = directory_filter_;
    viewport_->enter_single_image(raw_idx);
    
    auto& entry = store_.get(raw_idx);
    long long pixels = (long long)entry.original_width * entry.original_height;
    if (pixels > 16384LL * 16384LL || pixels <= 0) { // Fallback <= 0 to tiles if extremely large or unknown
        // For very large images, use tile manager
        std::string label = "  Viewing: " + std::filesystem::path(filepath).filename().string() + "  [Generating Map...]";
        statusbar_->copy_label(label.c_str());
        viewport_->set_tile_manager(tile_manager_, entry.content_hash, entry.original_width, entry.original_height);
        tile_manager_->request_tiles(raw_idx, filepath, entry.content_hash);
    } else {
        // Normal image
        viewport_->set_tile_manager(nullptr, "", entry.original_width, entry.original_height);
        full_res_loader_->request(raw_idx, filepath);
        std::string filename = std::filesystem::path(filepath).filename().string();
        std::string label = "  Viewing: " + filename + "  [Loading full resolution...]";
        statusbar_->copy_label(label.c_str());
    }
    statusbar_->redraw();
    
    scrollbar_->hide();
    int vp_h = h() - MENU_H - STATUS_H;
    int vp_w = w();
    if (info_panel_visible_) vp_w -= INFO_W;
    viewport_->resize(0, MENU_H, vp_w, vp_h); // Expand over scrollbar
}

void MainWindow::exit_single_image_mode() {
    viewport_->exit_single_image();
    directory_filter_ = pre_viewer_filter_;
    full_res_loader_->cancel();
    
    scrollbar_->show();
    int vp_h = h() - MENU_H - STATUS_H;
    int vp_w = w() - SCROLL_W;
    if (info_panel_visible_) vp_w -= INFO_W;
    viewport_->resize(0, MENU_H, vp_w, vp_h);
    
    layout_dirty_ = true;
    update_statusbar();
}

void MainWindow::apply_directory_filter(const std::string& dir) {
    directory_filter_ = dir;
    layout_dirty_ = true;
    viewport_->set_scroll_offset(0);
    scrollbar_->value(0);
    update_statusbar();
}

void MainWindow::reset_directory_filter() {
    directory_filter_.clear();
    layout_dirty_ = true;
    viewport_->set_scroll_offset(0);
    scrollbar_->value(0);
    update_statusbar();
}

void MainWindow::update_statusbar() {
    std::string label;
    if (!directory_filter_.empty()) {
        // Show the filter as a path relative to the launch root for brevity
        namespace fs = std::filesystem;
        std::string rel;
        try {
            rel = fs::relative(directory_filter_, std::filesystem::path(directory_).parent_path()).string();
        } catch (...) {
            rel = directory_filter_;
        }
        label = "  Filter: " + rel + "/";
    }

    if (db_build_total_ > 0 && !(scan_complete_ && db_build_completed_ >= db_build_total_)) {
        label += "   |   Building Thumbnails: " + std::to_string(db_build_completed_) + " / " + std::to_string(db_build_total_);
        if (!scan_complete_) {
            label += " (Scanning...)";
        }
    }
    
    statusbar_->copy_label(label.c_str());
    statusbar_->redraw();
}

// ── timer / event poll ────────────────────────────────────────────────────

void MainWindow::timer_cb(void* data) {
    MainWindow* win = static_cast<MainWindow*>(data);
    win->poll_events();
    Fl::repeat_timeout(0.016, timer_cb, data);
}

void MainWindow::poll_events() {
    bool need_redraw = false;
    std::vector<size_t> changed;
    
    UpdateEvent ev;
    int process_count = 0;
    while (update_queue_.try_dequeue(ev) && process_count < 200) {
        process_count++;
        if (ev.type == UpdateEvent::Type::IMAGE_DISCOVERED) {
            size_t idx = store_.add_image(ev.image.filepath, ev.image.aspect_ratio,
                                          ev.image.width, ev.image.height,
                                          ev.image.file_size, ev.image.file_timestamp);
            
            store_.get(idx).best_quality = ev.image.best_quality;
            store_.get(idx).content_hash = ev.image.content_hash;
            layout_dirty_ = true;
            
            if (ev.image.best_quality == ThumbQuality::NONE) {
                db_build_total_++;
                update_statusbar();
                
                // Queue a background task to generate the thumbnail so the DB fully populates.
                // Generation 0 ensures it is not discarded when the user scrolls.
                pipeline_->request_thumbnail(idx, ev.image.filepath, ev.image.content_hash, 
                                             ThumbQuality::SMALL, false, 0, 0, 0);
            }
        } else if (ev.type == UpdateEvent::Type::THUMB_READY) {
            bool was_none = (store_.get(ev.thumb.image_index).best_quality == ThumbQuality::NONE);
            store_.set_thumbnail(ev.thumb.image_index, ev.thumb.filepath, ev.thumb.quality,
                                 ev.thumb.jpeg_data.data(), ev.thumb.jpeg_data.size(),
                                 ev.thumb.width, ev.thumb.height);
            changed.push_back(ev.thumb.image_index);
            need_redraw = true;
            if (was_none) {
                db_build_completed_++;
                update_statusbar();
            }
        } else if (ev.type == UpdateEvent::Type::THUMB_RGB_READY) {
            bool was_none = (store_.get(ev.thumb_rgb.image_index).best_quality == ThumbQuality::NONE);
            store_.set_thumbnail_rgb(ev.thumb_rgb.image_index, ev.thumb_rgb.filepath,
                                     ev.thumb_rgb.quality, std::move(ev.thumb_rgb.rgb_data),
                                     ev.thumb_rgb.width, ev.thumb_rgb.height, ev.thumb_rgb.generation);
            changed.push_back(ev.thumb_rgb.image_index);
            need_redraw = true;
            if (was_none) {
                db_build_completed_++;
                update_statusbar();
            }
        } else if (ev.type == UpdateEvent::Type::THUMB_FAILED) {
            bool was_none = (store_.get(ev.failed.image_index).best_quality == ThumbQuality::NONE);
            // Mark the entry as FAILED in the ImageStore
            store_.set_thumbnail(ev.failed.image_index, ev.failed.filepath, ThumbQuality::FAILED, nullptr, 0, 0, 0);
            changed.push_back(ev.failed.image_index);
            need_redraw = true;
            if (was_none) {
                db_build_completed_++;
                update_statusbar();
            }
        } else if (ev.type == UpdateEvent::Type::FULL_RES_READY) {
            if (viewport_->current_mode() == VirtualViewport::ViewMode::SINGLE_IMAGE &&
                viewport_->current_single_image() == ev.full_res.image_index) {
                viewport_->set_full_res_image(ev.full_res.rgb_data, ev.full_res.width, ev.full_res.height);
                
                std::string filename = std::filesystem::path(ev.full_res.filepath).filename().string();
                std::string label = "  Viewing: " + filename;
                statusbar_->copy_label(label.c_str());
                statusbar_->redraw();
            }
        } else if (ev.type == UpdateEvent::Type::IMAGE_DELETED) {
            std::cout << "DELETED: " << ev.deletion.filepath << std::endl;
            store_.remove_image(ev.deletion.filepath);
            if (db_ && db_->is_open()) {
                std::lock_guard<std::mutex> lock(db_->get_mutex());
                if (db_->begin_transaction()) {
                    std::string hash;
                    if (db_->get_hash_for_path(ev.deletion.filepath, hash)) {
                        db_->delete_key(hash + ":path");
                    }
                    db_->delete_key("file:" + ev.deletion.filepath);
                    db_->commit_transaction();
                }
            }
            layout_dirty_ = true;
        } else if (ev.type == UpdateEvent::Type::IMAGE_RENAMED) {
            std::cout << "RENAMED: " << ev.rename.old_filepath
                      << " -> " << ev.rename.new_filepath << std::endl;
            store_.rename_image(ev.rename.old_filepath, ev.rename.new_filepath);
            if (db_ && db_->is_open()) {
                std::lock_guard<std::mutex> lock(db_->get_mutex());
                if (db_->begin_transaction()) {
                    std::string hash;
                    if (db_->get_hash_for_path(ev.rename.old_filepath, hash)) {
                        db_->delete_key("file:" + ev.rename.old_filepath);
                        db_->store_key_value(hash + ":path", ev.rename.new_filepath);
                        db_->store_key_value("file:" + ev.rename.new_filepath, hash);
                    }
                    db_->commit_transaction();
                }
            }
        } else if (ev.type == UpdateEvent::Type::SCAN_COMPLETE) {
            std::cout << "Scan complete." << std::endl;
            scan_complete_ = true;
            update_statusbar();
        }
    }

    if (layout_dirty_) {
        // Use get_filtered_aspects so the layout carries real raw store indices.
        auto indexed = store_.get_filtered_aspects(directory_filter_);
        layout_result_ = layout_engine_.compute(indexed, viewport_->content_width(), target_height_);
        viewport_->set_layout(&layout_result_);
        
        int max_scroll = std::max(0, static_cast<int>(layout_result_.total_height) - viewport_->h());
        int current_scroll = std::clamp(viewport_->scroll_offset(), 0, max_scroll);
        
        viewport_->set_scroll_offset(current_scroll);
        scrollbar_->value(current_scroll, viewport_->h(), 0, layout_result_.total_height);
        
        layout_dirty_ = false;
        need_redraw = true;
        
        reprioritize_thumbnails();
    } else if (need_redraw) {
        viewport_->apply_updates(changed);
    }
}

void MainWindow::reprioritize_thumbnails() {
    auto visible = viewport_->get_visible_indices();
    store_.mark_visible(visible);
    
    bool view_changed = false;
    if (visible.size() != last_visible_.size() || 
        target_height_ != last_target_height_ ||
        viewport_->w() != last_viewport_width_) {
        view_changed = true;
    } else {
        for (size_t i = 0; i < visible.size(); ++i) {
            if (visible[i] != last_visible_[i]) {
                view_changed = true;
                break;
            }
            
            const auto& entry = store_.get(visible[i]);
            bool size_mismatch = (entry.scaled.layout_width == 0); // basic check before layout recalculation
            if ((entry.best_quality == ThumbQuality::NONE || entry.scaled.rgb_data.empty() || size_mismatch) &&
                entry.last_requested_generation < current_generation_) {
                view_changed = true;
                break;
            }
        }
    }
    
    if (!view_changed) return;
    
    current_generation_++;
    pipeline_->set_generation(current_generation_);
    last_visible_ = visible;
    last_target_height_ = target_height_;
    last_viewport_width_ = viewport_->w();
    
    for (size_t idx : visible) {
        // idx is a raw store index (box.image_index carries the raw index now)
        const auto& entry = store_.get(idx);
        
        double layout_w = entry.aspect_ratio * target_height_;
        double layout_h = target_height_;
        // Find this raw index in the layout boxes
        for (const auto& box : layout_result_.boxes) {
            if (box.image_index == idx) {
                layout_w = box.w;
                layout_h = box.h;
                break;
            }
        }
        
        ThumbQuality needed = ThumbQuality::SMALL;
        if (layout_w < 96)       needed = ThumbQuality::SMALL;
        else if (layout_w < 192) needed = ThumbQuality::MEDIUM;
        else if (layout_w < 384) needed = ThumbQuality::LARGE;
        else if (layout_w < 768) needed = ThumbQuality::XLARGE;
        else                     needed = ThumbQuality::FULL;
        
        bool size_mismatch = (entry.scaled.layout_width  != static_cast<int>(layout_w) ||
                              entry.scaled.layout_height != static_cast<int>(layout_h));
        
        bool missing_or_mismatch = (entry.best_quality == ThumbQuality::NONE || entry.scaled.rgb_data.empty() || size_mismatch);
        
        if (missing_or_mismatch) {
            store_.get(idx).last_requested_generation = current_generation_;
            
            if (entry.best_quality == ThumbQuality::NONE) {
                pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash,
                                             ThumbQuality::SMALL, true, current_generation_,
                                             static_cast<int>(layout_w), static_cast<int>(layout_h));
            } else if (entry.scaled.rgb_data.empty()) {
                pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash,
                                             ThumbQuality::SMALL, true, current_generation_,
                                             static_cast<int>(layout_w), static_cast<int>(layout_h));
                if (needed > ThumbQuality::SMALL) {
                    pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash,
                                                 needed, false, current_generation_,
                                                 static_cast<int>(layout_w), static_cast<int>(layout_h));
                }
            } else if (entry.best_quality < needed || size_mismatch) {
                ThumbQuality req_quality = std::max(needed, entry.best_quality);
                pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash,
                                             req_quality, true, current_generation_,
                                             static_cast<int>(layout_w), static_cast<int>(layout_h));
            }
        }
    }
}

// ── resize ─────────────────────────────────────────────────────────────────

void MainWindow::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);
    int vp_h = H - MENU_H - STATUS_H;

    int info_w = INFO_W;
    int vp_w = W - SCROLL_W;
    if (info_panel_visible_) {
        vp_w -= info_w;
    }

    menubar_->resize(0, 0, W, MENU_H);
    viewport_->resize(0, MENU_H, vp_w, vp_h);
    scrollbar_->resize(vp_w, MENU_H, SCROLL_W, vp_h);
    statusbar_->resize(0, MENU_H + vp_h, W, STATUS_H);

    if (info_panel_visible_) {
        info_panel_->resize(vp_w + SCROLL_W, MENU_H, info_w, vp_h);
        info_panel_->show();
    } else {
        info_panel_->hide();
    }
    
    layout_dirty_ = true;
}

// ── callbacks ──────────────────────────────────────────────────────────────

void MainWindow::scroll_cb(Fl_Widget* w, void* data) {
    MainWindow* win = static_cast<MainWindow*>(data);
    win->viewport_->set_scroll_offset(win->scrollbar_->value());
    win->reprioritize_thumbnails();
}

void MainWindow::menu_cb(Fl_Widget* w, void* data) {
    MainWindow* win = static_cast<MainWindow*>(w->window());
    if (!win) return;
    
    int choice = (int)(intptr_t)data;
    
    ImageStore::SortCriteria criteria;
    bool ascending = true;
    
    switch (choice) {
        case 1: criteria = ImageStore::SortCriteria::ALPHABETICAL; ascending = true;  break;
        case 2: criteria = ImageStore::SortCriteria::ALPHABETICAL; ascending = false; break;
        case 3: criteria = ImageStore::SortCriteria::FILE_SIZE;    ascending = true;  break;
        case 4: criteria = ImageStore::SortCriteria::FILE_SIZE;    ascending = false; break;
        case 5: criteria = ImageStore::SortCriteria::TIMESTAMP;    ascending = true;  break;
        case 6: criteria = ImageStore::SortCriteria::TIMESTAMP;    ascending = false; break;
        case 7: win->target_height_ = std::min(win->target_height_ * 1.2, 800.0); win->layout_dirty_ = true; break;
        case 8: win->target_height_ = std::max(win->target_height_ / 1.2,  50.0); win->layout_dirty_ = true; break;
        case 9: win->target_height_ = 150.0; win->layout_dirty_ = true; break;
        case 10:
            win->info_panel_visible_ = !win->info_panel_visible_;
            win->resize(win->x(), win->y(), win->w(), win->h());
            break;
        case 11: win->info_panel_->set_font_size(11); break;
        case 12: win->info_panel_->set_font_size(14); break;
        case 13: win->info_panel_->set_font_size(18); break;
        case 14: win->info_panel_->set_font_size(24); break;
        case 15: {
            const char* val = fl_input("Enter font size (8\u201348):", "14");
            if (val) {
                int sz = std::atoi(val);
                if (sz >= 8 && sz <= 48) {
                    win->info_panel_->set_font_size(sz);
                } else {
                    fl_alert("Please enter a size between 8 and 48.");
                }
            }
            break;
        }
        case 16: win->reset_directory_filter(); break;
        default: return;
    }
    
    if (choice >= 1 && choice <= 6) {
        win->store_.sort_entries(criteria, ascending);
        win->layout_dirty_ = true;
        win->viewport_->set_scroll_offset(0);
    }
}

int MainWindow::handle(int event) {
    if (event == FL_MOUSEWHEEL) {
        if (!scrollbar_->visible()) {
            return Fl_Double_Window::handle(event);
        }
        int dy = Fl::event_dy();
        if (Fl::event_state() & FL_CTRL) {
            if (dy > 0) {
                target_height_ = std::max(target_height_ / 1.2, 50.0);
            } else if (dy < 0) {
                target_height_ = std::min(target_height_ * 1.2, 800.0);
            }
            layout_dirty_ = true;
            return 1;
        } else {
            int new_val = scrollbar_->value() + dy * 50;
            if (new_val < 0) new_val = 0;
            // scrollbar_->maximum() is already total_height - viewport_h;
            // do NOT subtract viewport_->h() again or the bound goes negative.
            if (new_val > scrollbar_->maximum())
                new_val = scrollbar_->maximum();
            if (new_val < 0) new_val = 0;
            scrollbar_->value(new_val);
            scroll_cb(scrollbar_, this);
            return 1;
        }
    }
    return Fl_Double_Window::handle(event);
}
