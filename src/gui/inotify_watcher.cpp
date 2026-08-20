#include "inotify_watcher.h"
#include "utils.h"
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace fs = std::filesystem;

InotifyWatcher::InotifyWatcher() {}

InotifyWatcher::~InotifyWatcher() {
    stop();
}

void InotifyWatcher::start(const std::string& directory, moodycamel::ConcurrentQueue<UpdateEvent>& update_queue) {
    if (inotify_fd_ != -1) return;
    
    directory_ = directory;
    update_queue_ = &update_queue;
    stop_requested_ = false;
    
    inotify_fd_ = inotify_init1(IN_NONBLOCK);
    if (inotify_fd_ == -1) {
        std::cerr << "Failed to initialize inotify! Real-time watching disabled." << std::endl;
        return;
    }
    
    watch_thread_ = std::thread(&InotifyWatcher::watch_thread_func, this);
}

void InotifyWatcher::stop() {
    if (stop_requested_) return;
    stop_requested_ = true;
    if (watch_thread_.joinable()) {
        watch_thread_.join();
    }
    if (inotify_fd_ != -1) {
        close(inotify_fd_);
        inotify_fd_ = -1;
    }
    wd_to_path_.clear();
    pending_renames_.clear();
}

void InotifyWatcher::add_watch_recursive(const std::string& path) {
    if (stop_requested_ || is_cache_or_db_path(path)) return;

    int wd = inotify_add_watch(inotify_fd_, path.c_str(), 
        IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_CREATE);
    if (wd == -1) {
        // Silently skip unreadable directories
        return;
    }
    wd_to_path_[wd] = path;
    
    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            if (stop_requested_) break;
            if (entry.is_directory() && !fs::is_symlink(entry)) {
                std::string subpath = fs::path(entry.path()).lexically_normal().string();
                if (!is_cache_or_db_path(subpath)) {
                    add_watch_recursive(subpath);
                }
            }
        }
    } catch (...) {}
}

void InotifyWatcher::watch_thread_func() {
    add_watch_recursive(directory_);

    const size_t buf_size = 8192;
    char buffer[buf_size] __attribute__((aligned(__alignof__(struct inotify_event))));
    
    while (!stop_requested_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        ssize_t len = read(inotify_fd_, buffer, buf_size);
        if (len <= 0) {
            // Clean up old pending renames that didn't get an IN_MOVED_TO (moved outside)
            if (!pending_renames_.empty()) {
                for (const auto& pair : pending_renames_) {
                    update_queue_->enqueue(UpdateEvent::make_image_deleted(pair.second));
                }
                pending_renames_.clear();
            }
            continue;
        }
        
        const struct inotify_event* event;
        for (char* ptr = buffer; ptr < buffer + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event*)ptr;
            
            if (event->len == 0) continue;
            
            auto it = wd_to_path_.find(event->wd);
            if (it == wd_to_path_.end()) continue;
            
            std::string dir_path = it->second;
            std::string full_path = fs::path(dir_path + "/" + event->name).lexically_normal().string();
            
            if (is_cache_or_db_path(full_path)) continue;

            if (event->mask & IN_ISDIR) {
                if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                    add_watch_recursive(full_path);
                }
                continue;
            }
            
            if (!is_image_file(full_path)) continue;
            
            if (event->mask & IN_CLOSE_WRITE) {
                int w = 0, h = 0;
                if (get_image_info(full_path, &w, &h) && w > 0 && h > 0) {
                    double ar = static_cast<double>(w) / h;
                    uintmax_t fsize = 0, ftime = 0;
                    try {
                        fsize = fs::file_size(full_path);
                        ftime = std::chrono::duration_cast<std::chrono::seconds>(fs::last_write_time(full_path).time_since_epoch()).count();
                    } catch (...) {}
                    update_queue_->enqueue(UpdateEvent::make_image_discovered(full_path, "", w, h, ar, fsize, ftime, ThumbQuality::NONE));
                }
            } else if (event->mask & IN_DELETE) {
                update_queue_->enqueue(UpdateEvent::make_image_deleted(full_path));
            } else if (event->mask & IN_MOVED_FROM) {
                pending_renames_[event->cookie] = full_path;
            } else if (event->mask & IN_MOVED_TO) {
                auto rit = pending_renames_.find(event->cookie);
                if (rit != pending_renames_.end()) {
                    update_queue_->enqueue(UpdateEvent::make_image_renamed(rit->second, full_path));
                    pending_renames_.erase(rit);
                } else {
                    int w = 0, h = 0;
                    if (get_image_info(full_path, &w, &h) && w > 0 && h > 0) {
                        double ar = static_cast<double>(w) / h;
                        update_queue_->enqueue(UpdateEvent::make_image_discovered(full_path, "", w, h, ar));
                    }
                }
            }
        }
    }
}
