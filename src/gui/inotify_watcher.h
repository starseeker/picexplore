#pragma once

#include "file_watcher.h"
#include <thread>
#include <atomic>
#include <unordered_map>
#include <string>

class InotifyWatcher : public FileWatcher {
public:
    InotifyWatcher();
    ~InotifyWatcher() override;

    void start(const std::string& directory, moodycamel::ConcurrentQueue<UpdateEvent>& update_queue) override;
    void stop() override;

private:
    void watch_thread_func();
    void add_watch_recursive(const std::string& path);

    std::string directory_;
    moodycamel::ConcurrentQueue<UpdateEvent>* update_queue_ = nullptr;
    
    int inotify_fd_ = -1;
    std::atomic<bool> stop_requested_{false};
    std::thread watch_thread_;

    // Maps watch descriptors (wd) to absolute directory paths
    std::unordered_map<int, std::string> wd_to_path_;
    
    // Maps inotify cookies to pending rename "from" paths
    std::unordered_map<uint32_t, std::string> pending_renames_;
};
