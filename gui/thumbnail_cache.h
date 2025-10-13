/*
 * thumbnail_cache.h - Async thumbnail cache with priority queue
 *
 * Copyright (c) 2025 Clifford Yapp
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <functional>

// Image metadata from fast scan
struct ImageMetadata {
    std::string filepath;
    std::string hash;
    int width = 0;
    int height = 0;
    double aspect_ratio = 1.0;
};

// Thumbnail quality levels
enum class ThumbQuality {
    NONE = 0,      // Grey rectangle only
    TINY = 32,     // 32px thumbnail
    SMALL = 64,    // 64px thumbnail
    MEDIUM = 128,  // 128px thumbnail
    LARGE = 256,   // 256px thumbnail
    XLARGE = 512,  // 512px thumbnail
    FULL = 1024    // 1024px thumbnail
};

// Thumbnail data
struct ThumbnailData {
    ThumbQuality quality = ThumbQuality::NONE;
    std::vector<uint8_t> jpeg_data;
    int width = 0;
    int height = 0;
    bool in_progress = false;
};

// Work item for thumbnail generation
struct ThumbnailWork {
    size_t image_index;
    ThumbQuality target_quality;
    bool is_priority;  // Visible in viewport

    bool operator<(const ThumbnailWork& other) const {
        // Higher priority items should come first in priority queue
        if (is_priority != other.is_priority)
            return !is_priority;  // priority items have lower value in max-heap
        return image_index > other.image_index;  // Earlier images first
    }
};

// Callback for when thumbnail is ready
using ThumbnailCallback = std::function<void(size_t image_index, ThumbQuality quality)>;

class ThumbnailCache {
public:
    ThumbnailCache();
    ~ThumbnailCache();

    // Initialize with image list and start workers
    void initialize(const std::vector<ImageMetadata>& images, int num_workers = 4);
    
    // Shutdown workers
    void shutdown();

    // Get current thumbnail for an image
    ThumbnailData get_thumbnail(size_t image_index);

    // Get image metadata
    ImageMetadata get_metadata(size_t image_index);

    // Request thumbnails for visible images (resets priority queue)
    void prioritize_visible_images(const std::vector<size_t>& visible_indices);

    // Set callback for thumbnail updates
    void set_callback(ThumbnailCallback callback);

    // Get total image count
    size_t image_count() const { return images_.size(); }

private:
    // Worker thread function
    void worker_thread();

    // Generate thumbnail at specific quality level
    bool generate_thumbnail(size_t image_index, ThumbQuality quality);

    // Database handle for loading thumbnails
    std::string db_path_;

    // Image metadata
    std::vector<ImageMetadata> images_;

    // Thumbnail cache (protected by cache_mutex_)
    std::unordered_map<size_t, ThumbnailData> thumbnails_;
    mutable std::mutex cache_mutex_;

    // Work queue (protected by queue_mutex_)
    std::priority_queue<ThumbnailWork> work_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // Worker threads
    std::vector<std::thread> workers_;
    std::atomic<bool> shutdown_requested_;

    // Callback
    ThumbnailCallback callback_;
    std::mutex callback_mutex_;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
