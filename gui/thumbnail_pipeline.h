#pragma once

#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>
#include "../third_party/concurrentqueue/concurrentqueue.h"
#include "update_events.h"
#include "../database.h"

struct ThumbRequest {
    size_t image_index;
    std::string filepath;
    ThumbQuality target_quality;
    std::string hash;
    uint64_t generation;
};

class ThumbnailPipeline {
public:
    ThumbnailPipeline(moodycamel::ConcurrentQueue<UpdateEvent>& update_queue,
                      const std::string& db_path = "");
    ~ThumbnailPipeline();

    void start(int num_workers = 4);
    void stop();

    void request_thumbnail(size_t image_index, const std::string& filepath, const std::string& hash, ThumbQuality quality, bool urgent, uint64_t generation = 0);
    
    void set_generation(uint64_t gen);
    uint64_t get_generation() const;

private:
    moodycamel::ConcurrentQueue<UpdateEvent>& update_queue_;
    std::string db_path_;
    
    DatabaseManager db_;

    std::vector<std::thread> workers_;
    std::atomic<bool> stop_requested_;

    // Queues
    moodycamel::ConcurrentQueue<ThumbRequest> urgent_queue_;
    moodycamel::ConcurrentQueue<ThumbRequest> normal_queue_;

    std::mutex wake_mutex_;
    std::condition_variable wake_cv_;
    
    std::atomic<uint64_t> current_generation_{0};
    std::atomic<int> pending_requests_{0};

    void worker_thread();
    bool process_request(const ThumbRequest& req);
};
