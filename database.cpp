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

// Third-party dependencies
#include "xxhash.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include <jpeglib.h>
#include <jerror.h>
#include "TinyEXIF.h"

#define MAX_DB_SIZE 549755813888

namespace fs = std::filesystem;
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

    unsigned int flags = MDB_NOSUBDIR;
    if (is_new_db) {
	flags |= MDB_NOSYNC; // Use MDB_NOSYNC for faster bulk insert on new DB
    }

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
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
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
    std::vector<int> thumb_sizes = {32, 64, 128, 256, 512, 1024};

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

    // Load image with stb_image
    int width, height, channels;
    unsigned char* image_data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
    if (!image_data) {
	fprintf(stderr, "Error: Failed to load image '%s': %s\n", filepath.c_str(), stbi_failure_reason());
	should_skip = true;
	return false;
    }

    // Check for zero width or height
    if (width <= 0 || height <= 0) {
	fprintf(stderr, "Error: Invalid image dimensions for '%s': %dx%d\n", filepath.c_str(), width, height);
	stbi_image_free(image_data);
	should_skip = true;
	return false;
    }

    // Read EXIF orientation and apply transformation if needed
    int orientation = get_exif_orientation(filepath);
    if (orientation > 1) {
	// Convert to RGB if not already (needed for orientation transforms)
	unsigned char* rgb_data = nullptr;
	bool allocated_rgb = false;

	if (channels == 3) {
	    // For RGB data, create a copy for transformation
	    rgb_data = (unsigned char*)malloc(width * height * 3);
	    allocated_rgb = true;
	    memcpy(rgb_data, image_data, width * height * 3);
	} else {
	    // Convert to RGB for orientation processing
	    rgb_data = (unsigned char*)malloc(width * height * 3);
	    allocated_rgb = true;

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

	// Apply orientation transformation
	apply_orientation_transform(rgb_data, width, height, orientation);

	// Replace the original image data
	stbi_image_free(image_data);
	image_data = rgb_data;
	channels = 3;
    }

    // Compute content hash
    size_t data_size = width * height * channels;
    XXH64_hash_t hash = XXH64(image_data, data_size, 0);
    char hash_str[17];
    snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hash);

    // Check if this hash already exists (duplicate detection)
    std::string path_key = std::string(hash_str) + ":path";
    std::string existing_path;

    // Thread-safe check for existing path
    {
	std::lock_guard<std::mutex> lock(db_mutex_);
	if (!begin_transaction()) {
	    stbi_image_free(image_data);
	    should_skip = true;
	    return false;
	}

	bool exists = get_key_value(path_key, existing_path);
	abort_transaction();

	if (exists) {
	    stbi_image_free(image_data);
	    should_skip = true;
	    return true; // Not an error, just a duplicate
	}
    }

    // Store file path as write task
    write_tasks.emplace_back(WriteTask::STORE_PATH, path_key, filepath);
    write_tasks.emplace_back(WriteTask::STORE_PATH, "file:" + filepath, std::string(hash_str));

    // Generate and store thumbnails
    std::vector<int> thumb_sizes = {32, 64, 128, 256, 512, 1024};
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

    stbi_image_free(image_data);
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
	for (const auto& entry : fs::recursive_directory_iterator(directory)) {
	    if (entry.is_regular_file() && is_image_file(entry.path().string())) {
		image_files.push_back(entry.path().string());
	    }
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

    timer.start("Directory Scanning");
    reporter.update_status("Scanning directory for images...");

    // Count total image files first
    std::vector<std::string> image_files;
    try {
	for (const auto& entry : fs::recursive_directory_iterator(directory)) {
	    if (entry.is_regular_file() && is_image_file(entry.path().string())) {
		image_files.push_back(entry.path().string());
	    }
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
    reporter.update_status("Processing images...");

    if (!begin_transaction()) {
	return -1;
    }

    int processed_count = 0;
    int skipped_count = 0;

    timer.start("Image Processing");

    for (size_t i = 0; i < image_files.size(); i++) {
	const std::string& filepath = image_files[i];
	reporter.set_current_count(i + 1);

	timer.start("Image Loading");

	// Load image with stb_image
	int width, height, channels;
	unsigned char* image_data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
	if (!image_data) {
	    fprintf(stderr, "Error: Failed to load image '%s': %s\n", filepath.c_str(), stbi_failure_reason());
	    skipped_count++;
	    timer.stop("Image Loading");
	    continue;
	}

	// Check for zero width or height
	if (width <= 0 || height <= 0) {
	    fprintf(stderr, "Error: Invalid image dimensions for '%s': %dx%d\n", filepath.c_str(), width, height);
	    stbi_image_free(image_data);
	    skipped_count++;
	    timer.stop("Image Loading");
	    continue;
	}

	timer.stop("Image Loading");
	timer.start("Hash Computation");

	// Compute content hash
	size_t data_size = width * height * channels;
	XXH64_hash_t hash = XXH64(image_data, data_size, 0);
	char hash_str[17];
	snprintf(hash_str, sizeof(hash_str), "%016llx", (unsigned long long)hash);

	timer.stop("Hash Computation");

	// Check if this hash already exists (duplicate detection)
	std::string path_key = std::string(hash_str) + ":path";
	std::string existing_path;
	if (get_key_value(path_key, existing_path)) {
	    stbi_image_free(image_data);
	    skipped_count++;
	    continue;
	}

	timer.start("Database Update");

	// Store file path and reverse lookup
	if (!store_key_value(path_key, filepath) || !store_key_value("file:" + filepath, std::string(hash_str))) {
	    stbi_image_free(image_data);
	    continue;
	}

	timer.stop("Database Update");
	timer.start("Thumbnail Generation");

	// Generate and store thumbnails
	bool thumbnails_ok = generate_thumbnails(filepath, hash_str, image_data, width, height, channels);

	timer.stop("Thumbnail Generation");

	stbi_image_free(image_data);

	if (thumbnails_ok) {
	    processed_count++;
	} else {
	    skipped_count++;
	}
    }

    timer.stop("Image Processing");
    timer.start("Database Commit");

    bool success = commit_transaction();

    timer.stop("Database Commit");

    if (!success) {
	return -1;
    }

    reporter.update_status("Scanning complete");
    return processed_count;
}

std::string DatabaseManager::extract_hash_from_key(const char* key, size_t key_size) {
    std::string key_str(key, key_size);
    if (key_str.length() > 5 && key_str.substr(key_str.length() - 5) == ":path") {
	return key_str.substr(0, key_str.length() - 5);
    }
    return "";
}

bool DatabaseManager::load_image_info(const std::string& hash, ImageInfo& info) {
    // Find largest thumbnail size available
    std::vector<int> sizes = {32, 64, 128, 256, 512, 1024};
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

    MDB_val key, data;
    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0) {
	std::string hash = extract_hash_from_key((char*)key.mv_data, key.mv_size);
	if (!hash.empty()) {
	    ImageInfo info;
	    info.hash = hash;
	    info.path = std::string((char*)data.mv_data, data.mv_size);

	    // Temporarily store current transaction state
	    MDB_txn* temp_txn = txn_;
	    MDB_dbi temp_dbi = dbi_;

	    // Use read transaction for loading image info
	    txn_ = read_txn;
	    dbi_ = read_dbi;

	    if (load_image_info(hash, info)) {
		images.push_back(std::move(info));
	    } else {
		fprintf(stderr, "Warning: Failed to load image info for '%s' (hash: %s)\n", info.path.c_str(), hash.c_str());
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
