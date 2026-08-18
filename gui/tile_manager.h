#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <unordered_map>
#include <list>
#include <filesystem>
#include "update_events.h"
#include "../third_party/concurrentqueue/concurrentqueue.h"

class TileManager {
public:
    TileManager(moodycamel::ConcurrentQueue<UpdateEvent>& update_queue);
    ~TileManager();

    void init(const std::string& cache_dir);
    
    // Request background generation of tiles for an extreme image
    void request_tiles(size_t image_index, const std::string& filepath, const std::string& hash);
    
    // Check if tiles for an image are fully generated
    bool are_tiles_ready(const std::string& hash) const;
    
    // Get a specific tile (synchronous, loads from disk cache)
    std::vector<uint8_t> get_tile(const std::string& hash, int tx, int ty, int zoom_level, int& out_w, int& out_h);

    // Number of pixels per tile
    static const int TILE_SIZE = 512;

private:
    void worker_thread();
    bool generate_tiles(const std::string& filepath, const std::string& hash);

    moodycamel::ConcurrentQueue<UpdateEvent>& update_queue_;
    std::string cache_dir_;
    
    struct TileRequest {
        size_t image_index;
        std::string filepath;
        std::string hash;
    };
    
    struct CachedTile {
        std::string key;
        std::vector<uint8_t> rgb;
        int w;
        int h;
    };
    std::list<CachedTile> tile_cache_;
    std::unordered_map<std::string, std::list<CachedTile>::iterator> tile_cache_map_;
    const size_t MAX_CACHE_TILES = 100; // ~75 MB
    std::mutex cache_mutex_;
    
    moodycamel::ConcurrentQueue<TileRequest> request_queue_;
    std::atomic<bool> stop_requested_{false};
    std::thread worker_;
    
    std::mutex ready_mutex_;
    std::unordered_set<std::string> ready_hashes_;
};
