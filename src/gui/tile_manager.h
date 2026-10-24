#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include "update_events.h"

class TileManager {
public:
    struct Tile { std::vector<uint8_t> rgb; int width = 0, height = 0; };
    TileManager(moodycamel::ConcurrentQueue<UpdateEvent>& queue, uint64_t session = 0);
    ~TileManager();
    void init(const std::string& cache_dir);
    void request_tiles(size_t image_id, const std::string& filepath, const std::string& hash,
                       uint64_t revision = 1, uint64_t request = 0);
    void cancel();
    bool are_tiles_ready(const std::string& hash) const;
    // Cache lookup only. A miss schedules a bounded, deduplicated disk read.
    std::shared_ptr<const Tile> get_tile(const std::string& hash, int tx, int ty, int zoom_level);
    static constexpr int TILE_SIZE = 512;
private:
    friend struct TileTestAccess;
    struct Request { JobToken token; std::string path, hash; uint64_t sequence = 0; };
    struct Read { Request request; std::string key, path; };
    struct Cached { std::string key; std::shared_ptr<const Tile> tile; };
    void generate_worker();
    void read_worker();
    bool generate_tiles(const Request& request);
    bool current(const Request& request) const;
    void fail(const Request& request, const std::string& message);
    EventPublisher publisher_;
    std::string cache_dir_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> sequence_{0};
    std::thread generator_, reader_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    Request active_;
    bool pending_ = false;
    std::deque<Read> reads_;
    std::unordered_set<std::string> pending_reads_, failed_reads_, ready_;
    std::list<Cached> cache_;
    std::unordered_map<std::string, std::list<Cached>::iterator> cache_map_;
    size_t cache_bytes_ = 0;
    static constexpr size_t MAX_CACHE_BYTES = 384 * 1024 * 1024;
    static constexpr size_t MAX_PENDING_READS = 512;
};
