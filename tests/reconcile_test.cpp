#include "gui/scan_coordinator.h"
#include "gui/image_store.h"
#include "gui/thumbnail_pipeline.h"
#include <cassert>
#include <fstream>
#include <filesystem>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
static void write_jpeg(const std::string& path, int w, int h, uint8_t value) {
    auto jpeg = encode_jpeg(std::vector<uint8_t>(w * h * 3, value).data(), w, h);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<char*>(jpeg.data()), jpeg.size());
}
int main() {
    char pattern[] = "/tmp/px-reconcile-XXXXXX";
    const std::string base = mkdtemp(pattern), root = base + "/photos", file = root + "/a.jpg";
    fs::create_directories(root);
    write_jpeg(file, 100, 80, 15);
    moodycamel::ConcurrentQueue<UpdateEvent> queue;
    ImageStore catalog;
    ScanCoordinator scanner(root, queue, base + "/cache.db");
    auto scan = [&] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            UpdateEvent event;
            while (queue.try_dequeue(event)) {
                if (event.type == UpdateEvent::Type::IMAGE_DISCOVERED) {
                    const auto& i = event.image;
                    catalog.add_image(i.filepath, i.aspect_ratio, i.width, i.height,
                        i.file_size, i.file_timestamp, i.content_hash, i.stamp);
                }
                if (event.type == UpdateEvent::Type::IMAGE_DELETED) catalog.remove_image(event.deletion.filepath);
                if (event.type == UpdateEvent::Type::SCAN_COMPLETE) return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(false && "reconciliation timed out");
    };
    scanner.start(); scan();
    assert(catalog.size() == 1);
    const auto id = catalog.order().front();
    const auto original_revision = catalog.get(id).revision;
    const std::string old_hash = catalog.get(id).content_hash;
    catalog.set_thumbnail_rgb(id, file, ThumbQuality::SMALL, std::vector<uint8_t>(3, 42), 1, 1, 1);
    DatabaseManager db;
    assert(db.open(base + "/cache.db"));
    std::string hash;
    assert(db.get_valid_hash_for_path(file, hash) && hash == old_hash);
    auto timestamp = fs::last_write_time(file);
    write_jpeg(file, 20, 10, 225);
    fs::last_write_time(file, timestamp); // mtime alone cannot validate a file
    assert(!db.get_valid_hash_for_path(file, hash));
    scanner.request_rescan(); scan();
    assert(catalog.get(id).revision > original_revision);
    assert(catalog.get(id).original_width == 20 && catalog.get(id).original_height == 10);
    assert(catalog.get(id).scaled.rgb_data.empty());
    assert(catalog.get(id).content_hash != old_hash);
    assert(db.get_paths_for_hash(old_hash).empty());
    assert(db.get_valid_hash_for_path(file, hash) && hash == catalog.get(id).content_hash);
    // A stale thumbnail request cannot publish the replacement under the original hash.
    {
        ThumbnailPipeline pipeline(queue, base + "/cache.db");
        pipeline.start(1);
        pipeline.request_thumbnail(id, file, old_hash, ThumbQuality::SMALL, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pipeline.stop();
        UpdateEvent event;
        while (queue.try_dequeue(event)) assert(event.type != UpdateEvent::Type::THUMB_RGB_READY);
    }
    // Atomic replacement is detected even with the same mtime and content dimensions.
    const auto revision = catalog.get(id).revision;
    write_jpeg(root + "/temp", 20, 10, 55);
    fs::last_write_time(root + "/temp", timestamp);
    fs::rename(root + "/temp", file);
    scanner.request_rescan(); scan();
    assert(catalog.get(id).revision > revision);
    assert(db.get_paths_for_hash(hash).empty());
    scanner.stop();
    // Reopening validates on-disk bytes instead of trusting cached paths.
    write_jpeg(file, 7, 9, 22);
    ScanCoordinator reopened(root, queue, base + "/cache.db");
    reopened.start(); scan();
    assert(catalog.get(id).original_width == 7);
    assert(catalog.get(id).original_height == 9);
    // An unreadable subtree is an incomplete scan, not evidence that its files vanished.
    fs::permissions(root, fs::perms::none);
    reopened.request_rescan(); scan();
    assert(catalog.contains(id));
    assert(db.get_hash_for_path(file, hash));
    fs::permissions(root, fs::perms::owner_all);
    reopened.stop();
    assert(path_is_within("/tmp/dir/file.jpg", "/tmp/dir"));
    assert(!path_is_within("/tmp/dir-other/file.jpg", "/tmp/dir"));
    assert(path_is_within("/tmp/dir/file.jpg", "/tmp/dir/"));
    assert(!is_cache_or_db_path("/photos/picexplore"));
    assert(!is_cache_or_db_path("/tmp/picexplore-photos/a.jpg"));
    fs::remove_all(base);
}
