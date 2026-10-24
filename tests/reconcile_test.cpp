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
    auto populate = [&] {
        std::vector<size_t> need_hash;
        for (auto id : catalog.order()) {
            if (catalog.get(id).content_hash.empty() && catalog.get(id).original_width > 0) {
                need_hash.push_back(id);
            }
        }
        if (need_hash.empty()) return;
        ThumbnailPipeline pipeline(queue, base + "/cache.db");
        pipeline.start(1);
        for (auto id : need_hash) {
            const auto& entry = catalog.get(id);
            pipeline.request_thumbnail(id, entry.filepath, "", ThumbQuality::SMALL, true, 1, 0, 0, entry.revision);
        }
        size_t done = 0;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (done < need_hash.size() && std::chrono::steady_clock::now() < deadline) {
            UpdateEvent event;
            while (queue.try_dequeue(event)) {
                if (event.type == UpdateEvent::Type::THUMB_RGB_READY) {
                    auto& entry = catalog.get(event.thumb_rgb.image_index);
                    if (entry.content_hash.empty()) entry.content_hash = event.thumb_rgb.content_hash;
                    catalog.set_thumbnail_rgb(event.thumb_rgb.image_index, event.thumb_rgb.filepath,
                        event.thumb_rgb.quality, std::move(event.thumb_rgb.rgb_data),
                        event.thumb_rgb.width, event.thumb_rgb.height, event.thumb_rgb.generation);
                    ++done;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        pipeline.stop();
        assert(done == need_hash.size());
    };
    auto scan = [&](bool auto_populate = true) {
        bool scan_done = false;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!scan_done && std::chrono::steady_clock::now() < deadline) {
            UpdateEvent event;
            while (queue.try_dequeue(event)) {
                if (event.type == UpdateEvent::Type::IMAGE_DISCOVERED) {
                    const auto& i = event.image;
                    catalog.add_image(i.filepath, i.aspect_ratio, i.width, i.height,
                        i.file_size, i.file_timestamp, i.content_hash, i.stamp);
                }
                if (event.type == UpdateEvent::Type::IMAGE_DELETED) catalog.remove_image(event.deletion.filepath);
                if (event.type == UpdateEvent::Type::SCAN_COMPLETE) { scan_done = true; break; }
            }
            if (!scan_done) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(scan_done && "reconciliation timed out");
        if (auto_populate) populate();
    };
    scanner.start(); scan(false);
    assert(catalog.size() == 1);
    const auto id = catalog.order().front();
    // Placeholder has dimensions immediately before full-file hashing/thumbnailing runs.
    assert(catalog.get(id).original_width == 100 && catalog.get(id).original_height == 80);
    assert(catalog.get(id).content_hash.empty());
    const auto original_revision = catalog.get(id).revision;
    populate();
    assert(catalog.get(id).revision == original_revision);
    const std::string old_hash = catalog.get(id).content_hash;
    assert(!old_hash.empty());
    DatabaseManager db;
    assert(db.open(base + "/cache.db"));
    std::string hash;
    assert(db.get_valid_hash_for_path(file, hash) && hash == old_hash);
    auto timestamp = fs::last_write_time(file);
    write_jpeg(file, 20, 10, 225);
    fs::last_write_time(file, timestamp); // mtime alone cannot validate a file
    assert(!db.get_valid_hash_for_path(file, hash));
    scanner.request_rescan(); scan(false);
    assert(catalog.get(id).revision > original_revision);
    assert(catalog.get(id).original_width == 20 && catalog.get(id).original_height == 10);
    assert(catalog.get(id).scaled.rgb_data.empty());
    assert(db.get_paths_for_hash(old_hash).empty());
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
    populate();
    assert(catalog.get(id).content_hash != old_hash);
    assert(db.get_valid_hash_for_path(file, hash) && hash == catalog.get(id).content_hash);
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

    // Verify non-recursive and explicit file/glob modes in resolve_input_targets and ScanCoordinator.
    const std::string sub = root + "/sub";
    fs::create_directories(sub);
    const std::string top_b = root + "/b.jpg";
    const std::string nested_c = sub + "/c.jpg";
    write_jpeg(top_b, 16, 16, 80);
    write_jpeg(nested_c, 24, 24, 120);

    // 1. Recursive scan populates DB with a.jpg, b.jpg, and sub/c.jpg
    catalog.clear();
    {
        ScanCoordinator full_scan(root, queue, base + "/cache.db");
        full_scan.start();
        scan();
        full_scan.stop();
    }
    assert(catalog.size() == 3);
    std::string nested_hash;
    assert(db.get_hash_for_path(nested_c, nested_hash) && !nested_hash.empty());

    // 2. Non-recursive scan discovers only top-level images and preserves sub/c.jpg in DB
    catalog.clear();
    {
        InputSelection non_rec = resolve_input_targets({root}, true);
        assert(non_rec.error.empty() && !non_rec.recursive && !non_rec.explicit_selection);
        ScanCoordinator flat_scan(non_rec.root_directory, queue, base + "/cache.db");
        flat_scan.set_recursive(false);
        flat_scan.start();
        scan();
        flat_scan.stop();
    }
    assert(catalog.size() == 2);
    assert(catalog.find_by_filepath(file) != static_cast<size_t>(-1));
    assert(catalog.find_by_filepath(top_b) != static_cast<size_t>(-1));
    assert(catalog.find_by_filepath(nested_c) == static_cast<size_t>(-1));
    assert(db.get_hash_for_path(nested_c, hash) && hash == nested_hash);

    // 3. Explicit glob / file selection discovers only matched files and preserves other DB records
    catalog.clear();
    {
        InputSelection glob_sel = resolve_input_targets({root + "/b*.jpg", sub + "/*.jpg"}, false);
        assert(glob_sel.error.empty() && glob_sel.explicit_selection);
        assert(glob_sel.files.size() == 2);
        assert(glob_sel.root_directory == normalize_path(root));
        ScanCoordinator explicit_scan(glob_sel.root_directory, queue, base + "/cache.db");
        explicit_scan.set_explicit_files(glob_sel.files);
        explicit_scan.start();
        scan();
        explicit_scan.stop();
    }
    assert(catalog.size() == 2);
    assert(catalog.find_by_filepath(file) == static_cast<size_t>(-1));
    assert(catalog.find_by_filepath(top_b) != static_cast<size_t>(-1));
    assert(catalog.find_by_filepath(nested_c) != static_cast<size_t>(-1));
    assert(db.get_hash_for_path(file, hash));

    // 4. Recursive ** glob pattern matches across root and subdirectories
    {
        InputSelection dstar = resolve_input_targets({root + "/**/*.jpg"}, false);
        assert(dstar.error.empty() && dstar.explicit_selection);
        assert(dstar.files.size() == 3);
    }

    fs::remove_all(base);
}
