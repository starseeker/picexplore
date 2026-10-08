#include "gui/scan_coordinator.h"
#include "gui/inotify_watcher.h"
#include "gui/image_store.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
struct WatcherTestAccess {
    static void overflow(moodycamel::ConcurrentQueue<UpdateEvent>& queue) {
        // Exercise the zero-length overflow branch without filling the kernel queue.
        InotifyWatcher watcher;
        watcher.update_queue_ = EventPublisher(queue);
        inotify_event event{};
        event.mask = IN_Q_OVERFLOW;
        watcher.handle_event(event);
        assert(watcher.rebuild_ && watcher.changed_ && watcher.degraded_);
    }
};
static void write_image(const std::string& path, int w = 16) {
    auto jpeg = encode_jpeg(std::vector<uint8_t>(w * 8 * 3, 100).data(), w, 8);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<char*>(jpeg.data()), jpeg.size());
}
int main() {
    char pattern[] = "/tmp/px-watcher-XXXXXX";
    const std::string base = mkdtemp(pattern), root = base + "/photos";
    fs::create_directories(root);
    moodycamel::ConcurrentQueue<UpdateEvent> queue;
    ScanCoordinator scanner(root, queue, base + "/cache.db");
    InotifyWatcher watcher;
    ImageStore catalog;
    watcher.start(root, queue); scanner.start();
    int scans = 0;
    auto pump = [&] {
        UpdateEvent event;
        while (queue.try_dequeue(event)) {
            switch (event.type) {
            case UpdateEvent::Type::FILESYSTEM_CHANGED: scanner.request_rescan(); break;
            case UpdateEvent::Type::FILESYSTEM_RENAMED:
                scanner.request_rename(event.rename.old_filepath, event.rename.new_filepath); break;
            case UpdateEvent::Type::IMAGE_DISCOVERED: {
                const auto& i = event.image;
                catalog.add_image(i.filepath, i.aspect_ratio, i.width, i.height, i.file_size,
                    i.file_timestamp, i.content_hash, i.stamp); break;
            }
            case UpdateEvent::Type::IMAGE_RENAMED:
                catalog.rename_image(event.rename.old_filepath, event.rename.new_filepath); break;
            case UpdateEvent::Type::IMAGE_DELETED: catalog.remove_image(event.deletion.filepath); break;
            case UpdateEvent::Type::SCAN_COMPLETE: ++scans; break;
            default: break;
            }
        }
    };
    auto until = [&](auto condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            pump();
            if (condition()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        assert(false && "watcher reconciliation timed out");
    };
    until([&]{ return scans >= 2; });
    write_image(root + "/a.jpg");
    until([&]{ return catalog.size() == 1; });
    const auto a = catalog.find_by_filepath(root + "/a.jpg");
    const auto revision = catalog.get(a).revision;
    write_image(root + "/save.tmp", 24);
    fs::rename(root + "/save.tmp", root + "/a.jpg");
    until([&]{ return catalog.get(a).original_width == 24; });
    assert(catalog.get(a).revision > revision);
    fs::create_hard_link(root + "/a.jpg", root + "/link.jpg");
    until([&]{ return catalog.size() == 2; });
    fs::create_directory(base + "/incoming");
    write_image(base + "/incoming/child.jpg");
    fs::rename(base + "/incoming", root + "/album");
    until([&]{ return catalog.size() == 3; });
    const auto child = catalog.find_by_filepath(root + "/album/child.jpg");
    fs::rename(root + "/album", root + "/renamed");
    until([&]{ return catalog.find_by_filepath(root + "/renamed/child.jpg") == child; });
    write_image(root + "/renamed/child.jpg", 33);
    until([&]{ return catalog.get(child).original_width == 33; });
    fs::rename(root + "/renamed", base + "/outside");
    // Continuous unrelated traffic must not prevent the moved-out cookie expiring.
    for (int n = 0; n < 40; ++n) {
        std::ofstream(root + "/activity.txt") << n;
        pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    until([&]{ return catalog.size() == 2; });
    fs::rename(root + "/link.jpg", root + "/link.txt");
    until([&]{ return catalog.size() == 1; });
    fs::rename(root + "/link.txt", root + "/back.jpg");
    until([&]{ return catalog.size() == 2; });
    fs::remove(root + "/a.jpg");
    until([&]{ return catalog.size() == 1; });
    assert(!catalog.contains(a));
    watcher.stop(); scanner.stop();
    WatcherTestAccess::overflow(queue);
    fs::remove_all(base);
}
