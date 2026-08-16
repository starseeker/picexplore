#include "scan_coordinator.h"
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
    worker_thread_ = std::thread(&ScanCoordinator::run, this);
}

void ScanCoordinator::stop() {
    stop_requested_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void ScanCoordinator::run() {
    DatabaseManager db;
    bool db_opened = db.open(db_path_);
    std::vector<ImageInfo> images;
    std::unordered_set<std::string> known_paths;
    
    if (db_opened) {
        images = db.get_all_images();
        if (!images.empty()) {
            std::cout << "Loading images from database: " << db_path_ << std::endl;
            int found = 0;
            for (const auto& img : images) {
                if (stop_requested_) break;
                
                if (!fs::exists(img.path)) {
                    std::cout << "File missing, removing from database: " << img.path << std::endl;
                    std::lock_guard<std::mutex> lock(db.get_mutex());
                    if (db.begin_transaction()) {
                        db.delete_key(img.hash + ":path");
                        db.delete_key("file:" + img.path);
                        db.commit_transaction();
                    }
                    continue;
                }
                
                known_paths.insert(img.path);
                
                ThumbQuality bq = static_cast<ThumbQuality>(img.best_thumb_size);
                
                uintmax_t fsize = 0, ftime = 0;
                try {
                    fsize = fs::file_size(img.path);
                    ftime = std::chrono::duration_cast<std::chrono::seconds>(
                                fs::last_write_time(img.path).time_since_epoch()).count();
                } catch (...) {}

                UpdateEvent ev = UpdateEvent::make_image_discovered(
                    img.path, img.hash,
                    img.thumb_width, img.thumb_height, img.aspect_ratio, 
                    fsize, ftime,
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

    // Always scan directory for new or renamed files
    int found = 0;
    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory_)) {
            if (stop_requested_) break;
            if (entry.is_regular_file()) {
                std::string path = entry.path().string();
                if (known_paths.find(path) != known_paths.end()) {
                    continue; // Already loaded from DB
                }
                
                auto ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    
                    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp") {
                        int w = 0, h = 0, comp = 0;
                        if (stbi_info(path.c_str(), &w, &h, &comp) && w > 0 && h > 0) {
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
                    }
                }
            }
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Filesystem error: " << e.what() << std::endl;
        }

    update_queue_.enqueue(UpdateEvent::make_scan_complete());
}
