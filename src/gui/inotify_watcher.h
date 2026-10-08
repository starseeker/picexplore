#pragma once

#include "file_watcher.h"
#include <thread>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <sys/inotify.h>

class InotifyWatcher : public FileWatcher {
public:
    InotifyWatcher() = default;
    ~InotifyWatcher() override;
    void start(const std::string& directory, moodycamel::ConcurrentQueue<UpdateEvent>& queue, uint64_t session = 0) override;
    void stop() override;
private:
    friend struct WatcherTestAccess;
    struct PendingRename {
        std::string path;
        bool directory;
        std::chrono::steady_clock::time_point time;
    };
    void watch_thread_func();
    void add_watch_recursive(const std::string& path);
    void handle_event(const inotify_event& event);
    void expire_renames();
    void remove_subtree(const std::string& path);
    void changed();
    void degraded(const std::string& reason);
    std::string directory_;
    EventPublisher update_queue_;
    int inotify_fd_ = -1;
    std::atomic<bool> stop_requested_{false};
    std::thread watch_thread_;
    bool degraded_ = false;
    bool rebuild_ = false;
    bool changed_ = false;
    std::unordered_map<int, std::string> wd_to_path_;
    std::unordered_map<uint32_t, PendingRename> pending_renames_;
};
