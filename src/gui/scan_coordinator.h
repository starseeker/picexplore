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
    std::vector<std::pair<std::string, std::string>> renames_;
    std::unordered_map<std::string, Record> known_;
    void run();
    bool reconcile(DatabaseManager& db);
    void rename(DatabaseManager& db, const std::string& from, const std::string& to);
    void error(const std::string& message);
};
