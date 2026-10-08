#include "tile_manager.h"
#include "utils.h"
#include "stb_image.h"
#include <png.h>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
TileManager::TileManager(moodycamel::ConcurrentQueue<UpdateEvent>& queue, uint64_t session)
    : publisher_(queue, session, &stop_) {
    generator_ = std::thread(&TileManager::generate_worker, this);
    reader_ = std::thread(&TileManager::read_worker, this);
}
TileManager::~TileManager() {
    stop_ = true;
    cancel();
    if (generator_.joinable()) generator_.join();
    if (reader_.joinable()) reader_.join();
}
void TileManager::init(const std::string& cache_dir) {
    cache_dir_ = cache_dir + "/tiles";
}
void TileManager::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++sequence_;
    pending_ = false;
    reads_.clear(); pending_reads_.clear(); failed_reads_.clear();
    wake_.notify_all();
}
bool TileManager::current(const Request& req) const { return !stop_ && req.sequence == sequence_; }
void TileManager::request_tiles(size_t id, const std::string& path, const std::string& hash, uint64_t revision, uint64_t request) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++sequence_;
    active_ = {{publisher_.session(), id, revision, request}, path, hash, sequence_.load()};
    reads_.clear(); pending_reads_.clear(); failed_reads_.clear();
    pending_ = true;
    wake_.notify_all();
}
bool TileManager::are_tiles_ready(const std::string& hash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_.count(hash) != 0;
}
std::shared_ptr<const TileManager::Tile> TileManager::get_tile(const std::string& hash, int tx, int ty, int zoom) {
    if (tx < 0 || ty < 0 || zoom != 0) return {};
    const std::string name = "z0_" + std::to_string(tx) + "_" + std::to_string(ty) + ".jpg";
    const std::string key = hash + "/" + name;
    std::lock_guard<std::mutex> lock(mutex_);
    auto cached = cache_map_.find(key);
    if (cached != cache_map_.end()) {
        cache_.splice(cache_.begin(), cache_, cached->second);
        return cached->second->tile;
    }
    if (hash != active_.hash || !ready_.count(hash) || reads_.size() >= MAX_PENDING_READS ||
        pending_reads_.count(key) || failed_reads_.count(key)) return {};
    pending_reads_.insert(key);
    reads_.push_back({active_, key, cache_dir_ + "/" + hash + "/v2/" + name});
    wake_.notify_all();
    return {};
}
void TileManager::fail(const Request& req, const std::string& message) {
    if (current(req)) publisher_.enqueue(UpdateEvent::make_message(UpdateEvent::Type::FULL_RES_FAILED, message), req.token);
}
void TileManager::read_worker() {
    while (!stop_) {
        Read read;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !reads_.empty(); });
            if (stop_) break;
            read = std::move(reads_.front()); reads_.pop_front();
        }
        WorkPermit work(&stop_, true);
        if (!work) break;
        auto tile = std::make_shared<Tile>();
        int channels = 0;
        // Check bounds before decoding a corrupt on-disk cache entry.
        if (stbi_info(read.path.c_str(), &tile->width, &tile->height, &channels) &&
            tile->width > 0 && tile->height > 0 && tile->width <= TILE_SIZE && tile->height <= TILE_SIZE) {
            auto* data = stbi_load(read.path.c_str(), &tile->width, &tile->height, &channels, 3);
            if (data) { tile->rgb.assign(data, data + size_t(tile->width) * tile->height * 3); stbi_image_free(data); }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!current(read.request)) continue;
            pending_reads_.erase(read.key);
            if (tile->rgb.empty()) failed_reads_.insert(read.key);
            else {
                cache_bytes_ += tile->rgb.size();
                cache_.push_front({read.key, tile}); cache_map_[read.key] = cache_.begin();
                while (cache_bytes_ > MAX_CACHE_BYTES) {
                    auto last = std::prev(cache_.end());
                    cache_bytes_ -= last->tile->rgb.size(); cache_map_.erase(last->key); cache_.erase(last);
                }
            }
        }
        if (tile->rgb.empty()) {
            std::error_code ec;
            fs::remove(fs::path(read.path).parent_path() / "ready.marker", ec);
            fail(read.request, "A cached tile is missing or damaged. Use F5 to regenerate it.");
        }
        else publisher_.enqueue(UpdateEvent::make_message(UpdateEvent::Type::TILE_AVAILABLE, ""), read.request.token);
    }
}
void TileManager::generate_worker() {
    while (!stop_) {
        Request req;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || pending_; });
            if (stop_) break;
            req = active_; pending_ = false;
        }
        try {
            WorkPermit work(&stop_, true);
            if (!work) break;
            const auto probe = probe_image(req.path);
            if (!probe || !probe.supports_tiles) {
                fail(req, "Full-resolution tiles are unavailable for this format. Showing the overview.");
                continue;
            }
            if (req.hash.empty()) { fail(req, "Image identity is unavailable. Use F5 to rescan."); continue; }
            const auto dir = fs::path(cache_dir_) / req.hash / "v2";
            int version = 0, w = 0, h = 0;
            std::ifstream marker(dir / "ready.marker");
            marker >> version >> w >> h;
            bool valid = version == 2 && w == probe.width && h == probe.height;
            // Validate all expected tiles so a retry repairs interrupted/manual cache damage.
            if (valid) for (int y = 0; y < (h + TILE_SIZE - 1) / TILE_SIZE && valid; ++y)
                for (int x = 0; x < (w + TILE_SIZE - 1) / TILE_SIZE && valid; ++x) {
                    std::error_code ec;
                    auto path = dir / ("z0_" + std::to_string(x) + "_" + std::to_string(y) + ".jpg");
                    int tw = 0, th = 0, channels = 0;
                    valid = fs::is_regular_file(path, ec) && stbi_info(path.c_str(), &tw, &th, &channels) &&
                        tw == std::min(TILE_SIZE, w - x * TILE_SIZE) && th == std::min(TILE_SIZE, h - y * TILE_SIZE);
                }
            if (!current(req)) continue;
            if (!valid && !generate_tiles(req)) {
                fail(req, "Could not generate image tiles. Check the source image and cache permissions.");
                continue;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!current(req)) continue;
                ready_.insert(req.hash);
            }
            publisher_.enqueue(UpdateEvent::make_message(UpdateEvent::Type::TILES_READY, ""), req.token);
        } catch (const std::exception& e) { fail(req, e.what()); }
    }
}

bool TileManager::generate_tiles(const Request& req) {
    FileStamp before, after;
    std::string hash;
    if (!read_file_stamp(req.path, before) || !compute_file_hash(req.path, hash, &stop_) || hash != req.hash) return false;
    const fs::path parent = fs::path(cache_dir_) / req.hash;
    const fs::path temporary = parent / ("building-" + std::to_string(getpid()) + "-" + std::to_string(req.sequence));
    fs::create_directories(temporary);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{temporary};
    FILE* file = fopen(req.path.c_str(), "rb");
    if (!file) return false;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) { fclose(file); return false; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); fclose(file); return false; }
    auto memory = ByteBudget::decode().acquire(132ULL * 1024 * 1024, &stop_);
    if (!memory) { png_destroy_read_struct(&png, &info, nullptr); fclose(file); return false; }
    std::vector<uint8_t> stripe, tile, encoded;
    std::ofstream output;
    std::string tile_path;
    auto close = [&] { png_destroy_read_struct(&png, &info, nullptr); fclose(file); };
    if (setjmp(png_jmpbuf(png))) { close(); return false; }
    png_init_io(png, file);
    png_read_info(png, info);
    const int width = png_get_image_width(png, info), height = png_get_image_height(png, info);
    if (width <= 0 || height <= 0 || uint64_t(width) * TILE_SIZE * 3 > 128ULL * 1024 * 1024 ||
        png_get_interlace_type(png, info) != PNG_INTERLACE_NONE) { close(); return false; }
    const auto color = png_get_color_type(png, info);
    if (png_get_bit_depth(png, info) == 16) png_set_strip_16(png);
    if (color == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color == PNG_COLOR_TYPE_GRAY && png_get_bit_depth(png, info) < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color == PNG_COLOR_TYPE_GRAY || color == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    if ((color & PNG_COLOR_MASK_ALPHA) || png_get_valid(png, info, PNG_INFO_tRNS)) png_set_strip_alpha(png);
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != size_t(width) * 3) { close(); return false; }
    stripe.resize(size_t(width) * TILE_SIZE * 3);
    const int rows = (height + TILE_SIZE - 1) / TILE_SIZE;
    for (int ty = 0; ty < rows; ++ty) {
        if (!current(req)) { close(); return false; }
        const int th = std::min(TILE_SIZE, height - ty * TILE_SIZE);
        for (int y = 0; y < th; ++y) png_read_row(png, stripe.data() + size_t(y) * width * 3, nullptr);
        for (int tx = 0; tx < (width + TILE_SIZE - 1) / TILE_SIZE; ++tx) {
            const int tw = std::min(TILE_SIZE, width - tx * TILE_SIZE);
            tile.resize(size_t(tw) * th * 3);
            for (int y = 0; y < th; ++y) std::copy_n(stripe.data() + (size_t(y) * width + tx * TILE_SIZE) * 3,
                tw * 3, tile.data() + size_t(y) * tw * 3);
            tile_path = (temporary / ("z0_" + std::to_string(tx) + "_" + std::to_string(ty) + ".jpg")).string();
            encoded = encode_jpeg(tile.data(), tw, th, 85);
            if (encoded.empty()) { close(); return false; }
            output.open(tile_path, std::ios::binary | std::ios::trunc);
            output.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            output.close();
            if (!output) { close(); return false; }
        }
        publisher_.enqueue(UpdateEvent::make_tile_progress(req.token.image_id, req.path, ty + 1, rows), req.token);
    }
    png_read_end(png, info);
    close();
    if (!current(req) || !read_file_stamp(req.path, after) || before != after) return false;
    std::ofstream marker(temporary / "ready.marker");
    marker << "2 " << width << ' ' << height << '\n'; marker.close();
    if (!marker) return false;
    std::error_code ec;
    fs::remove_all(parent / "v2", ec);
    if (ec) return false;
    fs::rename(temporary, parent / "v2", ec);
    return !ec;
}
