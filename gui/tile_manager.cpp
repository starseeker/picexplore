#include "tile_manager.h"
#include <iostream>
#include <png.h>
#include <cstring>
#include "../third_party/stb/stb_image_write.h"
#include "../third_party/stb/stb_image.h"

TileManager::TileManager(moodycamel::ConcurrentQueue<UpdateEvent>& update_queue)
    : update_queue_(update_queue) {
    worker_ = std::thread(&TileManager::worker_thread, this);
}

TileManager::~TileManager() {
    stop_requested_ = true;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void TileManager::init(const std::string& cache_dir) {
    cache_dir_ = cache_dir + "/tiles";
    std::filesystem::create_directories(cache_dir_);
}

void TileManager::request_tiles(size_t image_index, const std::string& filepath, const std::string& hash) {
    {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        if (ready_hashes_.count(hash)) {
            // Already generated!
            update_queue_.enqueue(UpdateEvent::make_full_res_ready(image_index, filepath, {}, 0, 0)); // Using empty rgb as tiled signal
            return;
        }
    }
    
    // Check if it already exists on disk
    std::string marker_path = cache_dir_ + "/" + hash + "/ready.marker";
    if (std::filesystem::exists(marker_path)) {
        std::lock_guard<std::mutex> lock(ready_mutex_);
        ready_hashes_.insert(hash);
        update_queue_.enqueue(UpdateEvent::make_full_res_ready(image_index, filepath, {}, 0, 0));
        return;
    }

    TileRequest req;
    req.image_index = image_index;
    req.filepath = filepath;
    req.hash = hash;
    request_queue_.enqueue(req);
}

bool TileManager::are_tiles_ready(const std::string& hash) const {
    std::string marker_path = cache_dir_ + "/" + hash + "/ready.marker";
    return std::filesystem::exists(marker_path);
}

std::vector<uint8_t> TileManager::get_tile(const std::string& hash, int tx, int ty, int zoom_level, int& out_w, int& out_h) {
    std::string key = hash + "_z" + std::to_string(zoom_level) + "_" + std::to_string(tx) + "_" + std::to_string(ty);
    
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        auto it = tile_cache_map_.find(key);
        if (it != tile_cache_map_.end()) {
            // Move to front
            tile_cache_.splice(tile_cache_.begin(), tile_cache_, it->second);
            out_w = tile_cache_.front().w;
            out_h = tile_cache_.front().h;
            return tile_cache_.front().rgb;
        }
    }

    std::string tile_path = cache_dir_ + "/" + hash + "/z" + std::to_string(zoom_level) + "_" + std::to_string(tx) + "_" + std::to_string(ty) + ".jpg";
    if (!std::filesystem::exists(tile_path)) return {};
    
    int channels;
    unsigned char* data = stbi_load(tile_path.c_str(), &out_w, &out_h, &channels, 3);
    if (!data) return {};
    
    std::vector<uint8_t> rgb_data(data, data + (out_w * out_h * 3));
    stbi_image_free(data);
    
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        tile_cache_.push_front({key, rgb_data, out_w, out_h});
        tile_cache_map_[key] = tile_cache_.begin();
        
        if (tile_cache_.size() > MAX_CACHE_TILES) {
            auto last = tile_cache_.end();
            last--;
            tile_cache_map_.erase(last->key);
            tile_cache_.pop_back();
        }
    }
    
    return rgb_data;
}

void TileManager::worker_thread() {
    while (!stop_requested_) {
        TileRequest req;
        if (request_queue_.try_dequeue(req)) {
            if (generate_tiles(req.image_index, req.filepath, req.hash)) {
                std::lock_guard<std::mutex> lock(ready_mutex_);
                ready_hashes_.insert(req.hash);
                // We use empty rgb_data with w=0, h=0 to signal that tiles are ready
                update_queue_.enqueue(UpdateEvent::make_full_res_ready(req.image_index, req.filepath, {}, 0, 0));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool TileManager::generate_tiles(size_t image_index, const std::string& filepath, const std::string& hash) {
    std::string out_dir = cache_dir_ + "/" + hash;
    std::filesystem::create_directories(out_dir);

    FILE *fp = fopen(filepath.c_str(), "rb");
    if (!fp) return false;
    
    unsigned char header[8];
    if (fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8)) {
        fclose(fp);
        return false;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop info = png_create_info_struct(png);
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);
        return false;
    }
    png_init_io(png, fp);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    int width = png_get_image_width(png, info);
    int height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth  = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    int cols = (width + TILE_SIZE - 1) / TILE_SIZE;
    int rows = (height + TILE_SIZE - 1) / TILE_SIZE;

    std::vector<uint8_t> stripe(width * TILE_SIZE * 3, 0);
    int row_bytes = png_get_rowbytes(png, info);
    std::vector<png_byte> row_buf(row_bytes);

    for (int ty = 0; ty < rows; ty++) {
        if (stop_requested_) break;
        
        // Report progress every row chunk
        update_queue_.enqueue(UpdateEvent::make_tile_progress(image_index, filepath, ty, rows));
        int current_tile_height = std::min(TILE_SIZE, height - ty * TILE_SIZE);
        
        for (int y = 0; y < current_tile_height; y++) {
            png_read_row(png, row_buf.data(), NULL);
            for (int x = 0; x < width; x++) {
                stripe[(y * width + x) * 3 + 0] = row_buf[x * 4 + 0];
                stripe[(y * width + x) * 3 + 1] = row_buf[x * 4 + 1];
                stripe[(y * width + x) * 3 + 2] = row_buf[x * 4 + 2];
            }
        }
        
        for (int tx = 0; tx < cols; tx++) {
            int current_tile_width = std::min(TILE_SIZE, width - tx * TILE_SIZE);
            std::vector<uint8_t> tile_rgb(current_tile_width * current_tile_height * 3, 0);
            
            for (int y = 0; y < current_tile_height; y++) {
                memcpy(tile_rgb.data() + y * current_tile_width * 3,
                       stripe.data() + (y * width + tx * TILE_SIZE) * 3,
                       current_tile_width * 3);
            }
            
            // Generate z0 tile (full resolution)
            std::string out_path = out_dir + "/z0_" + std::to_string(tx) + "_" + std::to_string(ty) + ".jpg";
            stbi_write_jpg(out_path.c_str(), current_tile_width, current_tile_height, 3, tile_rgb.data(), 80);
        }
    }

    png_destroy_read_struct(&png, &info, NULL);
    fclose(fp);
    
    if (!stop_requested_) {
        // Create marker
        FILE* marker = fopen((out_dir + "/ready.marker").c_str(), "w");
        if (marker) {
            fprintf(marker, "%d %d\n", width, height);
            fclose(marker);
        }
        return true;
    }
    return false;
}
