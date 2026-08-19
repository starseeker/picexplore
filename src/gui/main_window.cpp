#include "main_window.h"
#include "inotify_watcher.h"
#include "../database.h"
#include <FL/Fl.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/fl_ask.H>
#include <filesystem>
#include <thread>
#include <unordered_set>
#include <xxhash.h>

static constexpr int MENU_H   = 25;
static constexpr int STATUS_H = 20;
static constexpr int SCROLL_W = 20;
static constexpr int INFO_W   = 250;

MainWindow::MainWindow(int w, int h, const char* title, const std::string& directory, const std::string& db_path)
    : Fl_Double_Window(w, h, title), directory_(directory), db_path_(db_path) {

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
    menubar_->add("Sort/Pixel Area (Smallest)", 0, menu_cb, (void*)17);
    menubar_->add("Sort/Pixel Area (Largest)",  0, menu_cb, (void*)18);
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

    statusbar_hint_ = new Fl_Box(w - 220, MENU_H + vp_h, 220, STATUS_H, "");
    statusbar_hint_->box(FL_FLAT_BOX);
    statusbar_hint_->color(fl_darker(FL_DARK2));
    statusbar_hint_->labelcolor(fl_rgb_color(150, 190, 240));
    statusbar_hint_->labelsize(12);
    statusbar_hint_->labelfont(FL_HELVETICA);
    statusbar_hint_->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    statusbar_hint_->hide();

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

    info_panel_->on_exit_image_view = [this]() {
        exit_single_image_mode();
    };

    viewport_->on_image_clicked = [this](const std::string& path) {
        if (path.empty()) {
            current_selected_filepath_.clear();
            viewport_->set_selected_image((size_t)-1);
            info_panel_->clear_info();
            return;
        }
        size_t idx = store_.find_by_filepath(path);
        if (idx != (size_t)-1) {
            if (viewport_->get_selected_image() == idx) {
                // Clicking already selected image unselects it
                current_selected_filepath_.clear();
                viewport_->set_selected_image((size_t)-1);
                info_panel_->clear_info();
            } else {
                current_selected_filepath_ = path;
                info_panel_->display_info(store_.get(idx));
                viewport_->set_selected_image(idx);
            }
        }
    };

    viewport_->on_image_double_clicked = [this](const std::string& path) {
        size_t idx = store_.find_by_filepath(path);
        if (idx != (size_t)-1) {
            enter_single_image_mode(idx, path);
        }
    };

    viewport_->on_exit_single_image = [this]() {
        exit_single_image_mode();
    };

    viewport_->on_navigate_single_image = [this](int delta) {
        navigate_single_image(delta);
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
    namespace fs = std::filesystem;
    
    // Resolve database path
    if (db_path_.empty()) {
        std::string canon_dir = directory_;
        try {
            canon_dir = fs::canonical(directory_).string();
        } catch (...) {}

        fs::path local_db = fs::path(canon_dir) / "images.db";
        if (fs::exists(local_db)) {
            db_path_ = local_db.string();
        } else if (fs::exists("./images.db")) {
            db_path_ = "./images.db";
        } else {
            const char* home = getenv("HOME");
            fs::path cache_dbs = home ? (fs::path(home) / ".cache" / "picexplore" / "databases") : fs::path("/tmp/picexplore/databases");
            try {
                fs::create_directories(cache_dbs);
            } catch (...) {}

            XXH128_hash_t h = XXH3_128bits(canon_dir.data(), canon_dir.size());
            char buf[64];
            snprintf(buf, sizeof(buf), "%016llx%016llx.db",
                     (unsigned long long)h.high64, (unsigned long long)h.low64);
            db_path_ = (cache_dbs / buf).string();
        }
    }

    std::cout << "Using database: " << db_path_ << std::endl;
    
    db_ = new DatabaseManager();
    db_->open(db_path_);

    full_res_loader_ = new FullResLoader(update_queue_);

    pipeline_ = new ThumbnailPipeline(update_queue_, db_path_);
    int num_threads = std::thread::hardware_concurrency();
    if (num_threads < 4) num_threads = 4;
    pipeline_->start(num_threads);

    scanner_ = new ScanCoordinator(directory_, update_queue_, db_path_);
    scanner_->start();
    
    watcher_ = new InotifyWatcher();
    watcher_->start(directory_, update_queue_);

    update_statusbar();

    Fl::add_timeout(0.016, timer_cb, this);
}

// ── filter helpers ─────────────────────────────────────────────────────────

void MainWindow::enter_single_image_mode(size_t raw_idx, const std::string& filepath) {
    pre_viewer_filter_ = directory_filter_;
    viewport_->enter_single_image(raw_idx);
    
    auto& entry = store_.get(raw_idx);
    long long pixels = (long long)entry.original_width * entry.original_height;
    
    int screen_dim = std::max(w(), h());
    int max_overview_size = entry.original_width > 0 ? std::min((int)entry.original_width, 8192) : 8192;
    if (entry.content_hash.empty()) {
        std::string key = filepath;
        try {
            key = std::filesystem::canonical(filepath).string();
            key += ":" + std::to_string(std::filesystem::file_size(filepath));
            key += ":" + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                std::filesystem::last_write_time(filepath).time_since_epoch()).count());
        } catch (...) {}
        XXH128_hash_t h = XXH3_128bits(key.data(), key.size());
        char buf[33];
        snprintf(buf, sizeof(buf), "%016llx%016llx",
                 (unsigned long long)h.high64, (unsigned long long)h.low64);
        entry.content_hash = buf;
    }

    ThumbQuality target_quality = static_cast<ThumbQuality>(std::max(max_overview_size, screen_dim));
    if (static_cast<int>(entry.scaled.quality) < static_cast<int>(target_quality) || entry.scaled.rgb_data.empty()) {
        entry.last_requested_generation = current_generation_;
        pipeline_->request_thumbnail(raw_idx, filepath, entry.content_hash, target_quality, true, current_generation_);
    }

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
    
    int vp_h = h() - MENU_H - STATUS_H;
    int vp_w = w();
    if (info_panel_visible_) vp_w -= INFO_W;

    statusbar_->resize(0, MENU_H + vp_h, w() - 280, STATUS_H);
    statusbar_hint_->resize(w() - 280, MENU_H + vp_h, 280, STATUS_H);
    statusbar_hint_->copy_label("Left/Right: Prev/Next  |  Esc: Exit  ");
    statusbar_hint_->show();
    statusbar_->redraw();
    statusbar_hint_->redraw();

    scrollbar_->hide();
    info_panel_->set_single_image_mode(true);
    viewport_->resize(0, MENU_H, vp_w, vp_h); // Expand over scrollbar
}

void MainWindow::navigate_single_image(int delta) {
    if (viewport_->current_mode() != VirtualViewport::ViewMode::SINGLE_IMAGE) return;
    if (layout_result_.boxes.empty()) return;

    size_t current_idx = viewport_->current_single_image();
    
    int current_pos = -1;
    for (size_t i = 0; i < layout_result_.boxes.size(); ++i) {
        if (layout_result_.boxes[i].image_index == current_idx) {
            current_pos = static_cast<int>(i);
            break;
        }
    }

    if (current_pos == -1) {
        current_pos = 0;
    }

    int next_pos = current_pos + delta;
    if (next_pos < 0 || next_pos >= static_cast<int>(layout_result_.boxes.size())) {
        // Stop at boundaries rather than wrapping around
        return;
    }

    size_t next_raw_idx = layout_result_.boxes[next_pos].image_index;
    const auto& next_entry = store_.get(next_raw_idx);

    current_selected_filepath_ = next_entry.filepath;
    viewport_->set_selected_image(next_raw_idx);
    info_panel_->display_info(next_entry);
    enter_single_image_mode(next_raw_idx, next_entry.filepath);
}

void MainWindow::exit_single_image_mode() {
    viewport_->exit_single_image();
    info_panel_->set_single_image_mode(false);
    directory_filter_ = pre_viewer_filter_;
    full_res_loader_->cancel();
    
    int vp_h = h() - MENU_H - STATUS_H;
    statusbar_hint_->copy_label("");
    statusbar_hint_->hide();
    statusbar_->resize(0, MENU_H + vp_h, w(), STATUS_H);

    scrollbar_->show();
    int vp_w = w() - SCROLL_W;
    if (info_panel_visible_) vp_w -= INFO_W;
    viewport_->resize(0, MENU_H, vp_w, vp_h);
    
    last_visible_.clear();
    update_statusbar();
    recompute_layout();

    size_t sel = viewport_->get_selected_image();
    if (sel != (size_t)-1) {
        int target_y = viewport_->scroll_to_image(sel);
        viewport_->set_scroll_offset(target_y);
        scrollbar_->value(target_y);
        reprioritize_thumbnails();
    }
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
    
    if (viewport_->current_mode() == VirtualViewport::ViewMode::SINGLE_IMAGE) {
        size_t idx = viewport_->current_single_image();
        std::string filename = std::filesystem::path(store_.get(idx).filepath).filename().string();
        label = "  Viewing: " + filename;
        statusbar_->copy_label(label.c_str());
        statusbar_->redraw();
        return;
    }

    size_t total_images = store_.size();

    if (!directory_filter_.empty()) {
        // Show the filter as a path relative to the launch root for brevity
        namespace fs = std::filesystem;
        std::string rel;
        try {
            rel = fs::relative(directory_filter_, std::filesystem::path(directory_).parent_path()).string();
        } catch (...) {
            rel = directory_filter_;
        }
        auto filtered = store_.get_filtered_aspects(directory_filter_);
        label = "  Filter: " + rel + "/ (" + std::to_string(filtered.size()) + " of " + std::to_string(total_images) + (total_images == 1 ? " image)" : " images)");
    } else {
        if (total_images == 0 && !scan_complete_) {
            label = "  Scanning...";
        } else {
            label = "  " + std::to_string(total_images) + (total_images == 1 ? " image" : " images");
        }
    }

    int completed = db_build_total_ - static_cast<int>(pending_db_build_.size());
    bool building = (!pending_db_build_.empty());
    if (building) {
        label += "   |   Building Thumbnails: " + std::to_string(std::max(0, completed)) + " / " + std::to_string(db_build_total_);
        if (!scan_complete_) {
            label += " (Scanning...)";
        }
    } else if (!scan_complete_) {
        label += "   |   Scanning...";
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
    bool status_dirty = false;
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
            status_dirty = true;
            
            if (ev.image.best_quality == ThumbQuality::NONE) {
                pending_db_build_.insert(idx);
                db_build_total_++;
                
                // Queue a background task to generate the thumbnail so the DB fully populates.
                // Generation 0 ensures it is not discarded when the user scrolls.
                pipeline_->request_thumbnail(idx, ev.image.filepath, ev.image.content_hash, 
                                             ThumbQuality::LARGE, false, 0, 0, 0);
            }
        } else if (ev.type == UpdateEvent::Type::SCAN_PROGRESS) {
            status_dirty = true;
        } else if (ev.type == UpdateEvent::Type::THUMB_READY) {
            store_.set_thumbnail(ev.thumb.image_index, ev.thumb.filepath, ev.thumb.quality,
                                 ev.thumb.jpeg_data.data(), ev.thumb.jpeg_data.size(),
                                 ev.thumb.width, ev.thumb.height);
            changed.push_back(ev.thumb.image_index);
            need_redraw = true;
            if (pending_db_build_.erase(ev.thumb.image_index) > 0) {
                status_dirty = true;
            }
        } else if (ev.type == UpdateEvent::Type::THUMB_RGB_READY) {
            auto& entry = store_.get(ev.thumb_rgb.image_index);
            if (entry.content_hash.empty() && !ev.thumb_rgb.content_hash.empty()) {
                entry.content_hash = ev.thumb_rgb.content_hash;
            }
            store_.set_thumbnail_rgb(ev.thumb_rgb.image_index, ev.thumb_rgb.filepath,
                                     ev.thumb_rgb.quality, std::move(ev.thumb_rgb.rgb_data),
                                     ev.thumb_rgb.width, ev.thumb_rgb.height, ev.thumb_rgb.generation);
            changed.push_back(ev.thumb_rgb.image_index);
            need_redraw = true;
            if (pending_db_build_.erase(ev.thumb_rgb.image_index) > 0) {
                status_dirty = true;
            }
            // Always update statusbar in single image mode to clear any "Generating" text
            if (viewport_->current_mode() == VirtualViewport::ViewMode::SINGLE_IMAGE &&
                viewport_->current_single_image() == ev.thumb_rgb.image_index) {
                status_dirty = true;
            }
        } else if (ev.type == UpdateEvent::Type::THUMB_FAILED) {
            // Mark the entry as FAILED in the ImageStore
            store_.set_thumbnail(ev.failed.image_index, ev.failed.filepath, ThumbQuality::FAILED, nullptr, 0, 0, 0);
            changed.push_back(ev.failed.image_index);
            need_redraw = true;
            if (pending_db_build_.erase(ev.failed.image_index) > 0) {
                status_dirty = true;
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
        } else if (ev.type == UpdateEvent::Type::TILE_GENERATION_PROGRESS) {
            if (viewport_->current_mode() == VirtualViewport::ViewMode::SINGLE_IMAGE &&
                viewport_->current_single_image() == ev.tile_progress.image_index) {
                std::string filename = std::filesystem::path(ev.tile_progress.filepath).filename().string();
                int percent = (ev.tile_progress.current_row * 100) / std::max(1, ev.tile_progress.total_rows);
                std::string label = "  Viewing: " + filename + "  [Generating Map: " + std::to_string(percent) + "%]";
                statusbar_->copy_label(label.c_str());
                statusbar_->redraw();
            }
        } else if (ev.type == UpdateEvent::Type::THUMB_GENERATION_PROGRESS) {
            if (viewport_->current_mode() == VirtualViewport::ViewMode::SINGLE_IMAGE &&
                viewport_->current_single_image() == ev.thumb_progress.image_index) {
                std::string filename = std::filesystem::path(ev.thumb_progress.filepath).filename().string();
                std::string label = "  Viewing: " + filename + "  [Generating High-Res Overview: " + std::to_string(ev.thumb_progress.percent) + "%]";
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
            status_dirty = true;
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
            status_dirty = true;
        } else if (ev.type == UpdateEvent::Type::SCAN_COMPLETE) {
            std::cout << "Scan complete." << std::endl;
            scan_complete_ = true;
            status_dirty = true;
        }
    }

    if (status_dirty) {
        update_statusbar();
    }

    if (layout_dirty_) {
        recompute_layout(true);
    } else if (need_redraw) {
        viewport_->apply_updates(changed);
    }

    if (reprioritize_pending_) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_resize_time_).count();
        if (elapsed >= 50) {
            reprioritize_pending_ = false;
            if (viewport_->current_mode() == VirtualViewport::ViewMode::GRID) {
                reprioritize_thumbnails();
            }
        }
    }
}

void MainWindow::recompute_layout(bool reprioritize) {
    auto indexed = store_.get_filtered_aspects(directory_filter_);
    layout_result_ = layout_engine_.compute(indexed, viewport_->content_width(), target_height_);
    viewport_->set_layout(&layout_result_);
    
    int max_scroll = std::max(0, static_cast<int>(layout_result_.total_height) - viewport_->h());
    int current_scroll = std::clamp(viewport_->scroll_offset(), 0, max_scroll);
    
    viewport_->set_scroll_offset(current_scroll);
    scrollbar_->value(current_scroll, viewport_->h(), 0, layout_result_.total_height);
    
    layout_dirty_ = false;
    viewport_->redraw();
    
    if (reprioritize && viewport_->current_mode() == VirtualViewport::ViewMode::GRID) {
        reprioritize_thumbnails();
    }
}

void MainWindow::reprioritize_thumbnails() {
    if (viewport_->current_mode() != VirtualViewport::ViewMode::GRID) return;

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
            double layout_w = entry.aspect_ratio * target_height_;
            bool size_mismatch = (entry.scaled.layout_width == 0 ||
                                  std::abs(entry.scaled.layout_width - (int)layout_w) > 5);
            
            // Re-calculate basic needed quality for the check
            ThumbQuality needed = ThumbQuality::SMALL;
            if (layout_w <= 64)       needed = ThumbQuality::SMALL;
            else if (layout_w <= 128) needed = ThumbQuality::MEDIUM;
            else if (layout_w <= 256) needed = ThumbQuality::LARGE;
            else if (layout_w <= 512) needed = ThumbQuality::XLARGE;
            else if (layout_w <= 1024) needed = static_cast<ThumbQuality>(1024);
            else                      needed = ThumbQuality::FULL;
            
            bool needs_upgrade = (entry.scaled.quality < needed);
            
            if ((entry.best_quality == ThumbQuality::NONE || entry.scaled.rgb_data.empty() || size_mismatch || needs_upgrade) &&
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
        if (layout_w <= 64)       needed = ThumbQuality::SMALL;
        else if (layout_w <= 128) needed = ThumbQuality::MEDIUM;
        else if (layout_w <= 256) needed = ThumbQuality::LARGE;
        else if (layout_w <= 512) needed = ThumbQuality::XLARGE;
        else if (layout_w <= 1024) needed = static_cast<ThumbQuality>(1024);
        else                      needed = ThumbQuality::FULL;
        
        bool size_mismatch = (entry.scaled.layout_width  != static_cast<int>(layout_w) ||
                              entry.scaled.layout_height != static_cast<int>(layout_h));
        bool needs_upgrade = (entry.scaled.quality < needed);
        
        bool missing_or_mismatch = (entry.best_quality == ThumbQuality::NONE || entry.scaled.rgb_data.empty() || size_mismatch || needs_upgrade);
        
        if (missing_or_mismatch) {
            store_.get(idx).last_requested_generation = current_generation_;
            ThumbQuality req_quality = std::max(needed, entry.best_quality);
            pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash,
                                         req_quality, true, current_generation_,
                                         static_cast<int>(layout_w), static_cast<int>(layout_h));
        }
    }
    
    // Prefetch thumbnails just outside the viewport (e.g. +/- 2 screens)
    auto prefetch = viewport_->get_visible_indices(viewport_->h() * 2);
    std::unordered_set<size_t> visible_set(visible.begin(), visible.end());
    
    for (size_t idx : prefetch) {
        if (visible_set.count(idx)) continue;
        
        const auto& entry = store_.get(idx);
        
        double layout_w = entry.aspect_ratio * target_height_;
        double layout_h = target_height_;
        for (const auto& box : layout_result_.boxes) {
            if (box.image_index == idx) {
                layout_w = box.w;
                layout_h = box.h;
                break;
            }
        }
        
        // Prefetch base thumbnails
        ThumbQuality needed = ThumbQuality::LARGE;
        bool size_mismatch = (entry.scaled.layout_width  != static_cast<int>(layout_w) ||
                              entry.scaled.layout_height != static_cast<int>(layout_h));
        bool missing_or_mismatch = (entry.best_quality == ThumbQuality::NONE || entry.scaled.rgb_data.empty() || size_mismatch);
        
        if (missing_or_mismatch && entry.last_requested_generation < current_generation_) {
            store_.get(idx).last_requested_generation = current_generation_;
            pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash,
                                         needed, false, current_generation_,
                                         static_cast<int>(layout_w), static_cast<int>(layout_h));
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

    if (viewport_->current_mode() == VirtualViewport::ViewMode::SINGLE_IMAGE) {
        statusbar_->resize(0, MENU_H + vp_h, W - 220, STATUS_H);
        statusbar_hint_->resize(W - 220, MENU_H + vp_h, 220, STATUS_H);
    } else {
        statusbar_->resize(0, MENU_H + vp_h, W, STATUS_H);
        statusbar_hint_->resize(W - 220, MENU_H + vp_h, 220, STATUS_H);
    }

    if (info_panel_visible_) {
        info_panel_->resize(vp_w + SCROLL_W, MENU_H, info_w, vp_h);
        info_panel_->show();
    } else {
        info_panel_->hide();
    }
    
    if (viewport_->current_mode() == VirtualViewport::ViewMode::GRID) {
        recompute_layout(false);
        reprioritize_pending_ = true;
        last_resize_time_ = std::chrono::steady_clock::now();
    } else {
        layout_dirty_ = true;
    }
}

// ── callbacks ──────────────────────────────────────────────────────────────

void MainWindow::scroll_cb(Fl_Widget* w, void* data) {
    MainWindow* win = static_cast<MainWindow*>(data);
    win->viewport_->set_scroll_offset(win->scrollbar_->value());
    if (win->viewport_->current_mode() == VirtualViewport::ViewMode::GRID) {
        win->reprioritize_thumbnails();
    }
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
        case 17: criteria = ImageStore::SortCriteria::PIXEL_AREA;  ascending = true;  break;
        case 18: criteria = ImageStore::SortCriteria::PIXEL_AREA;  ascending = false; break;
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
    
    if ((choice >= 1 && choice <= 6) || choice == 17 || choice == 18) {
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
