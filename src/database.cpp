/*
 * database.cpp - LMDB database and thumbnail operations for picscan
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

#include "database.h"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <unordered_set>

// Third-party dependencies
#include "xxhash.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include <jpeglib.h>
#include <jerror.h>
#include <csetjmp>
#include "TinyEXIF.h"

#define MAX_DB_SIZE 549755813888

namespace fs = std::filesystem;

namespace {
struct CustomJpegErrorMgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
    const char* filepath = nullptr;
};

static void custom_jpeg_error_exit(j_common_ptr cinfo) {
    CustomJpegErrorMgr* myerr = reinterpret_cast<CustomJpegErrorMgr*>(cinfo->err);
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    if (myerr && myerr->filepath && myerr->filepath[0] != '\0') {
        fprintf(stderr, "JPEG [%s]: %s\n", myerr->filepath, buffer);
    } else {
        fprintf(stderr, "JPEG error: %s\n", buffer);
    }
    longjmp(myerr->setjmp_buffer, 1);
}

static void custom_jpeg_output_message(j_common_ptr cinfo) {
    CustomJpegErrorMgr* myerr = reinterpret_cast<CustomJpegErrorMgr*>(cinfo->err);
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    if (myerr && myerr->filepath && myerr->filepath[0] != '\0') {
        fprintf(stderr, "JPEG [%s]: %s\n", myerr->filepath, buffer);
    } else {
        fprintf(stderr, "JPEG: %s\n", buffer);
    }
}
} // anonymous namespace
DatabaseManager::DatabaseManager() : env_(nullptr), txn_(nullptr), dbi_(0), is_open_(false), stop_processing_(false) {
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open(const std::string& db_path) {
    if (is_open_) {
	close();
    }

    int rc = mdb_env_create(&env_);
    if (rc != 0) {
	return false;
    }

    // Set map size to handle large databases (1GB)
    rc = mdb_env_set_mapsize(env_, MAX_DB_SIZE);
    if (rc != 0) {
	mdb_env_close(env_);
	env_ = nullptr;
	return false;
    }

    // Check if this is a new database for bulk insert optimization
    bool is_new_db = !std::filesystem::exists(db_path);

    unsigned int flags = MDB_NOSUBDIR | MDB_NOSYNC | MDB_NOMETASYNC | MDB_WRITEMAP;

    rc = mdb_env_open(env_, db_path.c_str(), flags, 0664);
    if (rc != 0) {
	mdb_env_close(env_);
	env_ = nullptr;
	return false;
    }

    // Open the DBI handle once for all transactions
    MDB_txn* setup_txn;
    rc = mdb_txn_begin(env_, nullptr, 0, &setup_txn);
    if (rc != 0) {
	mdb_env_close(env_);
	env_ = nullptr;
	return false;
    }

    rc = mdb_dbi_open(setup_txn, nullptr, MDB_CREATE, &dbi_);
    if (rc != 0) {
	mdb_txn_abort(setup_txn);
	mdb_env_close(env_);
	env_ = nullptr;
	return false;
    }

    rc = mdb_txn_commit(setup_txn);
    if (rc != 0) {
	mdb_env_close(env_);
	env_ = nullptr;
	return false;
    }

    is_open_ = true;
    return true;
}

void DatabaseManager::close() {
    if (txn_) {
	mdb_txn_abort(txn_);
	txn_ = nullptr;
    }
    if (env_) {
	mdb_env_close(env_);
	env_ = nullptr;
    }
    is_open_ = false;
}

bool DatabaseManager::begin_transaction() {
    if (!is_open_ || txn_) return false;

    int rc = mdb_txn_begin(env_, nullptr, 0, &txn_);
    if (rc != 0) {
	return false;
    }

    // DBI is already opened in DatabaseManager::open(), so we don't need to open it again
    return true;
}

bool DatabaseManager::commit_transaction() {
    if (!txn_) return false;

    int rc = mdb_txn_commit(txn_);
    txn_ = nullptr;
    return (rc == 0);
}

void DatabaseManager::abort_transaction() {
    if (txn_) {
	mdb_txn_abort(txn_);
	txn_ = nullptr;
    }
}

bool DatabaseManager::store_key_value(const std::string& key, const std::string& value) {
    if (!txn_) return false;

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();
    v.mv_data = (void*)value.c_str();
    v.mv_size = value.length();

    return mdb_put(txn_, dbi_, &k, &v, 0) == 0;
}

bool DatabaseManager::store_key_data(const std::string& key, const std::vector<uint8_t>& data) {
    if (!txn_) return false;

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();
    v.mv_data = (void*)data.data();
    v.mv_size = data.size();

    return mdb_put(txn_, dbi_, &k, &v, 0) == 0;
}

bool DatabaseManager::delete_key(const std::string& key) {
    if (!txn_) return false;

    MDB_val k;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();

    return mdb_del(txn_, dbi_, &k, nullptr) == 0;
}

bool DatabaseManager::get_key_value(const std::string& key, std::string& value) {
    if (!txn_) return false;

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();

    if (mdb_get(txn_, dbi_, &k, &v) == 0) {
	value.assign((char*)v.mv_data, v.mv_size);
	return true;
    }
    return false;
}

bool DatabaseManager::get_key_data(const std::string& key, std::vector<uint8_t>& data) {
    if (!txn_) return false;

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();

    if (mdb_get(txn_, dbi_, &k, &v) == 0) {
	data.assign((uint8_t*)v.mv_data, (uint8_t*)v.mv_data + v.mv_size);
	return true;
    }
    return false;
}

bool DatabaseManager::get_key_data_concurrent(const std::string& key, std::vector<uint8_t>& data) const {
    if (!is_open_ || !env_) return false;

    MDB_txn* read_txn = nullptr;
    if (mdb_txn_begin(env_, nullptr, MDB_RDONLY, &read_txn) != 0) {
        return false;
    }

    MDB_dbi dbi;
    if (mdb_dbi_open(read_txn, nullptr, 0, &dbi) != 0) {
        mdb_txn_abort(read_txn);
        return false;
    }

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();

    bool found = false;
    if (mdb_get(read_txn, dbi, &k, &v) == 0) {
        data.assign((uint8_t*)v.mv_data, (uint8_t*)v.mv_data + v.mv_size);
        found = true;
    }

    mdb_txn_abort(read_txn);
    return found;
}

bool DatabaseManager::get_hash_for_path_concurrent(const std::string& filepath, std::string& hash_out) const {
    std::string key = "file:" + filepath;
    std::vector<uint8_t> data;
    if (get_key_data_concurrent(key, data)) {
        hash_out.assign((char*)data.data(), data.size());
        return true;
    }
    return false;
}

int DatabaseManager::calculate_scale_factor(int image_width, int image_height, int target_width, int target_height) {
    if (target_width == 0 || target_height == 0) return 1;
    int scale_x = image_width / target_width;
    int scale_y = image_height / target_height;
    int scale_factor = std::max(scale_x, scale_y);

    // Round to nearest valid scale factor (1, 2, 4, 8)
    if (scale_factor <= 1) return 1;
    else if (scale_factor <= 2) return 2;
    else if (scale_factor <= 4) return 4;
    else return 8;
}

std::vector<uint8_t> DatabaseManager::decode_jpeg_thumbnail_rgb(const std::string& filepath, int scale_factor, int* actual_width, int* actual_height) {
    // Load file into memory
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
	fprintf(stderr, "Error: Failed to open file for JPEG decoding: '%s'\n", filepath.c_str());
	return {};
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> file_data(file_size);
    file.read(reinterpret_cast<char*>(file_data.data()), file_size);

    // Initialize JPEG decompression
    struct jpeg_decompress_struct cinfo;
    struct CustomJpegErrorMgr jerr;
    jerr.filepath = filepath.c_str();

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = custom_jpeg_error_exit;
    jerr.pub.output_message = custom_jpeg_output_message;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return {};
    }

    jpeg_create_decompress(&cinfo);

    // Set up memory source
    jpeg_mem_src(&cinfo, file_data.data(), file_size);

    // Read JPEG header
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
	fprintf(stderr, "Error: Failed to read JPEG header for '%s'\n", filepath.c_str());
	jpeg_destroy_decompress(&cinfo);
	return {};
    }

    // Set scaling based on scale_factor (1, 2, 4, 8)
    // libjpeg-turbo supports 1/8, 1/4, 1/2, 1/1 scaling
    switch (scale_factor) {
	case 1:
	    cinfo.scale_num = 1;
	    cinfo.scale_denom = 1;
	    break;
	case 2:
	    cinfo.scale_num = 1;
	    cinfo.scale_denom = 2;
	    break;
	case 4:
	    cinfo.scale_num = 1;
	    cinfo.scale_denom = 4;
	    break;
	case 8:
	    cinfo.scale_num = 1;
	    cinfo.scale_denom = 8;
	    break;
	default:
	    cinfo.scale_num = 1;
	    cinfo.scale_denom = 1;
	    break;
    }

    // Force RGB output
    cinfo.out_color_space = JCS_RGB;

    // Start decompression
    if (!jpeg_start_decompress(&cinfo)) {
	fprintf(stderr, "Error: Failed to start JPEG decompression for '%s'\n", filepath.c_str());
	jpeg_destroy_decompress(&cinfo);
	return {};
    }

    int output_width = cinfo.output_width;
    int output_height = cinfo.output_height;
    int output_components = cinfo.output_components;

    if (actual_width) *actual_width = output_width;
    if (actual_height) *actual_height = output_height;

    // Ensure we have RGB output (3 components)
    if (output_components != 3) {
	fprintf(stderr, "Error: Expected RGB output but got %d components for '%s'\n", output_components, filepath.c_str());
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	return {};
    }

    // Allocate RGB output buffer
    std::vector<uint8_t> rgb_data(output_width * output_height * 3);

    // Read scanlines
    JSAMPROW row_buffer = rgb_data.data();
    while (cinfo.output_scanline < cinfo.output_height) {
	JSAMPROW row_ptr = row_buffer + (cinfo.output_scanline * output_width * 3);
	if (jpeg_read_scanlines(&cinfo, &row_ptr, 1) != 1) {
	    fprintf(stderr, "Error: Failed to read scanline %d for '%s'\n", cinfo.output_scanline, filepath.c_str());
	    jpeg_finish_decompress(&cinfo);
	    jpeg_destroy_decompress(&cinfo);
	    return {};
	}
    }

    // Finish decompression and cleanup
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    return rgb_data;
}

bool DatabaseManager::generate_thumbnails(const std::string& filepath, const std::string& hash,
	unsigned char* image_data, int width, int height, int channels) {
    // Thumbnail sizes to generate (must match what PDF generator expects)
    std::vector<int> thumb_sizes = {32, 64, 128, 256, 512, 1024, 2048};

    auto ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    bool thumbnails_generated = true;

    if (ext == ".jpg" || ext == ".jpeg") {
	// For JPEG files, use libjpeg-turbo with optimized scale factor grouping
	std::map<int, std::vector<int>> scale_groups; // scale_factor -> list of thumb_sizes

	// Group thumbnail sizes by their optimal scale factor
	for (int thumb_size : thumb_sizes) {
	    // Calculate thumbnail dimensions maintaining aspect ratio
	    double aspect_ratio = (double)width / height;
	    int thumb_width, thumb_height;

	    if (width > height) {
		thumb_width = thumb_size;
		thumb_height = (int)(thumb_size / aspect_ratio);  
	    } else {
		thumb_height = thumb_size;
		thumb_width = (int)(thumb_size * aspect_ratio);
	    }

	    // Skip if image is already smaller than thumbnail size
	    if (width <= thumb_width && height <= thumb_height) {
		continue;
	    }

	    int scale_factor = calculate_scale_factor(width, height, thumb_width, thumb_height);
	    scale_groups[scale_factor].push_back(thumb_size);
	}

	// Process each scale factor group
	for (const auto& group : scale_groups) {
	    int scale_factor = group.first;
	    const std::vector<int>& sizes = group.second;

	    // Decode once for this scale factor
	    int actual_width, actual_height;
	    std::vector<uint8_t> rgb_thumb = decode_jpeg_thumbnail_rgb(filepath, scale_factor, &actual_width, &actual_height);

	    if (!rgb_thumb.empty()) {
		// Use this RGB data for all thumbnail sizes in this group
		for (int thumb_size : sizes) {
		    std::vector<uint8_t> thumb_data = encode_jpeg(rgb_thumb.data(), actual_width, actual_height, 90);

		    if (!thumb_data.empty()) {
			std::string thumb_key = hash + ":" + std::to_string(thumb_size);
			if (!store_key_data(thumb_key, thumb_data)) {
			    fprintf(stderr, "Error: Failed to store thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
			    thumbnails_generated = false;
			}
		    } else {
			fprintf(stderr, "Error: Failed to encode JPEG thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
			thumbnails_generated = false;
		    }
		}
	    } else {
		// Failed to decode with libjpeg-turbo, fall back to stb_image for these sizes  
		fprintf(stderr, "Warning: JPEG decoding failed for '%s' (scale factor %d), falling back to STB\n", filepath.c_str(), scale_factor);
		thumbnails_generated = false;
		break;
	    }
	}
    } else {
	// For non-JPEG files, set flag to use fallback processing
	thumbnails_generated = false;
    }

    // Fallback processing for non-JPEG files or if libjpeg-turbo failed
    if (!thumbnails_generated) {
	thumbnails_generated = true; // Reset the flag
	for (int thumb_size : thumb_sizes) {
	    // Calculate thumbnail dimensions maintaining aspect ratio
	    double aspect_ratio = (double)width / height;
	    int thumb_width, thumb_height;

	    if (width > height) {
		thumb_width = thumb_size;
		thumb_height = (int)(thumb_size / aspect_ratio);
	    } else {
		thumb_height = thumb_size;
		thumb_width = (int)(thumb_size * aspect_ratio);
	    }

	    // Skip if image is already smaller than thumbnail size
	    if (width <= thumb_width && height <= thumb_height) {
		continue;
	    }

	    // Use stb_image_resize for non-JPEG or fallback
	    std::vector<uint8_t> thumb_data;
	    bool thumb_success = false;

	    // Convert to RGB if needed
	    unsigned char* rgb_data = nullptr;
	    bool need_free_rgb = false;

	    if (channels == 3) {
		rgb_data = image_data;
	    } else {
		rgb_data = (unsigned char*)malloc(width * height * 3);
		need_free_rgb = true;

		for (int i = 0; i < width * height; i++) {
		    if (channels == 1) {
			// Grayscale to RGB
			rgb_data[i*3] = rgb_data[i*3+1] = rgb_data[i*3+2] = image_data[i];
		    } else if (channels == 2) {
			// Grayscale + Alpha to RGB (ignore alpha)
			rgb_data[i*3] = rgb_data[i*3+1] = rgb_data[i*3+2] = image_data[i*2];
		    } else if (channels == 4) {
			// RGBA to RGB (ignore alpha)
			rgb_data[i*3] = image_data[i*4];
			rgb_data[i*3+1] = image_data[i*4+1];
			rgb_data[i*3+2] = image_data[i*4+2];
		    }
		}
	    }

	    // Resize image
	    std::vector<uint8_t> resized_rgb(thumb_width * thumb_height * 3);
	    if (stbir_resize_uint8_linear(rgb_data, width, height, 0,
			resized_rgb.data(), thumb_width, thumb_height, 0, STBIR_RGB)) {

		// Encode as JPEG
		thumb_data = encode_jpeg(resized_rgb.data(), thumb_width, thumb_height, 90);
		if (!thumb_data.empty()) {
		    thumb_success = true;
		} else {
		    fprintf(stderr, "Error: Failed to encode JPEG thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
		}
	    } else {
		fprintf(stderr, "Error: Failed to resize image for '%s' (size %d)\n", filepath.c_str(), thumb_size);
	    }

	    if (need_free_rgb) {
		free(rgb_data);
	    }

	    if (thumb_success && !thumb_data.empty()) {
		std::string thumb_key = hash + ":" + std::to_string(thumb_size);
		if (!store_key_data(thumb_key, thumb_data)) {
		    fprintf(stderr, "Error: Failed to store thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
		    thumbnails_generated = false;
		}
	    } else {
		thumbnails_generated = false;
	    }
	}
    }

    return thumbnails_generated;
}

bool DatabaseManager::process_image_file(const std::string& filepath, 
	std::vector<WriteTask>& write_tasks,
	Timer& timer, bool& should_skip) {
    should_skip = false;

    auto ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    int width = 0, height = 0, channels = 3;
    unsigned char* image_data = nullptr;
    std::vector<uint8_t> decoded_buf;
    bool needs_free_stbi = false;
    bool needs_free_malloc = false;

    if (ext == ".tif" || ext == ".tiff") {
        if (load_tiff_file(filepath, 0, 0, decoded_buf, width, height)) {
            image_data = decoded_buf.data();
        }
    } else if (ext == ".webp") {
        if (load_webp_file(filepath, 0, 0, decoded_buf, width, height)) {
            image_data = decoded_buf.data();
        }
    } else if (ext == ".png") {
        int info_w, info_h, info_c;
        if (stbi_info(filepath.c_str(), &info_w, &info_h, &info_c) && (long long)info_w * info_h > 25000000LL) {
            if (load_png_file(filepath, 0, 0, decoded_buf, width, height)) {
                image_data = decoded_buf.data();
            }
        } else {
            image_data = stbi_load(filepath.c_str(), &width, &height, &channels, 3);
            if (image_data) needs_free_stbi = true;
        }
    } else {
        image_data = stbi_load(filepath.c_str(), &width, &height, &channels, 3);
        if (image_data) needs_free_stbi = true;
    }

    if (!image_data || width <= 0 || height <= 0) {
        fprintf(stderr, "Error: Failed to load image '%s'\n", filepath.c_str());
        if (needs_free_stbi && image_data) stbi_image_free(image_data);
        should_skip = true;
        return false;
    }
    channels = 3;

    // Read EXIF orientation and apply transformation if needed
    int orientation = get_exif_orientation(filepath);
    if (orientation > 1) {
        unsigned char* rgb_data = (unsigned char*)malloc(width * height * 3);
        memcpy(rgb_data, image_data, width * height * 3);
        apply_orientation_transform(rgb_data, width, height, orientation);
        if (needs_free_stbi) stbi_image_free(image_data);
        image_data = rgb_data;
        needs_free_stbi = false;
        needs_free_malloc = true;
    }

    // Compute content hash using fast SIMD-vectorized 128-bit hash
    size_t data_size = width * height * channels;
    XXH128_hash_t hash = XXH3_128bits(image_data, data_size);
    char hash_str[33];
    snprintf(hash_str, sizeof(hash_str), "%016llx%016llx",
             (unsigned long long)hash.high64, (unsigned long long)hash.low64);

    // Check if this hash already exists (duplicate detection)
    bool has_existing = false;
    {
        std::lock_guard<std::mutex> lock(db_mutex_);
        if (begin_transaction()) {
            std::string val;
            has_existing = get_key_value(std::string(hash_str) + ":paths", val) ||
                           get_key_value(std::string(hash_str) + ":path", val);
            abort_transaction();
        }
    }

    if (has_existing) {
        if (needs_free_stbi && image_data) stbi_image_free(image_data);
        else if (needs_free_malloc && image_data) free(image_data);
        write_tasks.emplace_back(WriteTask::ADD_PATH_FOR_HASH, std::string(hash_str), filepath);
        write_tasks.emplace_back(WriteTask::STORE_PATH, "file:" + filepath, std::string(hash_str));
        should_skip = true;
        return true; // Not an error, just a duplicate
    }

    // Store file path as write task
    write_tasks.emplace_back(WriteTask::ADD_PATH_FOR_HASH, std::string(hash_str), filepath);
    write_tasks.emplace_back(WriteTask::STORE_PATH, "file:" + filepath, std::string(hash_str));

    // Store metadata write task (file size, timestamp, orig dimensions)
    uint64_t f_size = 0, f_time = 0;
    try {
        f_size = fs::file_size(filepath);
        f_time = std::chrono::duration_cast<std::chrono::seconds>(
            fs::last_write_time(filepath).time_since_epoch()).count();
    } catch (...) {}

    ImageMetadata meta{f_size, f_time, width, height};
    std::vector<uint8_t> meta_bytes(sizeof(ImageMetadata));
    std::memcpy(meta_bytes.data(), &meta, sizeof(ImageMetadata));
    write_tasks.emplace_back(WriteTask::STORE_THUMBNAIL, std::string(hash_str) + ":meta", meta_bytes);

    // Generate and store thumbnails
    std::vector<int> thumb_sizes = {32, 64, 128, 256, 512, 1024, 2048};
    bool thumbnails_generated = true;

    if (ext == ".jpg" || ext == ".jpeg") {
	// For JPEG files, use libjpeg-turbo with optimized scale factor grouping
	std::map<int, std::vector<int>> scale_groups; // scale_factor -> list of thumb_sizes

	// Group thumbnail sizes by their optimal scale factor
	for (int thumb_size : thumb_sizes) {
	    // Calculate thumbnail dimensions maintaining aspect ratio
	    double aspect_ratio = (double)width / height;
	    int thumb_width, thumb_height;

	    if (width > height) {
		thumb_width = thumb_size;
		thumb_height = (int)(thumb_size / aspect_ratio);  
	    } else {
		thumb_height = thumb_size;
		thumb_width = (int)(thumb_size * aspect_ratio);
	    }

	    // Skip if image is already smaller than thumbnail size
	    if (width <= thumb_width && height <= thumb_height) {
		continue;
	    }

	    int scale_factor = calculate_scale_factor(width, height, thumb_width, thumb_height);
	    scale_groups[scale_factor].push_back(thumb_size);
	}

	// Process each scale factor group
	for (const auto& group : scale_groups) {
	    int scale_factor = group.first;
	    const std::vector<int>& sizes = group.second;

	    // Decode once for this scale factor
	    int actual_width, actual_height;
	    std::vector<uint8_t> rgb_thumb = decode_jpeg_thumbnail_rgb(filepath, scale_factor, &actual_width, &actual_height);

	    if (!rgb_thumb.empty()) {
		// Use this RGB data for all thumbnail sizes in this group
		for (int thumb_size : sizes) {
		    std::vector<uint8_t> thumb_data = encode_jpeg(rgb_thumb.data(), actual_width, actual_height, 90);

		    if (!thumb_data.empty()) {
			std::string thumb_key = std::string(hash_str) + ":" + std::to_string(thumb_size);
			write_tasks.emplace_back(WriteTask::STORE_THUMBNAIL, thumb_key, thumb_data);
		    } else {
			fprintf(stderr, "Error: Failed to encode JPEG thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
			thumbnails_generated = false;
		    }
		}
	    } else {
		// Failed to decode with libjpeg-turbo, fall back to stb_image for these sizes  
		fprintf(stderr, "Warning: JPEG decoding failed for '%s' (scale factor %d), falling back to STB\n", filepath.c_str(), scale_factor);
		thumbnails_generated = false;
		break;
	    }
	}
    } else {
	// For non-JPEG files, set flag to use fallback processing
	thumbnails_generated = false;
    }

    // Fallback processing for non-JPEG files or if libjpeg-turbo failed
    if (!thumbnails_generated) {
	thumbnails_generated = true; // Reset the flag
	for (int thumb_size : thumb_sizes) {
	    // Calculate thumbnail dimensions maintaining aspect ratio
	    double aspect_ratio = (double)width / height;
	    int thumb_width, thumb_height;

	    if (width > height) {
		thumb_width = thumb_size;
		thumb_height = (int)(thumb_size / aspect_ratio);
	    } else {
		thumb_height = thumb_size;
		thumb_width = (int)(thumb_size * aspect_ratio);
	    }

	    // Skip if image is already smaller than thumbnail size
	    if (width <= thumb_width && height <= thumb_height) {
		continue;
	    }

	    // Use stb_image_resize for non-JPEG or fallback
	    std::vector<uint8_t> thumb_data;
	    bool thumb_success = false;

	    // Convert to RGB if needed
	    unsigned char* rgb_data = nullptr;
	    bool need_free_rgb = false;

	    if (channels == 3) {
		rgb_data = image_data;
	    } else {
		rgb_data = (unsigned char*)malloc(width * height * 3);
		need_free_rgb = true;

		for (int i = 0; i < width * height; i++) {
		    if (channels == 1) {
			// Grayscale to RGB
			rgb_data[i*3] = rgb_data[i*3+1] = rgb_data[i*3+2] = image_data[i];
		    } else if (channels == 2) {
			// Grayscale + Alpha to RGB (ignore alpha)
			rgb_data[i*3] = rgb_data[i*3+1] = rgb_data[i*3+2] = image_data[i*2];
		    } else if (channels == 4) {
			// RGBA to RGB (ignore alpha)
			rgb_data[i*3] = image_data[i*4];
			rgb_data[i*3+1] = image_data[i*4+1];
			rgb_data[i*3+2] = image_data[i*4+2];
		    }
		}
	    }

	    // Resize image
	    std::vector<uint8_t> resized_rgb(thumb_width * thumb_height * 3);
	    if (stbir_resize_uint8_linear(rgb_data, width, height, 0,
			resized_rgb.data(), thumb_width, thumb_height, 0, STBIR_RGB)) {

		// Encode as JPEG
		thumb_data = encode_jpeg(resized_rgb.data(), thumb_width, thumb_height, 90);
		if (!thumb_data.empty()) {
		    thumb_success = true;
		} else {
		    fprintf(stderr, "Error: Failed to encode JPEG thumbnail for '%s' (size %d)\n", filepath.c_str(), thumb_size);
		}
	    } else {
		fprintf(stderr, "Error: Failed to resize image for '%s' (size %d)\n", filepath.c_str(), thumb_size);
	    }

	    if (need_free_rgb) {
		free(rgb_data);
	    }

	    if (thumb_success && !thumb_data.empty()) {
		std::string thumb_key = std::string(hash_str) + ":" + std::to_string(thumb_size);
		write_tasks.emplace_back(WriteTask::STORE_THUMBNAIL, thumb_key, thumb_data);
	    } else {
		thumbnails_generated = false;
	    }
	}
    }
    // Generate and store center-cropped square thumbnails for Treemap / square mosaic modes
    if (image_data && width > 0 && height > 0) {
        int sq_dim = std::min(width, height);
        int crop_x = (width - sq_dim) / 2;
        int crop_y = (height - sq_dim) / 2;
        const unsigned char* crop_src = image_data + (crop_y * width + crop_x) * 3;

        std::vector<uint8_t> sq128_rgb(128 * 128 * 3);
        stbir_resize_uint8_linear(crop_src, sq_dim, sq_dim, width * 3,
                                 sq128_rgb.data(), 128, 128, 0, STBIR_RGB);
        std::vector<uint8_t> sq128_jpeg = encode_jpeg(sq128_rgb.data(), 128, 128, 85);
        if (!sq128_jpeg.empty()) {
            write_tasks.emplace_back(WriteTask::STORE_THUMBNAIL, std::string(hash_str) + ":sq128", std::move(sq128_jpeg));
        }

        std::vector<uint8_t> sq64_rgb(64 * 64 * 3);
        stbir_resize_uint8_linear(sq128_rgb.data(), 128, 128, 128 * 3,
                                 sq64_rgb.data(), 64, 64, 0, STBIR_RGB);
        std::vector<uint8_t> sq64_jpeg = encode_jpeg(sq64_rgb.data(), 64, 64, 85);
        if (!sq64_jpeg.empty()) {
            write_tasks.emplace_back(WriteTask::STORE_THUMBNAIL, std::string(hash_str) + ":sq64", std::move(sq64_jpeg));
        }
    }

    if (needs_free_stbi && image_data) {
        stbi_image_free(image_data);
    } else if (needs_free_malloc && image_data) {
        free(image_data);
    }
    return thumbnails_generated;
}

void DatabaseManager::worker_thread(const std::vector<std::string>& files, size_t start_idx, size_t end_idx,
	moodycamel::ConcurrentQueue<WriteTask>& write_queue,
	Timer& timer, StatusReporter& reporter,
	std::atomic<int>& processed_count, std::atomic<int>& skipped_count) {

    for (size_t i = start_idx; i < end_idx && !stop_processing_.load(); i++) {
	const std::string& filepath = files[i];

	std::vector<WriteTask> write_tasks;
	bool should_skip = false;

	bool success = process_image_file(filepath, write_tasks, timer, should_skip);

	if (should_skip) {
	    skipped_count.fetch_add(1);
	} else if (success) {
	    // Enqueue all write tasks for this image
	    for (const auto& task : write_tasks) {
		write_queue.enqueue(task);
	    }
	    processed_count.fetch_add(1);
	} else {
	    skipped_count.fetch_add(1);
	}

	// Update progress periodically
	if ((i - start_idx) % 10 == 0) {
	    reporter.set_current_count(processed_count.load() + skipped_count.load());
	}
    }
}

void DatabaseManager::writer_thread(moodycamel::ConcurrentQueue<WriteTask>& write_queue,
	std::atomic<bool>& workers_done,
	Timer& timer, StatusReporter& reporter,
	std::atomic<int>& write_count) {

    constexpr int BATCH_SIZE = 100;
    std::vector<WriteTask> batch;
    batch.reserve(BATCH_SIZE);

    while (!workers_done.load() || write_queue.size_approx() > 0) {
	batch.clear();

	// Dequeue a batch of writes
	WriteTask task;
	for (int i = 0; i < BATCH_SIZE && write_queue.try_dequeue(task); i++) {
	    batch.push_back(std::move(task));
	}

	if (batch.empty()) {
	    std::this_thread::sleep_for(std::chrono::milliseconds(10));
	    continue;
	}

	// Process batch in a single transaction
	{
	    std::lock_guard<std::mutex> lock(db_mutex_);

	    if (!begin_transaction()) {
		fprintf(stderr, "Error: Failed to begin transaction for batch write\n");
		continue;
	    }

	    bool batch_success = true;
	    for (const auto& write_task : batch) {
		bool success = false;

		if (write_task.type == WriteTask::STORE_PATH) {
		    success = store_key_value(write_task.key, write_task.string_value);
		} else if (write_task.type == WriteTask::ADD_PATH_FOR_HASH) {
		    success = add_path_for_hash(write_task.key, write_task.string_value);
		} else if (write_task.type == WriteTask::STORE_THUMBNAIL) {
		    success = store_key_data(write_task.key, write_task.data);
		}

		if (!success) {
		    fprintf(stderr, "Error: Failed to store key '%s'\n", write_task.key.c_str());
		    batch_success = false;
		    break;
		}
	    }

	    if (batch_success) {
		commit_transaction();
		write_count.fetch_add(batch.size());
	    } else {
		abort_transaction();
	    }
	}
    }
}

int DatabaseManager::scan_directory_parallel(const std::string& directory, Timer& timer, 
	StatusReporter& reporter, int num_threads) {
    if (!is_open_) return -1;

    if (num_threads <= 0) {
	num_threads = std::thread::hardware_concurrency();
	if (num_threads == 0) num_threads = 4; // fallback
    }

    timer.start("Directory Scanning");
    reporter.update_status("Scanning directory for images...");

    // Count total image files first
    std::vector<std::string> image_files;
    try {
	fs::recursive_directory_iterator it(directory), end;
	while (it != end) {
	    const auto& entry = *it;
	    std::string path = fs::path(entry.path()).lexically_normal().string();
	    if (entry.is_directory()) {
		if (is_cache_or_db_path(path)) {
		    it.disable_recursion_pending();
		}
	    } else if (entry.is_regular_file()) {
		if (!is_cache_or_db_path(path) && is_image_file(path)) {
		    image_files.push_back(path);
		}
	    }
	    ++it;
	}
    } catch (const fs::filesystem_error& ex) {
	return -1;
    }

    timer.stop("Directory Scanning");

    if (image_files.empty()) {
	reporter.update_status("No image files found");
	return 0;
    }

    reporter.set_total_count(image_files.size());
    reporter.update_status("Processing images in parallel...");

    std::cout << "Using " << num_threads << " worker threads for parallel processing" << std::endl;

    // Initialize parallel processing
    stop_processing_.store(false);
    std::atomic<int> processed_count(0);
    std::atomic<int> skipped_count(0);
    std::atomic<int> write_count(0);
    std::atomic<bool> workers_done(false);

    moodycamel::ConcurrentQueue<WriteTask> write_queue;

    timer.start("Parallel Image Processing");

    // Start writer thread
    std::thread writer_thread_handle(&DatabaseManager::writer_thread, this,
	    std::ref(write_queue), std::ref(workers_done),
	    std::ref(timer), std::ref(reporter), std::ref(write_count));

    // Start worker threads
    std::vector<std::thread> worker_threads;
    size_t files_per_thread = (image_files.size() + num_threads - 1) / num_threads;

    for (int t = 0; t < num_threads; t++) {
	size_t start_idx = t * files_per_thread;
	size_t end_idx = std::min(start_idx + files_per_thread, image_files.size());

	if (start_idx < end_idx) {
	    worker_threads.emplace_back(&DatabaseManager::worker_thread, this,
		    std::ref(image_files), start_idx, end_idx,
		    std::ref(write_queue), std::ref(timer), std::ref(reporter),
		    std::ref(processed_count), std::ref(skipped_count));
	}
    }

    // Wait for all worker threads to complete
    for (auto& worker : worker_threads) {
	worker.join();
    }

    // Signal writer thread that workers are done and wait for it
    workers_done.store(true);
    writer_thread_handle.join();

    timer.stop("Parallel Image Processing");

    reporter.update_status("Scanning complete");
    return processed_count.load();
}

int DatabaseManager::scan_directory(const std::string& directory, Timer& timer, StatusReporter& reporter) {
    return scan_directory_parallel(directory, timer, reporter, 1);
}

static std::vector<std::string> parse_paths(const std::string& val) {
    std::vector<std::string> result;
    if (val.empty()) return result;
    size_t start = 0;
    while (start < val.size()) {
	size_t end = val.find('\n', start);
	if (end == std::string::npos) {
	    std::string p = val.substr(start);
	    if (!p.empty()) result.push_back(p);
	    break;
	}
	std::string p = val.substr(start, end - start);
	if (!p.empty()) result.push_back(p);
	start = end + 1;
    }
    return result;
}

static std::string serialize_paths(const std::vector<std::string>& paths) {
    std::string result;
    for (size_t i = 0; i < paths.size(); ++i) {
	if (i > 0) result += '\n';
	result += paths[i];
    }
    return result;
}

std::string DatabaseManager::extract_hash_from_key(const char* key, size_t key_size) {
    std::string key_str(key, key_size);
    if (key_str.length() > 6 && key_str.substr(key_str.length() - 6) == ":paths") {
	return key_str.substr(0, key_str.length() - 6);
    }
    if (key_str.length() > 5 && key_str.substr(key_str.length() - 5) == ":path") {
	return key_str.substr(0, key_str.length() - 5);
    }
    return "";
}

bool DatabaseManager::has_thumbnails(const std::string& hash) {
    if (hash.empty()) return false;
    bool manage_txn = (txn_ == nullptr);
    if (manage_txn) {
	if (!begin_transaction()) return false;
    }
    std::vector<uint8_t> data;
    bool exists = get_key_data(hash + ":32", data) ||
		  get_key_data(hash + ":64", data) ||
		  get_key_data(hash + ":128", data) ||
		  get_key_data(hash + ":256", data) ||
		  get_key_data(hash + ":512", data) ||
		  get_key_data(hash + ":1024", data);
    if (manage_txn) {
	abort_transaction();
    }
    return exists;
}

std::vector<std::string> DatabaseManager::get_paths_for_hash(const std::string& hash) {
    if (hash.empty()) return {};

    bool manage_txn = (txn_ == nullptr);
    if (manage_txn) {
	if (!begin_transaction()) return {};
    }

    std::string val;
    bool found = get_key_value(hash + ":paths", val);
    if (!found) {
	found = get_key_value(hash + ":path", val);
    }

    if (manage_txn) {
	abort_transaction();
    }

    if (found) {
	return parse_paths(val);
    }
    return {};
}

bool DatabaseManager::set_paths_for_hash(const std::string& hash, const std::vector<std::string>& paths) {
    if (hash.empty()) return false;

    bool manage_txn = (txn_ == nullptr);
    if (manage_txn) {
	if (!begin_transaction()) return false;
    }

    bool success = true;
    if (paths.empty()) {
	delete_key(hash + ":paths");
	delete_key(hash + ":path");
    } else {
	std::string val = serialize_paths(paths);
	success = store_key_value(hash + ":paths", val);
	delete_key(hash + ":path"); // Cleanup legacy key if present
    }

    if (manage_txn) {
	if (success) commit_transaction();
	else abort_transaction();
    }

    return success;
}

bool DatabaseManager::add_path_for_hash(const std::string& hash, const std::string& filepath) {
    if (hash.empty() || filepath.empty()) return false;

    bool manage_txn = (txn_ == nullptr);
    if (manage_txn) {
	if (!begin_transaction()) return false;
    }

    std::vector<std::string> paths = get_paths_for_hash(hash);
    bool exists = false;
    for (const auto& p : paths) {
	if (p == filepath) {
	    exists = true;
	    break;
	}
    }

    bool success = true;
    if (!exists) {
	paths.push_back(filepath);
	success = set_paths_for_hash(hash, paths);
    }

    if (manage_txn) {
	if (success) commit_transaction();
	else abort_transaction();
    }

    return success;
}

bool DatabaseManager::remove_path_for_hash(const std::string& hash, const std::string& filepath) {
    if (hash.empty() || filepath.empty()) return false;

    bool manage_txn = (txn_ == nullptr);
    if (manage_txn) {
	if (!begin_transaction()) return false;
    }

    std::vector<std::string> paths = get_paths_for_hash(hash);
    std::vector<std::string> remaining;
    for (const auto& p : paths) {
	if (p != filepath) {
	    remaining.push_back(p);
	}
    }

    bool success = set_paths_for_hash(hash, remaining);

    if (manage_txn) {
	if (success) commit_transaction();
	else abort_transaction();
    }

    return success;
}

bool DatabaseManager::store_image_metadata(const std::string& hash, const ImageMetadata& meta) {
    std::string key = hash + ":meta";
    std::vector<uint8_t> data(sizeof(ImageMetadata));
    std::memcpy(data.data(), &meta, sizeof(ImageMetadata));
    return store_key_data(key, data);
}

bool DatabaseManager::get_image_metadata(const std::string& hash, ImageMetadata& meta) {
    std::string key = hash + ":meta";
    std::vector<uint8_t> data;
    if (get_key_data(key, data) && data.size() >= sizeof(ImageMetadata)) {
        std::memcpy(&meta, data.data(), sizeof(ImageMetadata));
        return true;
    }
    return false;
}

bool DatabaseManager::load_image_info(const std::string& hash, ImageInfo& info) {
    // Find largest thumbnail size available
    std::vector<int> sizes = {32, 64, 128, 256, 512, 1024, 2048};
    int best_size = 0;
    std::vector<uint8_t> best_data;

    for (int size : sizes) {
	std::string thumb_key = hash + ":" + std::to_string(size);
	std::vector<uint8_t> data;
	if (get_key_data(thumb_key, data)) {
	    best_size = size;
	    best_data = std::move(data);
	}
    }

    if (best_size == 0) {
        if (get_key_data(hash + ":sq128", best_data)) {
            best_size = 128;
        } else if (get_key_data(hash + ":sq64", best_data)) {
            best_size = 64;
        }
    }

    if (best_size == 0) {
	return false;
    }

    // Load thumbnail info to get dimensions and aspect ratio
    int width = 0, height = 0, channels = 0;
    if (!stbi_info_from_memory(best_data.data(), best_data.size(), &width, &height, &channels)) {
	fprintf(stderr, "Error: Failed to get thumbnail info for hash '%s': %s\n", hash.c_str(), stbi_failure_reason());
	return false;
    }

    info.best_thumb_size = best_size;
    info.thumb_data.clear(); // Free memory, we don't need the jpeg data here
    info.thumb_width = width;
    info.thumb_height = height;
    info.aspect_ratio = static_cast<double>(width) / height;
    info.orig_width = width;
    info.orig_height = height;
    info.file_size = 0;
    info.file_timestamp = 0;

    ImageMetadata meta;
    if (get_image_metadata(hash, meta)) {
        info.file_size = meta.file_size;
        info.file_timestamp = meta.file_timestamp;
        if (meta.orig_width > 0 && meta.orig_height > 0) {
            info.orig_width = meta.orig_width;
            info.orig_height = meta.orig_height;
            info.aspect_ratio = static_cast<double>(meta.orig_width) / meta.orig_height;
        }
    }

    return true;
}

std::vector<ImageInfo> DatabaseManager::get_all_images() {
    std::vector<ImageInfo> images;

    if (!is_open_) return images;

    MDB_txn* read_txn;
    MDB_dbi read_dbi;
    MDB_cursor* cursor;

    if (mdb_txn_begin(env_, nullptr, MDB_RDONLY, &read_txn) != 0) {
	return images;
    }

    if (mdb_dbi_open(read_txn, nullptr, 0, &read_dbi) != 0) {
	mdb_txn_abort(read_txn);
	return images;
    }

    if (mdb_cursor_open(read_txn, read_dbi, &cursor) != 0) {
	mdb_txn_abort(read_txn);
	return images;
    }

    std::unordered_set<std::string> seen_paths;
    MDB_val key, data;
    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0) {
	std::string hash = extract_hash_from_key((char*)key.mv_data, key.mv_size);
	if (!hash.empty()) {
	    std::vector<std::string> paths = parse_paths(std::string((char*)data.mv_data, data.mv_size));
	    if (paths.empty()) continue;

	    // Temporarily store current transaction state
	    MDB_txn* temp_txn = txn_;
	    MDB_dbi temp_dbi = dbi_;

	    // Use read transaction for loading image info
	    txn_ = read_txn;
	    dbi_ = read_dbi;

	    ImageInfo base_info;
	    base_info.hash = hash;

	    if (load_image_info(hash, base_info)) {
		for (const auto& p : paths) {
		    if (seen_paths.insert(p).second) {
			ImageInfo info = base_info;
			info.path = p;
			images.push_back(std::move(info));
		    }
		}
	    } else {
		fprintf(stderr, "Warning: Failed to load image info for hash '%s'\n", hash.c_str());
	    }

	    // Restore transaction state
	    txn_ = temp_txn;
	    dbi_ = temp_dbi;
	}
    }

    mdb_cursor_close(cursor);
    mdb_txn_abort(read_txn);

    // Sort images alphabetically by path
    std::sort(images.begin(), images.end(),
	    [](const ImageInfo& a, const ImageInfo& b) {
	    return a.path < b.path;
	    });

    return images;
}

bool DatabaseManager::get_hash_for_path(const std::string& filepath, std::string& hash_out) {
	return get_key_value("file:" + filepath, hash_out);
}

int DatabaseManager::get_exif_orientation(const std::string& filepath) {
    // Only process JPEG files
    auto ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext != ".jpg" && ext != ".jpeg") {
	return 1; // Default orientation (no transform needed)
    }

    // Read file into memory
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
	return 1;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read entire file
    std::vector<uint8_t> buffer(fileSize);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();

    if (buffer.empty()) {
	return 1;
    }

    // Parse EXIF data using TinyEXIF
    TinyEXIF::EXIFInfo exifInfo(buffer.data(), static_cast<unsigned>(buffer.size()));

    if (exifInfo.Orientation == 0) {
	return 1; // Default orientation if not specified in EXIF
    }

    return exifInfo.Orientation;
}

// Apply orientation transform to image data (rotates/flips the RGB data in-place)
void DatabaseManager::apply_orientation_transform(unsigned char* data, int& width, int& height, int orientation) {
    if (orientation <= 1 || orientation > 8) {
	return; // No transform needed or invalid orientation
    }

    int channels = 3; // Assuming RGB data

    // Create a copy of the original data for transformations that need it
    std::vector<unsigned char> original_data;
    bool needs_copy = (orientation >= 2); // All orientations except 1 need transformations

    if (needs_copy) {
	original_data.assign(data, data + width * height * channels);
    }

    switch (orientation) {
	case 1:
	    // Normal - no transformation needed
	    break;

	case 2:
	    // Horizontal flip
	    for (int y = 0; y < height; y++) {
		for (int x = 0; x < width / 2; x++) {
		    int left_idx = (y * width + x) * channels;
		    int right_idx = (y * width + (width - 1 - x)) * channels;

		    // Swap pixels
		    for (int c = 0; c < channels; c++) {
			std::swap(data[left_idx + c], data[right_idx + c]);
		    }
		}
	    }
	    break;

	case 3:
	    // 180 degree rotation
	    for (int i = 0; i < width * height / 2; i++) {
		int src_idx = i * channels;
		int dst_idx = (width * height - 1 - i) * channels;

		for (int c = 0; c < channels; c++) {
		    std::swap(data[src_idx + c], data[dst_idx + c]);
		}
	    }
	    break;

	case 4:
	    // Vertical flip
	    for (int y = 0; y < height / 2; y++) {
		for (int x = 0; x < width; x++) {
		    int top_idx = (y * width + x) * channels;
		    int bottom_idx = ((height - 1 - y) * width + x) * channels;

		    // Swap pixels
		    for (int c = 0; c < channels; c++) {
			std::swap(data[top_idx + c], data[bottom_idx + c]);
		    }
		}
	    }
	    break;

	case 5:
	case 6:
	case 7:
	case 8: {
		    // These require 90-degree rotations, which change dimensions
		    int new_width, new_height;

		    if (orientation == 6 || orientation == 8) {
			// 90-degree rotations swap dimensions
			new_width = height;
			new_height = width;
		    } else {
			// Transpose operations keep dimensions
			new_width = height;
			new_height = width;
		    }

		    std::vector<unsigned char> rotated_data(new_width * new_height * channels);

		    for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
			    int src_idx = (y * width + x) * channels;
			    int dst_x, dst_y;

			    switch (orientation) {
				case 5: // Transpose + horizontal flip
				    dst_x = y;
				    dst_y = width - 1 - x;
				    break;
				case 6: // 90 degree clockwise rotation
				    dst_x = height - 1 - y;
				    dst_y = x;
				    break;
				case 7: // Transpose + vertical flip
				    dst_x = height - 1 - y;
				    dst_y = width - 1 - x;
				    break;
				case 8: // 90 degree counter-clockwise rotation
				    dst_x = y;
				    dst_y = width - 1 - x;
				    break;
			    }

			    int dst_idx = (dst_y * new_width + dst_x) * channels;

			    for (int c = 0; c < channels; c++) {
				rotated_data[dst_idx + c] = original_data[src_idx + c];
			    }
			}
		    }

		    // Copy rotated data back
		    std::copy(rotated_data.begin(), rotated_data.end(), data);

		    // Update dimensions
		    width = new_width;
		    height = new_height;
		    break;
		}
    }
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
