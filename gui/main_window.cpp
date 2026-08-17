#include "main_window.h"
#include "inotify_watcher.h"
#include "../database.h"
#include <FL/Fl.H>

#include <FL/Fl_Output.H>
#include <FL/Fl_Menu_Bar.H>

MainWindow::MainWindow(int w, int h, const char* title, const std::string& directory)
    : Fl_Double_Window(w, h, title), directory_(directory) {
    
    int scroll_w = 20;
    int menu_h = 25;
    int info_h = 25;
    int vp_h = h - menu_h - info_h;

    menubar_ = new Fl_Menu_Bar(0, 0, w, menu_h);
    menubar_->add("Sort/Alphabetical (A-Z)", 0, menu_cb, (void*)1);
    menubar_->add("Sort/Alphabetical (Z-A)", 0, menu_cb, (void*)2);
    menubar_->add("Sort/File Size (Smallest)", 0, menu_cb, (void*)3);
    menubar_->add("Sort/File Size (Largest)", 0, menu_cb, (void*)4);
    menubar_->add("Sort/Date (Oldest)", 0, menu_cb, (void*)5);
    menubar_->add("Sort/Date (Newest)", 0, menu_cb, (void*)6);
    
    menubar_->add("View/Zoom In (Ctrl+Wheel Up)", FL_CTRL | '=', menu_cb, (void*)7);
    menubar_->add("View/Zoom Out (Ctrl+Wheel Down)", FL_CTRL | '-', menu_cb, (void*)8);
    menubar_->add("View/Reset Zoom", FL_CTRL | '0', menu_cb, (void*)9);

    viewport_ = new VirtualViewport(0, menu_h, w - scroll_w, vp_h, store_);
    scrollbar_ = new Fl_Scrollbar(w - scroll_w, menu_h, scroll_w, vp_h);
    scrollbar_->type(FL_VERTICAL);
    scrollbar_->callback(scroll_cb, this);

    info_bar_ = new Fl_Output(0, h - info_h, w, info_h);
    info_bar_->box(FL_FLAT_BOX);
    info_bar_->color(FL_LIGHT2);
    info_bar_->textsize(12);

    viewport_->on_image_clicked = [this](const std::string& path) {
        info_bar_->value(path.c_str());
    };

    end();
    resizable(viewport_);
}

MainWindow::~MainWindow() {
    if (scanner_) {
        scanner_->stop();
        delete scanner_;
    }
    if (pipeline_) {
        pipeline_->stop();
        delete pipeline_;
    }
    if (watcher_) {
        watcher_->stop();
        delete watcher_;
    }
    if (db_) {
        delete db_;
    }
    Fl::remove_timeout(timer_cb, this);
}

void MainWindow::start() {
    std::string db_path = "./images.db";
    
    db_ = new DatabaseManager();
    db_->open(db_path);

    pipeline_ = new ThumbnailPipeline(update_queue_, db_path);
    pipeline_->start(4);

    scanner_ = new ScanCoordinator(directory_, update_queue_, db_path);
    scanner_->start();
    
    watcher_ = new InotifyWatcher();
    watcher_->start(directory_, update_queue_);

    Fl::add_timeout(0.016, timer_cb, this); 
}

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
            store_.add_image(ev.image.filepath, ev.image.aspect_ratio, ev.image.width, ev.image.height, ev.image.file_size, ev.image.file_timestamp);
            layout_dirty_ = true;
        } else if (ev.type == UpdateEvent::Type::THUMB_READY) {
            store_.set_thumbnail(ev.thumb.image_index, ev.thumb.filepath, ev.thumb.quality,
                                 ev.thumb.jpeg_data.data(), ev.thumb.jpeg_data.size(),
                                 ev.thumb.width, ev.thumb.height);
            changed.push_back(ev.thumb.image_index);
            need_redraw = true;
        } else if (ev.type == UpdateEvent::Type::THUMB_RGB_READY) {
            store_.set_thumbnail_rgb(ev.thumb_rgb.image_index, ev.thumb_rgb.filepath, ev.thumb_rgb.quality,
                                     std::move(ev.thumb_rgb.rgb_data), ev.thumb_rgb.width, ev.thumb_rgb.height);
            changed.push_back(ev.thumb_rgb.image_index);
            need_redraw = true;
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
            std::cout << "RENAMED: " << ev.rename.old_filepath << " -> " << ev.rename.new_filepath << std::endl;
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
        }
    }

    if (layout_dirty_) {
        layout_result_ = layout_engine_.compute(store_.get_aspect_ratios(), viewport_->content_width(), target_height_);
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
    
    current_generation_++;
    pipeline_->set_generation(current_generation_);
    
    for (size_t idx : visible) {
        const auto& entry = store_.get(idx);
        
        double layout_w = entry.aspect_ratio * target_height_;
        double layout_h = target_height_;
        if (idx < layout_result_.boxes.size()) {
            layout_w = layout_result_.boxes[idx].w;
            layout_h = layout_result_.boxes[idx].h;
        }
        
        ThumbQuality needed = ThumbQuality::SMALL;
        if (layout_w < 96) needed = ThumbQuality::SMALL;
        else if (layout_w < 192) needed = ThumbQuality::MEDIUM;
        else if (layout_w < 384) needed = ThumbQuality::LARGE;
        else if (layout_w < 768) needed = ThumbQuality::XLARGE;
        else needed = ThumbQuality::FULL;
        
        bool size_mismatch = (entry.scaled.layout_width != static_cast<int>(layout_w) || 
                              entry.scaled.layout_height != static_cast<int>(layout_h));
        
        if (entry.best_quality == ThumbQuality::NONE) {
            pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash, ThumbQuality::SMALL, true, current_generation_, static_cast<int>(layout_w), static_cast<int>(layout_h));
            if (needed > ThumbQuality::SMALL) {
                pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash, needed, false, current_generation_, static_cast<int>(layout_w), static_cast<int>(layout_h));
            }
        } else if (entry.best_quality < needed || size_mismatch) {
            ThumbQuality req_quality = std::max(needed, entry.best_quality);
            pipeline_->request_thumbnail(idx, entry.filepath, entry.content_hash, req_quality, false, current_generation_, static_cast<int>(layout_w), static_cast<int>(layout_h));
        }
    }
}

void MainWindow::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);
    int scroll_w = scrollbar_->w();
    int menu_h = menubar_->h();
    int info_h = info_bar_->h();
    int vp_h = H - menu_h - info_h;

    menubar_->resize(0, 0, W, menu_h);
    viewport_->resize(0, menu_h, W - scroll_w, vp_h);
    scrollbar_->resize(W - scroll_w, menu_h, scroll_w, vp_h);
    info_bar_->resize(0, H - info_h, W, info_h);
    
    layout_dirty_ = true;
}

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
        case 1: criteria = ImageStore::SortCriteria::ALPHABETICAL; ascending = true; break;
        case 2: criteria = ImageStore::SortCriteria::ALPHABETICAL; ascending = false; break;
        case 3: criteria = ImageStore::SortCriteria::FILE_SIZE; ascending = true; break;
        case 4: criteria = ImageStore::SortCriteria::FILE_SIZE; ascending = false; break;
        case 5: criteria = ImageStore::SortCriteria::TIMESTAMP; ascending = true; break;
        case 6: criteria = ImageStore::SortCriteria::TIMESTAMP; ascending = false; break;
        case 7: win->target_height_ = std::min(win->target_height_ * 1.2, 800.0); win->layout_dirty_ = true; break;
        case 8: win->target_height_ = std::max(win->target_height_ / 1.2, 50.0); win->layout_dirty_ = true; break;
        case 9: win->target_height_ = 150.0; win->layout_dirty_ = true; break;
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
            if (new_val > scrollbar_->maximum() - viewport_->h()) new_val = scrollbar_->maximum() - viewport_->h();
            if (new_val < 0) new_val = 0;
            scrollbar_->value(new_val);
            scroll_cb(scrollbar_, this);
            return 1;
        }
    }
    return Fl_Double_Window::handle(event);
}
