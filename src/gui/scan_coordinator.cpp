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
    bool complete = true, retry = false;
    std::unordered_multimap<std::string, std::string> identities;
    auto identity = [](const FileStamp& stamp) {
        return std::to_string(stamp.device) + ":" + std::to_string(stamp.inode);
    };
    for (const auto& item : known_) if (item.second.stamp.valid)
        identities.emplace(identity(item.second.stamp), item.first);

    auto process_file = [&](const std::string& path, bool is_explicit) -> bool {
        FileStamp before, after;
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
            if (known == known_.end() || known->second.stamp != before || known->second.width == 0) {
                WorkPermit work(&stop_requested_);
                if (!work) return false;
                Record record;
                record.stamp = before;
                bool identified = get_image_info(path, &record.width, &record.height);
                if (!db.is_open() || !db.get_valid_hash_for_path(path, record.hash))
                    identified = identified && compute_file_hash(path, record.hash, &stop_requested_);
                if (!read_file_stamp(path, after) || before != after) {
                    retry = true;
                } else {
                    if (!identified) {
                        record.width = record.height = 0;
                        record.hash.clear();
                        retry = true;
                        error("Cannot identify " + path + ". The file may be incomplete or damaged; use File > Rescan to retry.");
                    }
                    bool changed = known == known_.end() || known->second.stamp != before ||
                        known->second.width != record.width || known->second.hash != record.hash;
                    if (identified && db.is_open()) {
                        DatabaseManager::WriteTransaction tx(db);
                        // Upgrade legacy thumbnails once to the shared orientation policy.
                        std::string policy;
                        if (tx && (!db.get_key_value(record.hash + ":render-policy", policy) || policy != "2")) {
                            for (const auto* suffix : {"32", "64", "128", "256", "512", "1024", "2048", "4096", "8192", "sq64", "sq128", "sift"})
                                db.delete_key(record.hash + ":" + suffix);
                            db.store_key_value(record.hash + ":render-policy", "2");
                        }
                        ImageMetadata meta{before.size, static_cast<uint64_t>(before.modified_ns / 1000000000), record.width, record.height};
                        if (!tx || !db.store_file_record(path, record.hash, before) ||
                            !db.store_image_metadata(record.hash, meta) || !tx.commit())
                            error("Could not save image metadata. Check disk space and use File > Rescan to retry.");
                    }
                    known_[path] = record;
                    if (changed) {
                        auto event = UpdateEvent::make_image_discovered(path, record.hash,
                            record.width, record.height, record.height > 0 ? double(record.width) / record.height : 1,
                            before.size, before.modified_ns / 1000000000,
                            identified ? ThumbQuality::NONE : ThumbQuality::FAILED);
                        event.image.stamp = before;
                        update_queue_.enqueue(std::move(event));
                    }
                }
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
    if (stop_requested_) return false;
    if (complete) {
        // Include persisted paths on first scan, but never display them without validation.
        std::unordered_set<std::string> candidates;
        for (const auto& item : known_) candidates.insert(item.first);
        if (db.is_open() && !has_explicit_files_) {
            for (const auto& image : db.get_images_for_directory(directory_)) {
                if (!recursive_ && fs::path(image.path).parent_path().lexically_normal().string() != directory_) {
                    continue;
                }
                candidates.insert(image.path);
            }
        }
        for (const auto& path : candidates) {
            if (seen.count(path)) continue;
            if (db.is_open()) {
                DatabaseManager::WriteTransaction tx(db);
                if (!tx || !db.remove_file_record(path) || !tx.commit())
                    error("Could not remove a stale cache record. Use File > Rescan to retry.");
            }
            if (known_.erase(path)) update_queue_.enqueue(UpdateEvent::make_image_deleted(path));
        }
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
