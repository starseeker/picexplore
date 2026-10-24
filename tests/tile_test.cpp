#include "gui/tile_manager.h"
#include "utils.h"
#include "stb_image_write.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace fs = std::filesystem;
int main() {
    char pattern[] = "/tmp/px-tiles-XXXXXX";
    const std::string root = mkdtemp(pattern), path = root + "/image.png";
    std::vector<uint8_t> rgb(520 * 530 * 3, 100);
    assert(stbi_write_png(path.c_str(), 520, 530, 3, rgb.data(), 520 * 3));
    std::string hash; assert(compute_file_hash(path, hash));
    moodycamel::ConcurrentQueue<UpdateEvent> queue;
    auto until = [&](auto check) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            UpdateEvent event;
            while (queue.try_dequeue(event)) assert(event.type != UpdateEvent::Type::FULL_RES_FAILED);
            if (check()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < deadline);
        assert(false && "tile load timed out");
    };
    {
        TileManager tiles(queue, 3);
        assert(tiles.max_cached_tiles() >= 512);
        tiles.init(root);
        tiles.request_tiles(5, path, hash, 1, 1);
        until([&] { return tiles.are_tiles_ready(hash); });
        assert(fs::exists(root + "/tiles/" + hash + "/v2/ready.marker"));
        auto first = tiles.get_tile(hash, 1, 1, 0);
        assert(!first); // A cache miss returns immediately and schedules I/O.
        until([&] { first = tiles.get_tile(hash, 1, 1, 0); return bool(first); });
        assert(first->width == 8 && first->height == 18 && first->rgb.size() == 8 * 18 * 3);
        assert(tiles.get_tile(hash, 1, 1, 0) == first); // cache hits share immutable pixels
        tiles.cancel();
    }
    // Incomplete on-disk caches are regenerated before readiness is announced.
    fs::remove(root + "/tiles/" + hash + "/v2/z0_1_1.jpg");
    {
        TileManager tiles(queue, 3);
        assert(tiles.max_cached_tiles() >= 512);
        tiles.init(root);
        tiles.request_tiles(5, path, hash, 1, 2);
        until([&] { return tiles.are_tiles_ready(hash); });
        assert(fs::exists(root + "/tiles/" + hash + "/v2/z0_1_1.jpg"));
    }
    // A failed destination must not leave a successful completion marker.
    {
        std::ofstream(root + "/blocked") << "not a directory";
        TileManager tiles(queue, 3);
        tiles.init(root + "/blocked");
        tiles.request_tiles(5, path, hash, 1, 3);
        bool failed = false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!failed && std::chrono::steady_clock::now() < deadline) {
            UpdateEvent event;
            while (queue.try_dequeue(event)) if (event.type == UpdateEvent::Type::FULL_RES_FAILED) failed = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(failed && !tiles.are_tiles_ready(hash));
    }
    fs::remove_all(root);
}
