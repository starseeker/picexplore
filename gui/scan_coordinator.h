#pragma once

#include <string>
#include <thread>
#include <atomic>
#include "../third_party/concurrentqueue/concurrentqueue.h"
#include "update_events.h"
#include "../database.h"

class ScanCoordinator {
public:
    ScanCoordinator(const std::string& directory,
                    moodycamel::ConcurrentQueue<UpdateEvent>& update_queue,
                    const std::string& db_path = "");
    ~ScanCoordinator();

    void start();
    void stop();

private:
    std::string directory_;
    std::string db_path_;
    moodycamel::ConcurrentQueue<UpdateEvent>& update_queue_;

    std::thread worker_thread_;
    std::atomic<bool> stop_requested_;

    void run();
};
