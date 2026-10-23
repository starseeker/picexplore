#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "update_events.h"
#include "../database.h"

// The sole producer of catalog changes. File notifications request reconciliation;
// they never directly mutate the catalog or durable path records.
class ScanCoordinator {
public:
    ScanCoordinator(const std::string& directory,
                    moodycamel::ConcurrentQueue<UpdateEvent>& update_queue,
                    const std::string& db_path = "", uint64_t session = 0);
    ~ScanCoordinator();
    // Seed the UI's retained paths before restarting a session, including paths
    // that disappeared while the old session was stopping.
    void track_existing(const std::string& path) { if (!worker_.joinable()) known_.emplace(path, Record{}); }
    void set_recursive(bool recursive) { if (!worker_.joinable()) recursive_ = recursive; }
    void set_explicit_files(std::vector<std::string> files) {
        if (!worker_.joinable()) {
            has_explicit_files_ = true;
            explicit_files_ = std::move(files);
        }
    }
    void start();
    void stop();
    void request_rescan();
    void request_rename(const std::string& from, const std::string& to);

private:
    struct Record {
        FileStamp stamp;
        std::string hash;
        int width = 0, height = 0;
    };
    std::string directory_, db_path_;
    EventPublisher update_queue_;
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::mutex mutex_;
    std::condition_variable wake_;
    bool dirty_ = false;
    bool recursive_ = true;
    bool has_explicit_files_ = false;
    std::vector<std::string> explicit_files_;
    std::vector<std::pair<std::string, std::string>> renames_;
    std::unordered_map<std::string, Record> known_;
    void run();
    bool reconcile(DatabaseManager& db);
    void rename(DatabaseManager& db, const std::string& from, const std::string& to);
    void error(const std::string& message);
};
