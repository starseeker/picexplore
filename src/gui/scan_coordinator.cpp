#include "scan_coordinator.h"
#include <filesystem>
#include <unordered_set>

namespace fs = std::filesystem;

ScanCoordinator::ScanCoordinator(const std::string& directory,
        moodycamel::ConcurrentQueue<UpdateEvent>& queue, const std::string& db_path, uint64_t session)
    : directory_(normalize_path(directory)), db_path_(db_path), update_queue_(queue, session, &stop_requested_) {}
ScanCoordinator::~ScanCoordinator() { stop(); }
void ScanCoordinator::start() {
    if (worker_.joinable()) return;
    stop_requested_ = false;
    worker_ = std::thread(&ScanCoordinator::run, this);
}
void ScanCoordinator::stop() {
    stop_requested_ = true;
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}
void ScanCoordinator::request_rescan() {
    std::lock_guard<std::mutex> lock(mutex_);
    dirty_ = true;
    wake_.notify_one();
}
void ScanCoordinator::request_rename(const std::string& from, const std::string& to) {
    std::lock_guard<std::mutex> lock(mutex_);
    renames_.emplace_back(normalize_path(from), normalize_path(to));
    dirty_ = true;
    wake_.notify_one();
}
void ScanCoordinator::error(const std::string& message) {
    update_queue_.enqueue(UpdateEvent::make_message(UpdateEvent::Type::SCAN_ERROR, message));
}

void ScanCoordinator::rename(DatabaseManager& db, const std::string& from, const std::string& to) {
    std::vector<std::pair<std::string, Record>> moved;
    for (const auto& item : known_) {
        if (path_is_within(item.first, from)) moved.push_back(item);
    }
    for (const auto& item : moved) {
        const std::string target = to + item.first.substr(from.size());
        FileStamp stamp;
        // Ignore an obsolete rename hint if its destination was replaced again.
        bool target_in_scope = path_is_within(target, directory_);
        if (!recursive_ && !has_explicit_files_) {
            target_in_scope = (fs::path(target).parent_path().lexically_normal().string() == directory_);
        }
        if (!target_in_scope || !is_image_file(target) ||
            !read_file_stamp(target, stamp) || stamp.device != item.second.stamp.device ||
            stamp.inode != item.second.stamp.inode) continue;
        if (db.is_open()) {
            DatabaseManager::WriteTransaction tx(db);
            if (!tx || !db.remove_file_record(item.first) || !tx.commit())
                error("Could not update the cache after a rename. Use File > Rescan to retry.");
        }
        if (has_explicit_files_) {
            for (auto& ef : explicit_files_) {
                if (ef == item.first) ef = target;
            }
        }
        known_.erase(item.first);
        known_[target] = item.second;
        known_[target].stamp.valid = false; // revalidate bytes, including simultaneous edits
        update_queue_.enqueue(UpdateEvent::make_image_renamed(item.first, target));
    }
}

// Returns true if a short retry is useful (e.g. a file was still being written).
bool ScanCoordinator::reconcile(DatabaseManager& db) {
    std::unordered_set<std::string> seen;
    std::vector<std::string> stale_db_paths;
    bool complete = true, retry = false;
    std::unordered_multimap<std::string, std::string> identities;
    auto identity = [](const FileStamp& stamp) {
        return std::to_string(stamp.device) + ":" + std::to_string(stamp.inode);
    };
    for (const auto& item : known_) if (item.second.stamp.valid)
        identities.emplace(identity(item.second.stamp), item.first);

    struct UncachedFile {
        std::string path;
        FileStamp before;
        int width = 0;
        int height = 0;
        bool identified = false;
    };
    std::vector<UncachedFile> uncached_batch;
    uncached_batch.reserve(256);

    auto flush_header_batch = [&]() -> bool {
        if (uncached_batch.empty()) return true;
        if (stop_requested_) return false;

        if (uncached_batch.size() == 1) {
            auto& item = uncached_batch[0];
            item.identified = get_image_info(item.path, &item.width, &item.height);
        } else {
            const size_t num_workers = std::min<size_t>(WorkPermit::limit(), uncached_batch.size());
            std::atomic<size_t> next_idx{0};
            std::vector<std::thread> workers;
            workers.reserve(num_workers);
            for (size_t t = 0; t < num_workers; ++t) {
                workers.emplace_back([&]() {
                    while (!stop_requested_.load(std::memory_order_relaxed)) {
                        size_t idx = next_idx.fetch_add(1, std::memory_order_relaxed);
                        if (idx >= uncached_batch.size()) break;
                        auto& item = uncached_batch[idx];
                        item.identified = get_image_info(item.path, &item.width, &item.height);
                    }
                });
            }
            for (auto& w : workers) {
                if (w.joinable()) w.join();
            }
        }

        if (stop_requested_) return false;

        for (auto& item : uncached_batch) {
            FileStamp after;
            if (!read_file_stamp(item.path, after) || item.before != after) {
                retry = true;
                continue;
            }
            Record record;
            record.stamp = item.before;
            record.width = item.width;
            record.height = item.height;
            bool ok = item.identified && record.width > 0 && record.height > 0;
            if (!ok) {
                record.width = record.height = 0;
                retry = true;
                error("Cannot identify " + item.path + ". The file may be incomplete or damaged; use File > Rescan to retry.");
            }
            auto known = known_.find(item.path);
            bool changed = known == known_.end() || known->second.stamp != item.before ||
                known->second.width != record.width || known->second.height != record.height;
            known_[item.path] = record;
            if (changed) {
                auto event = UpdateEvent::make_image_discovered(item.path, "",
                    record.width, record.height, record.height > 0 ? double(record.width) / record.height : 1.0,
                    item.before.size, item.before.modified_ns / 1000000000,
                    ok ? ThumbQuality::NONE : ThumbQuality::FAILED);
                event.image.stamp = item.before;
                update_queue_.enqueue(std::move(event));
            }
        }
        uncached_batch.clear();
        return !stop_requested_;
    };

    auto process_file = [&](const std::string& path, bool is_explicit) -> bool {
        FileStamp before;
        if (read_file_stamp(path, before)) {
            seen.insert(path);
            auto known = known_.find(path);
            if (known == known_.end()) {
                const auto candidates = identities.equal_range(identity(before));
                for (auto candidate = candidates.first; candidate != candidates.second; ++candidate) {
                    FileStamp previous;
                    if (candidate->second != path && !read_file_stamp(candidate->second, previous) && errno == ENOENT) {
                        rename(db, candidate->second, path);
                        known = known_.find(path);
                        break;
                    }
                }
            }
            if (known != known_.end() && known->second.stamp == before && known->second.width > 0) {
                return true;
            }
            std::string cached_hash;
            ImageMetadata cached_meta{};
            int best_thumb_size = 0;
            bool had_stale = false;
            if (db.is_open() && db.get_cached_file_record(path, before, cached_hash, cached_meta, best_thumb_size, &had_stale)) {
                Record record;
                record.stamp = before;
                record.hash = cached_hash;
                record.width = cached_meta.orig_width;
                record.height = cached_meta.orig_height;
                bool changed = known == known_.end() || known->second.stamp != before ||
                    known->second.width != record.width || known->second.hash != record.hash;
                known_[path] = record;
                if (changed) {
                    auto event = UpdateEvent::make_image_discovered(path, record.hash,
                        record.width, record.height, record.height > 0 ? double(record.width) / record.height : 1.0,
                        before.size, before.modified_ns / 1000000000,
                        best_thumb_size > 0 ? ThumbQuality::SMALL : ThumbQuality::NONE);
                    event.image.stamp = before;
                    update_queue_.enqueue(std::move(event));
                }
                return true;
            }
            if (had_stale) {
                stale_db_paths.push_back(path);
            }
            uncached_batch.push_back(UncachedFile{path, before});
            if (uncached_batch.size() >= 256) {
                return flush_header_batch();
            }
        } else if (!is_explicit || errno != ENOENT) {
            // A failed stat (other than ENOENT for an explicit target) is not proof of deletion.
            seen.insert(path);
            retry = true;
        }
        return true;
    };

    size_t found = 0;
    if (has_explicit_files_) {
        for (const auto& raw_path : explicit_files_) {
            if (stop_requested_) break;
            const std::string path = normalize_path(raw_path);
            if (!is_cache_or_db_path(path, db_path_) && is_image_file(path)) {
                if (!process_file(path, true)) break;
            }
            if (++found % 1000 == 0) update_queue_.enqueue(UpdateEvent::make_scan_progress(found));
        }
    } else if (!recursive_) {
        std::error_code ec;
        fs::directory_iterator it(directory_, fs::directory_options::none, ec), end;
        if (ec) {
            complete = ec == std::errc::no_such_file_or_directory;
            if (!complete) error("Cannot read " + directory_ + ": " + ec.message() + ". Use File > Rescan to retry.");
        }
        while (it != end && !stop_requested_) {
            const auto entry = *it;
            const std::string path = entry.path().lexically_normal().string();
            std::error_code type_error;
            bool is_dir = entry.is_directory(type_error);
            if (type_error) complete = false;
            if (!is_dir && !is_cache_or_db_path(path, db_path_) && is_image_file(path)) {
                if (!process_file(path, false)) break;
            }
            it.increment(ec);
            if (ec) { complete = false; error("Directory scan incomplete: " + ec.message() + ". Existing images have been kept; use File > Rescan to retry."); }
            if (++found % 1000 == 0) update_queue_.enqueue(UpdateEvent::make_scan_progress(found));
        }
    } else {
        std::error_code ec;
        fs::recursive_directory_iterator it(directory_, fs::directory_options::none, ec), end;
        if (ec) {
            // Only confirmed absence permits removal. Permission and I/O failures do not.
            complete = ec == std::errc::no_such_file_or_directory;
            if (!complete) error("Cannot read " + directory_ + ": " + ec.message() + ". Use File > Rescan to retry.");
        }
        while (it != end && !stop_requested_) {
            const auto entry = *it;
            const std::string path = entry.path().lexically_normal().string();
            std::error_code type_error;
            bool is_dir = entry.is_directory(type_error);
            if (type_error) complete = false;
            if (is_cache_or_db_path(path, db_path_)) {
                if (is_dir) it.disable_recursion_pending();
            } else if (!is_dir && is_image_file(path)) {
                if (!process_file(path, false)) break;
            }
            it.increment(ec);
            if (ec) { complete = false; error("Directory scan incomplete: " + ec.message() + ". Existing images have been kept; use File > Rescan to retry."); }
            if (++found % 1000 == 0) update_queue_.enqueue(UpdateEvent::make_scan_progress(found));
        }
    }
    if (!flush_header_batch() || stop_requested_) return false;
    std::vector<std::string> deleted_paths;
    if (complete) {
        // Include persisted paths on first scan, but never display them without validation.
        std::unordered_set<std::string> candidates;
        for (const auto& item : known_) candidates.insert(item.first);
        if (db.is_open() && !has_explicit_files_ && !initial_db_reconciled_) {
            for (const auto& image : db.get_images_for_directory(directory_)) {
                if (!recursive_ && fs::path(image.path).parent_path().lexically_normal().string() != directory_) {
                    continue;
                }
                candidates.insert(image.path);
            }
            initial_db_reconciled_ = true;
        }
        for (const auto& path : candidates) {
            if (!seen.count(path)) {
                deleted_paths.push_back(path);
            }
        }
    }
    if (db.is_open() && (!stale_db_paths.empty() || !deleted_paths.empty())) {
        DatabaseManager::WriteTransaction tx(db);
        bool ok = static_cast<bool>(tx);
        for (const auto& path : stale_db_paths) {
            if (ok && !db.remove_file_record(path)) ok = false;
        }
        for (const auto& path : deleted_paths) {
            if (ok && !db.remove_file_record(path)) ok = false;
        }
        if (!ok || !tx.commit()) {
            error("Could not remove a stale cache record. Use File > Rescan to retry.");
        }
    }
    for (const auto& path : deleted_paths) {
        if (known_.erase(path)) update_queue_.enqueue(UpdateEvent::make_image_deleted(path));
    }
    update_queue_.enqueue(UpdateEvent::make_scan_complete());
    return retry;
}

void ScanCoordinator::run() {
    DatabaseManager db;
    if (!db_path_.empty() && !db.open(db_path_)) error("Could not open the image cache. Check permissions and use File > Rescan to retry.");
    unsigned retries = 0;
    bool retry = reconcile(db);
    while (!stop_requested_) {
        std::vector<std::pair<std::string, std::string>> renames;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            const auto delay = retry && retries < 3 ? std::chrono::milliseconds(250 << retries++) : std::chrono::milliseconds(30000);
            wake_.wait_for(lock, delay, [this] { return stop_requested_ || dirty_; });
            if (stop_requested_) break;
            if (dirty_) {
                // Coalesce bursts, with a fixed maximum latency even under constant activity.
                wake_.wait_for(lock, std::chrono::milliseconds(150), [this] { return stop_requested_.load(); });
                retries = 0;
            }
            dirty_ = false;
            renames.swap(renames_);
        }
        for (const auto& item : renames) rename(db, item.first, item.second);
        retry = reconcile(db);
    }
}
