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
    FullResLoader(moodycamel::ConcurrentQueue<UpdateEvent>& update_queue);
    ~FullResLoader();

    void request(size_t image_index, const std::string& filepath);
    void cancel();

private:
    moodycamel::ConcurrentQueue<UpdateEvent>& update_queue_;
    
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    
    std::mutex mutex_;
    std::condition_variable cv_;
    
    bool has_request_{false};
    size_t pending_index_{0};
    std::string pending_filepath_;

    void worker_thread();
};
