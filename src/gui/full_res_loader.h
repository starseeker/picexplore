#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "../third_party/concurrentqueue/concurrentqueue.h"
#include "update_events.h"

class FullResLoader {
public:
    FullResLoader(moodycamel::ConcurrentQueue<UpdateEvent>& update_queue, uint64_t session = 0);
    ~FullResLoader();

    void request(size_t image_index, const std::string& filepath, uint64_t revision = 1, uint64_t request = 0);
    void cancel();

private:
    EventPublisher update_queue_;
    
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    
    std::mutex mutex_;
    std::condition_variable cv_;
    
    bool has_request_{false};
    size_t pending_index_{0};
    std::string pending_filepath_;
    JobToken pending_token_;
    uint64_t cancellation_ = 0;

    void worker_thread();
};
