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
#include "work_budget.h"
#include "sift_feature.h"
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <unordered_set>
#include <unordered_map>
#include <sys/stat.h>

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

struct DatabaseEnvironment {
    MDB_env* env = nullptr;
    MDB_dbi dbi = 0;
    ~DatabaseEnvironment() { if (env) mdb_env_close(env); }
};

namespace {
std::mutex environment_mutex;
std::unordered_map<std::string, std::weak_ptr<DatabaseEnvironment>> environments;

std::string database_identity(const std::string& path) {
    struct stat status{};
    if (stat(path.c_str(), &status) != 0) return "path:" + path;
    return "inode:" + std::to_string(status.st_dev) + ":" + std::to_string(status.st_ino);
}

// Local read handles cannot accidentally replace a connection's write transaction.
struct ReadTransaction {
    MDB_txn* txn = nullptr;
    explicit ReadTransaction(MDB_env* env) {
        if (env) mdb_txn_begin(env, nullptr, MDB_RDONLY, &txn);
    }
    ~ReadTransaction() { if (txn) mdb_txn_abort(txn); }
};
}
DatabaseManager::DatabaseManager() : env_(nullptr), txn_(nullptr), dbi_(0), is_open_(false), stop_processing_(false) {
}

DatabaseManager::~DatabaseManager() {
    close();
}

bool DatabaseManager::open(const std::string& db_path) {
    close();
    std::error_code ec;
    const auto canonical = fs::weakly_canonical(fs::absolute(db_path, ec), ec).string();
    if (ec || canonical.empty()) return false;
    std::lock_guard<std::mutex> lock(environment_mutex);
    const std::string identity = database_identity(canonical);
    auto existing = environments.find(identity);
    if (existing != environments.end()) environment_ = existing->second.lock();
    if (!environment_) {
        auto owner = std::make_shared<DatabaseEnvironment>();
        if (mdb_env_create(&owner->env) != 0 ||
            mdb_env_set_mapsize(owner->env, MAX_DB_SIZE) != 0 ||
            mdb_env_set_maxreaders(owner->env, 512) != 0) return false;
        // MDB_NOTLS allows independent scoped reads on a shared environment,
        // including a read while this thread has another connection's writer.
        if (mdb_env_open(owner->env, canonical.c_str(), MDB_NOSUBDIR | MDB_NOTLS, 0664) != 0)
            return false;
        MDB_txn* setup = nullptr;
        if (mdb_txn_begin(owner->env, nullptr, MDB_RDONLY, &setup) != 0) return false;
        const int rc = mdb_dbi_open(setup, nullptr, 0, &owner->dbi);
        mdb_txn_abort(setup);
        if (rc != 0) return false;
        environment_ = std::move(owner);
        // Re-stat after creating a new file so aliases/hard links share the owner.
        for (auto it = environments.begin(); it != environments.end();) {
            if (it->second.expired()) it = environments.erase(it); else ++it;
        }
        environments[database_identity(canonical)] = environment_;
    }
    env_ = environment_->env;
    dbi_ = environment_->dbi;
    is_open_ = true;
    return true;
}

void DatabaseManager::close() {
    abort_transaction();
    env_ = nullptr;
    dbi_ = 0;
    is_open_ = false;
    // Serialize the last close with a concurrent reopen of the same file.
    std::lock_guard<std::mutex> lock(environment_mutex);
    environment_.reset();
}

bool DatabaseManager::record_write_result(int error) {
    if (error && !transaction_error_) transaction_error_ = error;
    return error == 0;
}

bool DatabaseManager::begin_transaction() {
    if (!is_open_ || txn_) return false;

    transaction_error_ = 0;
    int rc = mdb_txn_begin(env_, nullptr, 0, &txn_);
    if (rc != 0) {
	return false;
    }

    // DBI is already opened in DatabaseManager::open(), so we don't need to open it again
    return true;
}

bool DatabaseManager::commit_transaction() {
    if (!txn_) return false;

    if (transaction_error_) {
        abort_transaction();
        return false;
    }
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

    return record_write_result(mdb_put(txn_, dbi_, &k, &v, 0));
}

bool DatabaseManager::store_key_data(const std::string& key, const std::vector<uint8_t>& data) {
    if (!txn_) return false;

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();
    v.mv_data = (void*)data.data();
    v.mv_size = data.size();

    return record_write_result(mdb_put(txn_, dbi_, &k, &v, 0));
}

bool DatabaseManager::delete_key(const std::string& key) {
    if (!txn_) return false;

    MDB_val k;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();

    int rc = mdb_del(txn_, dbi_, &k, nullptr);
    return record_write_result(rc == MDB_NOTFOUND ? 0 : rc);
}

bool DatabaseManager::get_key_value(const std::string& key, std::string& value) {
    if (!txn_) {
        return get_key_value_concurrent(key, value);
    }

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();

    if (mdb_get(txn_, dbi_, &k, &v) == 0) {
	value.assign((char*)v.mv_data, v.mv_size);
	return true;
    }
    return false;
}

bool DatabaseManager::get_key_value_concurrent(const std::string& key, std::string& value) const {
    if (!is_open_ || !env_) return false;

    ReadTransaction read(env_);
    if (!read.txn) return false;
    MDB_txn* read_txn = read.txn;
    const MDB_dbi dbi = dbi_;

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();

    bool found = false;
    if (mdb_get(read_txn, dbi, &k, &v) == 0) {
        value.assign((char*)v.mv_data, v.mv_size);
        found = true;
    }

    return found;
}

bool DatabaseManager::get_key_data(const std::string& key, std::vector<uint8_t>& data) {
    if (!txn_) {
        return get_key_data_concurrent(key, data);
    }

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

    ReadTransaction read(env_);
    if (!read.txn) return false;
    MDB_txn* read_txn = read.txn;
    const MDB_dbi dbi = dbi_;

    MDB_val k, v;
    k.mv_data = (void*)key.c_str();
    k.mv_size = key.length();

    bool found = false;
    if (mdb_get(read_txn, dbi, &k, &v) == 0) {
        data.assign((uint8_t*)v.mv_data, (uint8_t*)v.mv_data + v.mv_size);
        found = true;
    }

    return found;
}

bool DatabaseManager::get_hash_for_path_concurrent(const std::string& filepath, std::string& hash_out) const {
    return get_key_value_concurrent("file:" + filepath, hash_out);
}

bool DatabaseManager::process_image_file(const std::string& filepath, 
	std::vector<WriteTask>& write_tasks,
	Timer& timer, bool& should_skip) {
    WorkPermit work(&stop_processing_);
    if (!work) return false;
    should_skip = false;
    FileStamp before, after;
    std::string hash_str, policy;
    if (get_valid_hash_for_path(filepath, hash_str) &&
        get_key_value_concurrent(hash_str + ":render-policy", policy) && policy == "2") {
        should_skip = true;
        return true;
    }
    if (!read_file_stamp(filepath, before) || !compute_file_hash(filepath, hash_str, &stop_processing_)) return false;
    auto decoded = decode_image(filepath, 2048, 2048, &stop_processing_);
    if (!decoded || !read_file_stamp(filepath, after) || before != after) return false;
    const int width = decoded.width, height = decoded.height;
    const auto* image_data = decoded.rgb.data();
    ImageMetadata meta{before.size, static_cast<uint64_t>(before.modified_ns / 1000000000),
        decoded.source.width, decoded.source.height};
    std::vector<uint8_t> meta_bytes(sizeof(meta));
    std::memcpy(meta_bytes.data(), &meta, sizeof(meta));
    write_tasks.emplace_back(WriteTask::STORE_THUMBNAIL, hash_str + ":meta", meta_bytes);
    bool thumbnails_generated = true;
    for (int size : {32, 64, 128, 256, 512, 1024, 2048}) {
        const double scale = std::min({1.0, double(size) / width, double(size) / height});
        const int tw = std::max(1, int(width * scale)), th = std::max(1, int(height * scale));
        std::vector<uint8_t> rgb(size_t(tw) * th * 3);
        if (!stbir_resize_uint8_linear(image_data, width, height, 0, rgb.data(), tw, th, 0, STBIR_RGB)) return false;
        auto jpeg = encode_jpeg(rgb.data(), tw, th, 90);
        if (jpeg.empty()) return false;
        write_tasks.emplace_back(WriteTask::STORE_THUMBNAIL, hash_str + ":" + std::to_string(size), jpeg);
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

        // Generate and store SIFT features for visual similarity search (512px max dimension)
        int target_sift_dim = std::min(512, std::max(width, height));
        int sift_w, sift_h;
        double aspect_ratio = (double)width / height;
        if (width > height) {
            sift_w = target_sift_dim;
            sift_h = std::max(1, (int)(target_sift_dim / aspect_ratio));
        } else {
            sift_h = target_sift_dim;
            sift_w = std::max(1, (int)(target_sift_dim * aspect_ratio));
        }
        std::vector<uint8_t> sift_rgb(sift_w * sift_h * 3);
        if (stbir_resize_uint8_linear(image_data, width, height, 0,
                                     sift_rgb.data(), sift_w, sift_h, 0, STBIR_RGB)) {
            SiftFeatureData sift_data;
            if (SiftFeatureEngine::extract_from_rgb(sift_rgb.data(), sift_w, sift_h, sift_data)) {
                std::vector<uint8_t> sift_bytes = sift_data.serialize();
                if (!sift_bytes.empty()) {
                    write_tasks.emplace_back(WriteTask::STORE_THUMBNAIL, std::string(hash_str) + ":sift", std::move(sift_bytes));
                }
            }
        }
    }

    write_tasks.emplace_back(WriteTask::STORE_PATH, hash_str + ":render-policy", "2");
    WriteTask record(WriteTask::STORE_FILE_RECORD, filepath, hash_str);
    record.stamp = before;
    write_tasks.push_back(std::move(record));
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
        WriteTransaction transaction(*this);
	    if (!transaction) {
		fprintf(stderr, "Error: Failed to begin transaction for batch write\n");
        stop_processing_ = true;
        break;
	    }

	    bool batch_success = true;
	    for (const auto& write_task : batch) {
		bool success = false;

        if (write_task.type == WriteTask::STORE_FILE_RECORD) {
            FileStamp now;
            // A queued write must not resurrect a deleted or replaced path.
            success = !read_file_stamp(write_task.key, now) || now != write_task.stamp ||
                store_file_record(write_task.key, write_task.string_value, write_task.stamp);
        } else if (write_task.type == WriteTask::STORE_PATH) {
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

        if (batch_success && transaction.commit()) write_count.fetch_add(batch.size());
        else { stop_processing_ = true; break; }
	}
    }
}

int DatabaseManager::scan_directory_parallel(const std::string& directory, Timer& timer, 
	StatusReporter& reporter, int num_threads, bool recursive) {
    if (!is_open_) return -1;

    timer.start("Directory Scanning");
    reporter.update_status("Scanning directory for images...");

    // Count total image files first
    std::vector<std::string> image_files;
    try {
	if (recursive) {
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
	} else {
	    fs::directory_iterator it(directory), end;
	    while (it != end) {
		const auto& entry = *it;
		std::string path = fs::path(entry.path()).lexically_normal().string();
		if (entry.is_regular_file() && !is_cache_or_db_path(path) && is_image_file(path)) {
		    image_files.push_back(path);
		}
		++it;
	    }
	}
    } catch (const fs::filesystem_error& ex) {
	return -1;
    }

    timer.stop("Directory Scanning");
    return scan_files_parallel(image_files, timer, reporter, num_threads);
}

int DatabaseManager::scan_files_parallel(const std::vector<std::string>& image_files, Timer& timer,
	StatusReporter& reporter, int num_threads) {
    if (!is_open_) return -1;

    if (num_threads <= 0) {
	num_threads = WorkPermit::limit();
	if (num_threads == 0) num_threads = 4; // fallback
    }

    num_threads = std::clamp(num_threads, 1, int(WorkPermit::limit()));

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
    return stop_processing_.load() ? -1 : processed_count.load();
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

std::vector<std::string> DatabaseManager::get_paths_for_hash(const std::string& hash) {
    if (hash.empty()) return {};

    std::string val;
    bool found = get_key_value(hash + ":paths", val);
    if (!found) {
	found = get_key_value(hash + ":path", val);
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
	if (success) success = commit_transaction();
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
	if (success) success = commit_transaction();
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
	if (success) success = commit_transaction();
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

namespace {
bool load_image_info_in_txn(MDB_txn* txn, MDB_dbi dbi, const std::string& hash, ImageInfo& info) {
    if (!txn) return false;
    info.hash = hash;
    info.thumb_data.clear();

    auto get_val = [&](const std::string& key_str, MDB_val& val) -> bool {
        MDB_val key;
        key.mv_data = const_cast<char*>(key_str.c_str());
        key.mv_size = key_str.size();
        return mdb_get(txn, dbi, &key, &val) == 0;
    };

    MDB_val val{};
    if (get_val(hash + ":meta", val) && val.mv_size >= sizeof(ImageMetadata)) {
        ImageMetadata meta{};
        std::memcpy(&meta, val.mv_data, sizeof(ImageMetadata));
        if (meta.orig_width > 0 && meta.orig_height > 0) {
            info.orig_width = meta.orig_width;
            info.orig_height = meta.orig_height;
            info.aspect_ratio = static_cast<double>(meta.orig_width) / meta.orig_height;
            info.file_size = meta.file_size;
            info.file_timestamp = meta.file_timestamp;
            info.thumb_width = meta.orig_width;
            info.thumb_height = meta.orig_height;
            info.best_thumb_size = 128;
            return true;
        }
    }

    for (int size : {32, 64, 128, 256, 512, 1024, 2048}) {
        if (get_val(hash + ":" + std::to_string(size), val) && val.mv_size > 0) {
            int width = 0, height = 0, channels = 0;
            if (stbi_info_from_memory(static_cast<const stbi_uc*>(val.mv_data),
                                      static_cast<int>(val.mv_size),
                                      &width, &height, &channels) &&
                width > 0 && height > 0) {
                info.best_thumb_size = size;
                info.thumb_width = width;
                info.thumb_height = height;
                info.aspect_ratio = static_cast<double>(width) / height;
                info.orig_width = width;
                info.orig_height = height;
                info.file_size = 0;
                info.file_timestamp = 0;
                return true;
            }
        }
    }

    if (get_val(hash + ":sq128", val) || get_val(hash + ":sq64", val)) {
        info.best_thumb_size = 128;
        info.thumb_width = 128;
        info.thumb_height = 128;
        info.aspect_ratio = 1.0;
        info.orig_width = 128;
        info.orig_height = 128;
        info.file_size = 0;
        info.file_timestamp = 0;
        return true;
    }

    return false;
}
} // namespace

bool DatabaseManager::load_image_info(const std::string& hash, ImageInfo& info) {
    if (!is_open_ || !env_) return false;
    MDB_txn* active_txn = txn_;
    ReadTransaction read(active_txn ? nullptr : env_);
    if (!active_txn) active_txn = read.txn;
    return load_image_info_in_txn(active_txn, dbi_, hash, info);
}

std::vector<ImageInfo> DatabaseManager::get_images_for_directory(const std::string& directory) {
    std::vector<ImageInfo> images;

    if (!is_open_ || !env_) return images;

    std::string exact_dir;
    std::string prefix;
    if (!directory.empty()) {
        exact_dir = normalize_path(directory);
        prefix = exact_dir;
        if (!prefix.empty() && prefix.back() != '/' && prefix.back() != '\\') {
            prefix += '/';
        }
    }

    ReadTransaction read(env_);
    if (!read.txn) return images;

    MDB_cursor* cursor = nullptr;
    if (mdb_cursor_open(read.txn, dbi_, &cursor) != 0) {
        return images;
    }

    // Fast path: B-Tree prefix range seek on "file:" keys
    std::string start_key = prefix.empty() ? "file:" : ("file:" + (exact_dir.empty() ? prefix : exact_dir));
    MDB_val key, data;
    key.mv_data = (void*)start_key.c_str();
    key.mv_size = start_key.length();

    std::unordered_set<std::string> seen_paths;
    int rc = mdb_cursor_get(cursor, &key, &data, MDB_SET_RANGE);
    while (rc == 0) {
        std::string_view k((const char*)key.mv_data, key.mv_size);
        if (k.rfind("file:", 0) != 0) {
            // Reached end of "file:" key namespace
            break;
        }

        std::string_view file_path = k.substr(5);
        if (!prefix.empty() && file_path.rfind(prefix, 0) != 0 && file_path != exact_dir) {
            // Because B-Tree keys are lexicographically sorted, once file_path > prefix we are done
            if (file_path > prefix) {
                break;
            }
        }

        if (prefix.empty() || file_path.rfind(prefix, 0) == 0 || file_path == exact_dir) {
            std::string norm_p(file_path);
            if (seen_paths.insert(norm_p).second) {
                std::string hash((const char*)data.mv_data, data.mv_size);
                ImageInfo info;
                info.path = std::move(norm_p);
                info.hash = hash;
                if (load_image_info_in_txn(read.txn, dbi_, hash, info)) {
                    images.push_back(std::move(info));
                }
            }
        }

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    bool is_fast_path = !images.empty();

    // Fallback for legacy DBs without "file:" keys
    if (images.empty()) {
        rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
        while (rc == 0) {
            std::string_view k((const char*)key.mv_data, key.mv_size);
            std::string hash;
            if (k.length() > 6 && k.substr(k.length() - 6) == ":paths") {
                hash = std::string(k.substr(0, k.length() - 6));
            } else if (k.length() > 5 && k.substr(k.length() - 5) == ":path") {
                hash = std::string(k.substr(0, k.length() - 5));
            }

            if (!hash.empty()) {
                std::vector<std::string> paths = parse_paths(std::string((char*)data.mv_data, data.mv_size));
                for (const auto& p : paths) {
                    std::string norm_p = normalize_path(p);
                    if (prefix.empty() || norm_p.rfind(prefix, 0) == 0 || norm_p == exact_dir) {
                        if (seen_paths.insert(norm_p).second) {
                            ImageInfo info;
                            info.path = norm_p;
                            info.hash = hash;
                            if (load_image_info_in_txn(read.txn, dbi_, hash, info)) {
                                images.push_back(std::move(info));
                            }
                        }
                    }
                }
            }
            rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
        }
    }

    mdb_cursor_close(cursor);

    if (!is_fast_path) {
        std::sort(images.begin(), images.end(),
                  [](const ImageInfo& a, const ImageInfo& b) {
                      return a.path < b.path;
                  });
    }

    return images;
}

std::vector<ImageInfo> DatabaseManager::get_all_images() {
    return get_images_for_directory("");
}

bool DatabaseManager::get_hash_for_path(const std::string& filepath, std::string& hash_out) {
	return get_key_value("file:" + filepath, hash_out);
}

int DatabaseManager::garbage_collect(const std::string& tile_cache_dir,
        std::function<void(int, int, int)> progress_cb, std::atomic<bool>* stop_flag) {
    if (!is_open_) return -1;
    std::map<std::string, std::vector<std::string>> candidates;
    {
        ReadTransaction read(env_);
        if (!read.txn) return -1;
        MDB_cursor* cursor = nullptr;
        if (mdb_cursor_open(read.txn, dbi_, &cursor)) return -1;
        MDB_val key, value;
        int rc = mdb_cursor_get(cursor, &key, &value, MDB_FIRST);
        while (!rc) {
            if (stop_flag && stop_flag->load()) { mdb_cursor_close(cursor); return 0; }
            const std::string k(static_cast<char*>(key.mv_data), key.mv_size);
            const std::string v(static_cast<char*>(value.mv_data), value.mv_size);
            if (k.rfind("file:", 0) == 0) candidates[v].push_back(k.substr(5));
            else if (k.size() > 6 && k.compare(k.size() - 6, 6, ":paths") == 0) candidates[k.substr(0, k.size() - 6)];
            else if (k.size() > 5 && k.compare(k.size() - 5, 5, ":path") == 0) candidates[k.substr(0, k.size() - 5)];
            rc = mdb_cursor_get(cursor, &key, &value, MDB_NEXT);
        }
        mdb_cursor_close(cursor);
        if (rc != MDB_NOTFOUND) return -1;
    }
    int checked = 0, pruned = 0;
    for (const auto& candidate : candidates) {
        if (stop_flag && stop_flag->load()) break;
        if (progress_cb) progress_cb(checked, candidates.size(), pruned);
        const auto& hash = candidate.first;
        // Re-read the current path set under the writer lock. A snapshot taken
        // before a scan/rename must never replace newer reverse mappings.
        WriteTransaction tx(*this);
        if (!tx) return -1;
        auto paths = get_paths_for_hash(hash);
        paths.insert(paths.end(), candidate.second.begin(), candidate.second.end());
        std::sort(paths.begin(), paths.end()); paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        std::vector<std::string> retained;
        for (const auto& path : paths) {
            std::string mapped;
            const bool mapped_here = get_hash_for_path(path, mapped) && mapped == hash;
            if (!mapped.empty() && mapped != hash) continue;
            std::error_code ec;
            const auto status = fs::status(path, ec);
            const bool missing = ec == std::errc::no_such_file_or_directory ||
                ec == std::errc::not_a_directory || (!ec && !fs::exists(status));
            if (missing) {
                if (mapped_here && !remove_file_record(path)) return -1;
            } else {
                // Permission, I/O and mount errors are not evidence of deletion.
                retained.push_back(path);
            }
        }
        const bool removed = retained.empty();
        if (removed) {
            for (const char* suffix : {"paths", "path", "meta", "32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "sq64", "sq128", "sift", "render-policy"})
                if (!delete_key(hash + ":" + suffix)) return -1;
        } else if (!set_paths_for_hash(hash, retained)) return -1;
        if (!tx.commit()) return -1;
        if (removed) {
            ++pruned;
            // Cache keys are names, never filesystem paths.
            if (!tile_cache_dir.empty() && !hash.empty() && hash != "." && hash != ".." &&
                hash.find_first_of("/\\") == std::string::npos) {
                std::error_code ec;
                fs::remove_all(fs::path(tile_cache_dir) / "tiles" / hash, ec);
                if (ec) return -1;
            }
        }
        ++checked;
    }
    if (progress_cb) progress_cb(checked, candidates.size(), pruned);
    return pruned;
}

bool DatabaseManager::store_sift_features(const std::string& hash, const SiftFeatureData& data) {
    if (!is_open_) return false;
    std::string key = hash + ":sift";
    std::vector<uint8_t> bytes = data.serialize();
    if (bytes.empty()) return false;

    WriteTransaction transaction(*this);
    if (!transaction) return false;
    return store_key_data(key, bytes) && transaction.commit();
}

bool DatabaseManager::get_sift_features(const std::string& hash, SiftFeatureData& out_data) const {
    std::string key = hash + ":sift";
    std::vector<uint8_t> data;
    if (get_key_data_concurrent(key, data) && !data.empty()) {
        return SiftFeatureData::deserialize(data.data(), data.size(), out_data);
    }
    return false;
}

bool DatabaseManager::extract_sift_from_cached_thumbnail(const std::string& hash, SiftFeatureData& out_data, int preferred_size) {
    if (hash.empty()) return false;

    // Check if already in DB
    if (get_sift_features(hash, out_data)) {
        return true;
    }

    // Check candidate thumbnails in priority order based on user setting
    std::vector<std::string> thumb_keys;
    if (preferred_size >= 512) {
        thumb_keys = {hash + ":512", hash + ":256", hash + ":sq128", hash + ":128", hash + ":1024", hash + ":64"};
    } else {
        thumb_keys = {hash + ":256", hash + ":512", hash + ":sq128", hash + ":128", hash + ":64"};
    }

    std::vector<uint8_t> jpeg_bytes;
    for (const auto& k : thumb_keys) {
        if (get_key_data_concurrent(k, jpeg_bytes) && !jpeg_bytes.empty()) {
            if (SiftFeatureEngine::extract_from_jpeg_bytes(jpeg_bytes.data(), jpeg_bytes.size(), out_data)) {
                store_sift_features(hash, out_data);
                return true;
            }
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


bool DatabaseManager::get_valid_hash_for_path(const std::string& filepath, std::string& hash_out,
                                              FileStamp* stamp_out) const {
    FileStamp current;
    if (!read_file_stamp(filepath, current)) return false;
    std::string value;
    if (!get_key_value_concurrent("fileinfo:" + filepath, value)) return false;
    std::istringstream input(value);
    std::string version, hash;
    FileStamp stored;
    if (!(input >> version >> hash >> stored.device >> stored.inode >> stored.size >>
          stored.modified_ns >> stored.changed_ns) || version != "v1") return false;
    stored.valid = true;
    if (stored != current) return false;
    hash_out = hash;
    if (stamp_out) *stamp_out = current;
    return true;
}

bool DatabaseManager::store_file_record(const std::string& filepath, const std::string& hash,
                                        const FileStamp& stamp) {
    if (!txn_ || !stamp.valid || hash.empty()) return false;
    std::string old_hash;
    if (get_hash_for_path(filepath, old_hash) && old_hash != hash) {
        if (!remove_path_for_hash(old_hash, filepath)) return false;
    }
    std::ostringstream value;
    value << "v1 " << hash << ' ' << stamp.device << ' ' << stamp.inode << ' ' << stamp.size
          << ' ' << stamp.modified_ns << ' ' << stamp.changed_ns;
    return store_key_value("file:" + filepath, hash) &&
        store_key_value("fileinfo:" + filepath, value.str()) && add_path_for_hash(hash, filepath);
}

bool DatabaseManager::remove_file_record(const std::string& filepath) {
    if (!txn_) return false;
    std::string old_hash;
    if (get_hash_for_path(filepath, old_hash) && !remove_path_for_hash(old_hash, filepath)) return false;
    return delete_key("file:" + filepath) && delete_key("fileinfo:" + filepath);
}
