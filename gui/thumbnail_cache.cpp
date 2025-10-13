/*
 * thumbnail_cache.cpp - Async thumbnail cache implementation
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

#include "thumbnail_cache.h"
#include <algorithm>
#include <filesystem>
#include <iostream>

#include "stb_image.h"
#include "stb_image_resize2.h"

#include <jpeglib.h>
#include <setjmp.h>

namespace fs = std::filesystem;

ThumbnailCache::ThumbnailCache()
    : shutdown_requested_(false)
{
}

ThumbnailCache::~ThumbnailCache()
{
    shutdown();
}

void ThumbnailCache::initialize(const std::vector<ImageMetadata>& images, int num_workers)
{
    images_ = images;
    
    // Initialize thumbnail cache with empty entries
    for (size_t i = 0; i < images_.size(); ++i) {
        thumbnails_[i] = ThumbnailData();
    }

    // Start worker threads
    shutdown_requested_ = false;
    for (int i = 0; i < num_workers; ++i) {
        workers_.emplace_back(&ThumbnailCache::worker_thread, this);
    }

    // Queue initial work for all images (lowest priority)
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (size_t i = 0; i < images_.size(); ++i) {
            // Start with tiny thumbnails
            work_queue_.push({i, ThumbQuality::TINY, false});
        }
    }
    queue_cv_.notify_all();
}

void ThumbnailCache::shutdown()
{
    shutdown_requested_ = true;
    queue_cv_.notify_all();
    
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

ThumbnailData ThumbnailCache::get_thumbnail(size_t image_index)
{
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = thumbnails_.find(image_index);
    if (it != thumbnails_.end()) {
        return it->second;
    }
    return ThumbnailData();
}

ImageMetadata ThumbnailCache::get_metadata(size_t image_index)
{
    if (image_index < images_.size()) {
        return images_[image_index];
    }
    return ImageMetadata();
}

void ThumbnailCache::prioritize_visible_images(const std::vector<size_t>& visible_indices)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    // Clear the work queue and rebuild it
    std::priority_queue<ThumbnailWork> empty_queue;
    std::swap(work_queue_, empty_queue);

    // Add visible images with high priority, requesting progressively better quality
    for (size_t idx : visible_indices) {
        if (idx >= images_.size())
            continue;

        // Check current quality level
        ThumbQuality current_quality;
        {
            std::lock_guard<std::mutex> cache_lock(cache_mutex_);
            auto it = thumbnails_.find(idx);
            current_quality = (it != thumbnails_.end()) ? it->second.quality : ThumbQuality::NONE;
        }

        // Queue next quality level
        if (current_quality < ThumbQuality::FULL) {
            ThumbQuality next_quality = static_cast<ThumbQuality>(
                static_cast<int>(current_quality) * 2);
            if (next_quality == ThumbQuality::NONE)
                next_quality = ThumbQuality::TINY;
            
            work_queue_.push({idx, next_quality, true});
        }
    }

    // Add non-visible images with lower priority
    for (size_t i = 0; i < images_.size(); ++i) {
        // Skip if already visible
        if (std::find(visible_indices.begin(), visible_indices.end(), i) != visible_indices.end())
            continue;

        ThumbQuality current_quality;
        {
            std::lock_guard<std::mutex> cache_lock(cache_mutex_);
            auto it = thumbnails_.find(i);
            current_quality = (it != thumbnails_.end()) ? it->second.quality : ThumbQuality::NONE;
        }

        if (current_quality < ThumbQuality::FULL) {
            ThumbQuality next_quality = static_cast<ThumbQuality>(
                static_cast<int>(current_quality) * 2);
            if (next_quality == ThumbQuality::NONE)
                next_quality = ThumbQuality::TINY;
            
            work_queue_.push({i, next_quality, false});
        }
    }

    queue_cv_.notify_all();
}

void ThumbnailCache::set_callback(ThumbnailCallback callback)
{
    std::lock_guard<std::mutex> lock(callback_mutex_);
    callback_ = callback;
}

void ThumbnailCache::worker_thread()
{
    while (!shutdown_requested_) {
        ThumbnailWork work;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return shutdown_requested_ || !work_queue_.empty();
            });

            if (shutdown_requested_)
                break;

            if (work_queue_.empty())
                continue;

            work = work_queue_.top();
            work_queue_.pop();
        }

        // Mark as in progress
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto& thumb = thumbnails_[work.image_index];
            if (thumb.in_progress || thumb.quality >= work.target_quality)
                continue;
            thumb.in_progress = true;
        }

        // Generate thumbnail
        bool success = generate_thumbnail(work.image_index, work.target_quality);

        // Update cache
        {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            thumbnails_[work.image_index].in_progress = false;
        }

        // Notify callback if successful
        if (success) {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (callback_) {
                callback_(work.image_index, work.target_quality);
            }
        }
    }
}

// JPEG error handling
struct jpeg_error_mgr_wrapper {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void jpeg_error_exit(j_common_ptr cinfo)
{
    jpeg_error_mgr_wrapper* err = (jpeg_error_mgr_wrapper*)cinfo->err;
    longjmp(err->setjmp_buffer, 1);
}

bool ThumbnailCache::generate_thumbnail(size_t image_index, ThumbQuality quality)
{
    if (image_index >= images_.size())
        return false;

    const ImageMetadata& meta = images_[image_index];
    int target_size = static_cast<int>(quality);

    // Calculate target dimensions
    double aspect_ratio = meta.aspect_ratio;
    int thumb_width, thumb_height;
    if (meta.width > meta.height) {
        thumb_width = target_size;
        thumb_height = (int)(target_size / aspect_ratio);
    } else {
        thumb_height = target_size;
        thumb_width = (int)(target_size * aspect_ratio);
    }

    // Try to use libjpeg-turbo for JPEG files
    auto ext = fs::path(meta.filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::vector<uint8_t> thumb_rgb;
    int actual_width = 0, actual_height = 0;

    if (ext == ".jpg" || ext == ".jpeg") {
        // Determine optimal scale factor
        int scale_factor = 1;
        if (meta.width / 8 >= thumb_width && meta.height / 8 >= thumb_height)
            scale_factor = 8;
        else if (meta.width / 4 >= thumb_width && meta.height / 4 >= thumb_height)
            scale_factor = 4;
        else if (meta.width / 2 >= thumb_width && meta.height / 2 >= thumb_height)
            scale_factor = 2;

        // Decode JPEG with scaling
        FILE* fp = fopen(meta.filepath.c_str(), "rb");
        if (fp) {
            struct jpeg_decompress_struct cinfo;
            jpeg_error_mgr_wrapper jerr;

            cinfo.err = jpeg_std_error(&jerr.pub);
            jerr.pub.error_exit = jpeg_error_exit;

            if (setjmp(jerr.setjmp_buffer) == 0) {
                jpeg_create_decompress(&cinfo);
                jpeg_stdio_src(&cinfo, fp);
                jpeg_read_header(&cinfo, TRUE);

                cinfo.scale_num = 1;
                cinfo.scale_denom = scale_factor;

                jpeg_start_decompress(&cinfo);

                actual_width = cinfo.output_width;
                actual_height = cinfo.output_height;
                int row_stride = actual_width * cinfo.output_components;

                thumb_rgb.resize(actual_width * actual_height * 3);

                while (cinfo.output_scanline < cinfo.output_height) {
                    unsigned char* row = thumb_rgb.data() + cinfo.output_scanline * actual_width * 3;
                    jpeg_read_scanlines(&cinfo, &row, 1);
                }

                jpeg_finish_decompress(&cinfo);
                jpeg_destroy_decompress(&cinfo);
            } else {
                jpeg_destroy_decompress(&cinfo);
            }
            fclose(fp);
        }
    }

    // Fall back to stb_image if JPEG decode failed or for other formats
    if (thumb_rgb.empty()) {
        int width, height, channels;
        unsigned char* image_data = stbi_load(meta.filepath.c_str(), &width, &height, &channels, 3);
        
        if (!image_data)
            return false;

        // Resize using stb_image_resize
        thumb_rgb.resize(thumb_width * thumb_height * 3);
        stbir_resize_uint8_linear(image_data, width, height, 0,
                                   thumb_rgb.data(), thumb_width, thumb_height, 0,
                                   STBIR_RGB);
        
        actual_width = thumb_width;
        actual_height = thumb_height;
        
        stbi_image_free(image_data);
    }

    // Encode as JPEG
    if (!thumb_rgb.empty()) {
        // Simple JPEG encoding
        std::vector<uint8_t> jpeg_data;
        
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);

        unsigned char* outbuffer = nullptr;
        unsigned long outsize = 0;
        jpeg_mem_dest(&cinfo, &outbuffer, &outsize);

        cinfo.image_width = actual_width;
        cinfo.image_height = actual_height;
        cinfo.input_components = 3;
        cinfo.in_color_space = JCS_RGB;

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 90, TRUE);
        jpeg_start_compress(&cinfo, TRUE);

        int row_stride = actual_width * 3;
        while (cinfo.next_scanline < cinfo.image_height) {
            JSAMPROW row = thumb_rgb.data() + cinfo.next_scanline * row_stride;
            jpeg_write_scanlines(&cinfo, &row, 1);
        }

        jpeg_finish_compress(&cinfo);
        
        if (outbuffer && outsize > 0) {
            jpeg_data.assign(outbuffer, outbuffer + outsize);
            free(outbuffer);
        }
        
        jpeg_destroy_compress(&cinfo);

        // Update cache
        if (!jpeg_data.empty()) {
            std::lock_guard<std::mutex> lock(cache_mutex_);
            auto& thumb = thumbnails_[image_index];
            thumb.quality = quality;
            thumb.jpeg_data = std::move(jpeg_data);
            thumb.width = actual_width;
            thumb.height = actual_height;
            return true;
        }
    }

    return false;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
