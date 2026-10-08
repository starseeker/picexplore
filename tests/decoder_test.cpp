#include "utils.h"
#include "gui/full_res_loader.h"
#include "gui/thumbnail_pipeline.h"
#include "stb_image_write.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace fs = std::filesystem;
static void write(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
int main() {
    char pattern[] = "/tmp/px-decoder-XXXXXX";
    const std::string root = mkdtemp(pattern);
    const std::vector<std::vector<int>> expected = {
        {0,1,2,3,4,5}, {1,0,3,2,5,4}, {5,4,3,2,1,0}, {4,5,2,3,0,1},
        {0,2,4,1,3,5}, {4,2,0,5,3,1}, {5,3,1,4,2,0}, {1,3,5,0,2,4}
    };
    for (int o = 1; o <= 8; ++o) {
        std::vector<uint8_t> rgb;
        for (int i = 0; i < 6; ++i) for (int c = 0; c < 3; ++c) rgb.push_back(i);
        int w = 2, h = 3;
        orient_rgb(rgb, w, h, o);
        assert(w == (o >= 5 ? 3 : 2) && h == (o >= 5 ? 2 : 3));
        for (int i = 0; i < 6; ++i) assert(rgb[i * 3] == expected[o - 1][i]);
    }
    const auto jpeg = encode_jpeg(std::vector<uint8_t>(30 * 20 * 3, 100).data(), 30, 20);
    // Minimal EXIF APP1 with orientation=6. Identify and pixels must agree.
    const std::vector<uint8_t> exif = {0xff,0xe1,0,34,'E','x','i','f',0,0,'I','I',42,0,8,0,0,0,
        1,0,0x12,1,3,0,1,0,0,0,6,0,0,0,0,0,0,0};
    auto rotated = jpeg;
    rotated.insert(rotated.begin() + 2, exif.begin(), exif.end());
    write(root + "/rotated.png", rotated); // Content identification, regardless of extension.
    const auto probe = probe_image(root + "/rotated.png");
    assert(probe && probe.format == ImageFormat::JPEG && probe.orientation == 6);
    assert(probe.width == 20 && probe.height == 30);
    auto decoded = decode_image(root + "/rotated.png");
    assert(decoded && decoded.width == 20 && decoded.height == 30);
    decoded = decode_image(root + "/rotated.png", 10, 10);
    assert(decoded && decoded.width <= 10 && decoded.height <= 10 && decoded.height > decoded.width);
    std::vector<uint8_t> png(517 * 513 * 3, 73);
    assert(stbi_write_png((root + "/pixels.jpg").c_str(), 517, 513, 3, png.data(), 517 * 3));
    assert(probe_image(root + "/pixels.jpg").supports_tiles);
    decoded = decode_image(root + "/pixels.jpg", 31, 31);
    assert(decoded && decoded.width <= 31 && decoded.height <= 31);
    for (auto value : decoded.rgb) assert(value == 73);
    fs::copy(root + "/pixels.jpg", root + "/broken.png");
    fs::resize_file(root + "/broken.png", 50);
    assert(!decode_image(root + "/broken.png", 20, 20));
    write(root + "/bad.jpg", {0xff,0xd8,0,1,2});
    assert(!probe_image(root + "/bad.jpg"));
    std::atomic<bool> cancelled{true};
    assert(decode_image(root + "/pixels.jpg", 20, 20, &cancelled).status == DecodeStatus::CANCELLED);
    moodycamel::ConcurrentQueue<UpdateEvent> queue;
    {
        FullResLoader loader(queue, 4);
        loader.request(17, root + "/bad.jpg", 3, 9);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool failed = false;
        while (std::chrono::steady_clock::now() < deadline && !failed) {
            UpdateEvent event;
            if (queue.try_dequeue(event)) {
                assert(event.type == UpdateEvent::Type::FULL_RES_FAILED);
                assert(event.job.image_id == 17 && event.job.revision == 3 && event.job.request == 9);
                assert(!event.message.empty()); failed = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(failed);
        loader.request(18, root + "/pixels.jpg", 3, 10);
        loader.cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        UpdateEvent event;
        assert(!queue.try_dequeue(event));
    }
    fs::remove_all(root);
}
