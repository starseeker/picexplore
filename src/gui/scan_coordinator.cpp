#include "scan_coordinator.h"
#include "utils.h"
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <unordered_set>
#include "../third_party/stb/stb_image.h"

namespace fs = std::filesystem;

ScanCoordinator::ScanCoordinator(const std::string& directory,
                                 moodycamel::ConcurrentQueue<UpdateEvent>& update_queue,
                                 const std::string& db_path)
    : directory_(directory), db_path_(db_path), update_queue_(update_queue), stop_requested_(false) {
    if (db_path_.empty()) {
        db_path_ = (fs::path(directory_) / "images.db").string();
    }
}

ScanCoordinator::~ScanCoordinator() {
    stop();
}

void ScanCoordinator::start() {
    stop_requested_ = false;
    workers_.emplace_back(&ScanCoordinator::run, this);
    
    int num_workers = std::thread::hardware_concurrency();
    if (num_workers < 2) num_workers = 2;
    active_workers_ = num_workers;
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&ScanCoordinator::scan_worker, this);
    }
}

void ScanCoordinator::stop() {
    stop_requested_ = true;
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

void ScanCoordinator::run() {
    DatabaseManager db;
    bool db_opened = db.open(db_path_);
    std::vector<ImageInfo> images;
    std::unordered_set<std::string> db_paths;
    
    if (db_opened) {
        images = db.get_all_images();
        if (!images.empty()) {
            std::cout << "Loading images from database: " << db_path_ << std::endl;
            
            // Normalize directory string for safe prefix matching
            std::string prefix = fs::path(directory_).lexically_normal().string();
            if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\') {
                prefix += '/';
            }
            
            int found = 0;
            for (const auto& img : images) {
                if (stop_requested_) break;
                
                std::string norm_path = fs::path(img.path).lexically_normal().string();
                // Only load images that belong to the currently requested directory
                if (norm_path.find(prefix) != 0 && norm_path != fs::path(directory_).lexically_normal().string()) {
                    continue;
                }
                
                db_paths.insert(norm_path);
                
                ThumbQuality bq = static_cast<ThumbQuality>(img.best_thumb_size);
                
                int actual_w = (img.orig_width > 0) ? img.orig_width : img.thumb_width;
                int actual_h = (img.orig_height > 0) ? img.orig_height : img.thumb_height;
                double ar = img.aspect_ratio;

                UpdateEvent ev = UpdateEvent::make_image_discovered(
                    norm_path, img.hash,
                    actual_w, actual_h, ar, 
                    img.file_size, img.file_timestamp,
                    bq, {}, // Skip passing jpeg data to save memory, ThumbnailPipeline will load it
                    img.thumb_width, img.thumb_height
                );
                
                update_queue_.enqueue(std::move(ev));
                
                found++;
                if (found % 100 == 0) {
                    update_queue_.enqueue(UpdateEvent::make_scan_progress(found));
                }
            }
            std::cout << "Loaded " << found << " images from database." << std::endl;
        }
    }

    // Now scan directory for current state
    std::unordered_set<std::string> disk_paths;
    try {
        fs::recursive_directory_iterator it(directory_), end;
        while (it != end && !stop_requested_) {
            const auto& entry = *it;
            std::string path = fs::path(entry.path()).lexically_normal().string();

            if (entry.is_directory()) {
                if (is_cache_or_db_path(path, db_path_)) {
                    it.disable_recursion_pending();
                }
            } else if (entry.is_regular_file()) {
                if (!is_cache_or_db_path(path, db_path_) && is_image_file(path)) {
                    disk_paths.insert(path);
                    if (db_paths.find(path) == db_paths.end()) {
                        // New file discovered!
                        file_queue_.enqueue(path);
                    }
                }
            }
            ++it;
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error: " << e.what() << std::endl;
    }

    // Reconcile removed files (in DB from last time, but no longer on disk)
    if (db_opened) {
        for (const auto& db_path : db_paths) {
            if (stop_requested_) break;
            if (disk_paths.find(db_path) == disk_paths.end()) {
                std::cout << "File removed from disk, updating database and store: " << db_path << std::endl;
                
                std::lock_guard<std::mutex> lock(db.get_mutex());
                if (db.begin_transaction()) {
                    std::string hash;
                    if (db.get_hash_for_path(db_path, hash)) {
                        db.remove_path_for_hash(hash, db_path);
                    }
                    db.delete_key("file:" + db_path);
                    db.commit_transaction();
                }
                
                update_queue_.enqueue(UpdateEvent::make_image_deleted(db_path));
            }
        }
    }

    // End of traversal
    traversal_done_ = true;
}

void ScanCoordinator::scan_worker() {
    int found = 0;
    while (!stop_requested_) {
        std::string path;
        if (file_queue_.try_dequeue(path)) {
            int w = 0, h = 0;
            if (get_image_info(path, &w, &h) && w > 0 && h > 0) {
                double ar = static_cast<double>(w) / h;
                uintmax_t fsize = 0, ftime = 0;
                try {
                    fsize = fs::file_size(path);
                    ftime = std::chrono::duration_cast<std::chrono::seconds>(
                                fs::last_write_time(path).time_since_epoch()).count();
                } catch (...) {}
                UpdateEvent ev = UpdateEvent::make_image_discovered(
                    path, "", w, h, ar, fsize, ftime, ThumbQuality::NONE
                );
                update_queue_.enqueue(std::move(ev));
                
                found++;
                if (found % 100 == 0) {
                    update_queue_.enqueue(UpdateEvent::make_scan_progress(found));
                }
            }
        } else {
            if (traversal_done_ && file_queue_.size_approx() == 0) {
                break; // Traversal done and queue empty
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    
    // Only the last worker to exit sends the complete event
    if (--active_workers_ == 0) {
        update_queue_.enqueue(UpdateEvent::make_scan_complete());
    }
}
