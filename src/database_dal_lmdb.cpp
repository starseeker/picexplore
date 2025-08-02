/*
 * database_dal_lmdb.cpp - LMDB implementation of Database Abstraction Layer
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

#include "database_dal_lmdb.hpp"
#include "event_bus.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include <filesystem>
#include <algorithm>
#include <sstream>

namespace fs = std::filesystem;

//==========================================================================
// LMDBTransaction Implementation
//==========================================================================

LMDBTransaction::LMDBTransaction(MDB_txn* txn, bool is_read_only)
    : txn_(txn), is_read_only_(is_read_only), is_active_(true) {
}

LMDBTransaction::~LMDBTransaction() {
    if (is_active_.load()) {
        abort();
    }
}

bool LMDBTransaction::commit() {
    if (!is_active_.load() || !txn_) {
        return false;
    }
    
    int rc = mdb_txn_commit(txn_);
    is_active_.store(false);
    txn_ = nullptr;
    
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Transaction commit failed: " + std::string(mdb_strerror(rc)));
        return false;
    }
    
    return true;
}

void LMDBTransaction::abort() {
    if (!is_active_.load() || !txn_) {
        return;
    }
    
    mdb_txn_abort(txn_);
    is_active_.store(false);
    txn_ = nullptr;
}

bool LMDBTransaction::is_active() const {
    return is_active_.load();
}

//==========================================================================
// LMDBImageDataAccess Implementation
//==========================================================================

LMDBImageDataAccess::LMDBImageDataAccess(MDB_dbi dbi, EventBus* event_bus)
    : dbi_(dbi), event_bus_(event_bus) {
}

bool LMDBImageDataAccess::store_image_path(ITransaction& txn, const std::string& hash, const std::string& path) {
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return false;
    }
    
    std::string path_key = make_path_key(hash);
    bool success = store_key_value(lmdb_txn->get_txn(), path_key, path);
    
    if (success && event_bus_) {
        // Publish event - will be sent when transaction commits
        // For now, we don't publish here since the transaction might be aborted
    }
    
    return success;
}

bool LMDBImageDataAccess::store_image_metadata(ITransaction& txn, const std::string& hash, double aspect_ratio) {
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return false;
    }
    
    std::string metadata_key = make_metadata_key(hash);
    std::string metadata_value = std::to_string(aspect_ratio);
    
    return store_key_value(lmdb_txn->get_txn(), metadata_key, metadata_value);
}

std::optional<std::string> LMDBImageDataAccess::get_image_path(ITransaction& txn, const std::string& hash) {
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return std::nullopt;
    }
    
    std::string path_key = make_path_key(hash);
    std::string path;
    if (get_key_value(lmdb_txn->get_txn(), path_key, path)) {
        return path;
    }
    
    return std::nullopt;
}

std::optional<double> LMDBImageDataAccess::get_image_metadata(ITransaction& txn, const std::string& hash) {
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return std::nullopt;
    }
    
    std::string metadata_key = make_metadata_key(hash);
    std::string metadata_value;
    if (get_key_value(lmdb_txn->get_txn(), metadata_key, metadata_value)) {
        try {
            double aspect_ratio = std::stod(metadata_value);
            return aspect_ratio;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    
    return std::nullopt;
}

std::vector<std::string> LMDBImageDataAccess::get_all_image_hashes(ITransaction& txn) {
    std::vector<std::string> hashes;
    
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return hashes;
    }
    
    MDB_cursor* cursor;
    if (mdb_cursor_open(lmdb_txn->get_txn(), dbi_, &cursor) != 0) {
        return hashes;
    }
    
    MDB_val key, data;
    while (mdb_cursor_get(cursor, &key, &data, MDB_NEXT) == 0) {
        std::string key_str(static_cast<char*>(key.mv_data), key.mv_size);
        std::string hash = extract_hash_from_path_key(key_str);
        if (!hash.empty()) {
            hashes.push_back(hash);
        }
    }
    
    mdb_cursor_close(cursor);
    
    // Sort for consistent ordering
    std::sort(hashes.begin(), hashes.end());
    
    return hashes;
}

bool LMDBImageDataAccess::remove_image(ITransaction& txn, const std::string& hash) {
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return false;
    }
    
    std::string path_key = make_path_key(hash);
    std::string metadata_key = make_metadata_key(hash);
    
    bool success = true;
    success &= delete_key(lmdb_txn->get_txn(), path_key);
    success &= delete_key(lmdb_txn->get_txn(), metadata_key);
    
    return success;
}

bool LMDBImageDataAccess::image_exists(ITransaction& txn, const std::string& hash) {
    return get_image_path(txn, hash).has_value();
}

// Helper methods
bool LMDBImageDataAccess::store_key_value(MDB_txn* txn, const std::string& key, const std::string& value) {
    if (!txn) return false;
    
    MDB_val k, v;
    k.mv_data = const_cast<char*>(key.c_str());
    k.mv_size = key.length();
    v.mv_data = const_cast<char*>(value.c_str());
    v.mv_size = value.length();
    
    int rc = mdb_put(txn, dbi_, &k, &v, 0);
    return (rc == 0);
}

bool LMDBImageDataAccess::get_key_value(MDB_txn* txn, const std::string& key, std::string& value) {
    if (!txn) return false;
    
    MDB_val k, v;
    k.mv_data = const_cast<char*>(key.c_str());
    k.mv_size = key.length();
    
    if (mdb_get(txn, dbi_, &k, &v) == 0) {
        value.assign(static_cast<char*>(v.mv_data), v.mv_size);
        return true;
    }
    return false;
}

bool LMDBImageDataAccess::delete_key(MDB_txn* txn, const std::string& key) {
    if (!txn) return false;
    
    MDB_val k;
    k.mv_data = const_cast<char*>(key.c_str());
    k.mv_size = key.length();
    
    int rc = mdb_del(txn, dbi_, &k, nullptr);
    return (rc == 0 || rc == MDB_NOTFOUND); // Success if deleted or already not found
}

std::string LMDBImageDataAccess::make_path_key(const std::string& hash) {
    return hash + ":path";
}

std::string LMDBImageDataAccess::make_metadata_key(const std::string& hash) {
    return hash + ":metadata";
}

std::string LMDBImageDataAccess::extract_hash_from_path_key(const std::string& key) {
    const std::string suffix = ":path";
    if (key.length() > suffix.length() && key.substr(key.length() - suffix.length()) == suffix) {
        return key.substr(0, key.length() - suffix.length());
    }
    return "";
}

//==========================================================================
// LMDBThumbnailDataAccess Implementation
//==========================================================================

LMDBThumbnailDataAccess::LMDBThumbnailDataAccess(MDB_dbi dbi, EventBus* event_bus)
    : dbi_(dbi), event_bus_(event_bus) {
}

bool LMDBThumbnailDataAccess::store_thumbnail(ITransaction& txn, const std::string& hash, int size, 
                                            const std::vector<uint8_t>& data) {
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return false;
    }
    
    std::string thumb_key = make_thumbnail_key(hash, size);
    return store_key_data(lmdb_txn->get_txn(), thumb_key, data);
}

std::optional<std::vector<uint8_t>> LMDBThumbnailDataAccess::get_thumbnail(ITransaction& txn, const std::string& hash, int size) {
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return std::nullopt;
    }
    
    std::string thumb_key = make_thumbnail_key(hash, size);
    std::vector<uint8_t> data;
    if (get_key_data(lmdb_txn->get_txn(), thumb_key, data)) {
        return data;
    }
    
    return std::nullopt;
}

std::vector<int> LMDBThumbnailDataAccess::get_available_thumbnail_sizes(ITransaction& txn, const std::string& hash) {
    std::vector<int> sizes;
    
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return sizes;
    }
    
    // Standard thumbnail sizes to check
    std::vector<int> check_sizes = {32, 64, 128, 256, 512, 1024};
    
    for (int size : check_sizes) {
        if (thumbnail_exists(txn, hash, size)) {
            sizes.push_back(size);
        }
    }
    
    return sizes;
}

bool LMDBThumbnailDataAccess::remove_thumbnails(ITransaction& txn, const std::string& hash) {
    std::vector<int> sizes = get_available_thumbnail_sizes(txn, hash);
    bool success = true;
    
    for (int size : sizes) {
        success &= remove_thumbnail(txn, hash, size);
    }
    
    return success;
}

bool LMDBThumbnailDataAccess::remove_thumbnail(ITransaction& txn, const std::string& hash, int size) {
    auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(&txn);
    if (!lmdb_txn || !lmdb_txn->is_active()) {
        return false;
    }
    
    std::string thumb_key = make_thumbnail_key(hash, size);
    return delete_key(lmdb_txn->get_txn(), thumb_key);
}

bool LMDBThumbnailDataAccess::thumbnail_exists(ITransaction& txn, const std::string& hash, int size) {
    return get_thumbnail(txn, hash, size).has_value();
}

// Helper methods
bool LMDBThumbnailDataAccess::store_key_data(MDB_txn* txn, const std::string& key, const std::vector<uint8_t>& data) {
    if (!txn) return false;
    
    MDB_val k, v;
    k.mv_data = const_cast<char*>(key.c_str());
    k.mv_size = key.length();
    v.mv_data = const_cast<void*>(static_cast<const void*>(data.data()));
    v.mv_size = data.size();
    
    int rc = mdb_put(txn, dbi_, &k, &v, 0);
    return (rc == 0);
}

bool LMDBThumbnailDataAccess::get_key_data(MDB_txn* txn, const std::string& key, std::vector<uint8_t>& data) {
    if (!txn) return false;
    
    MDB_val k, v;
    k.mv_data = const_cast<char*>(key.c_str());
    k.mv_size = key.length();
    
    if (mdb_get(txn, dbi_, &k, &v) == 0) {
        data.assign(static_cast<uint8_t*>(v.mv_data), static_cast<uint8_t*>(v.mv_data) + v.mv_size);
        return true;
    }
    return false;
}

bool LMDBThumbnailDataAccess::delete_key(MDB_txn* txn, const std::string& key) {
    if (!txn) return false;
    
    MDB_val k;
    k.mv_data = const_cast<char*>(key.c_str());
    k.mv_size = key.length();
    
    int rc = mdb_del(txn, dbi_, &k, nullptr);
    return (rc == 0 || rc == MDB_NOTFOUND);
}

std::string LMDBThumbnailDataAccess::make_thumbnail_key(const std::string& hash, int size) {
    return ::make_thumbnail_key(hash, size); // Use utility function from utils.hpp
}

bool LMDBThumbnailDataAccess::extract_hash_and_size_from_thumbnail_key(const std::string& key, std::string& hash, int& size) {
    // Expected format: "hash:thumb:size"
    size_t first_colon = key.find(':');
    if (first_colon == std::string::npos) return false;
    
    size_t second_colon = key.find(':', first_colon + 1);
    if (second_colon == std::string::npos) return false;
    
    std::string thumb_part = key.substr(first_colon + 1, second_colon - first_colon - 1);
    if (thumb_part != "thumb") return false;
    
    hash = key.substr(0, first_colon);
    
    try {
        size = std::stoi(key.substr(second_colon + 1));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

//==========================================================================
// DatabaseDAL_LMDB Implementation
//==========================================================================

DatabaseDAL_LMDB::DatabaseDAL_LMDB(EventBus* event_bus)
    : env_(nullptr), dbi_(0), is_ready_(false), event_bus_(event_bus),
      image_access_(0, event_bus), thumbnail_access_(0, event_bus) {
}

DatabaseDAL_LMDB::~DatabaseDAL_LMDB() {
    close();
}

bool DatabaseDAL_LMDB::initialize(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(db_mutex_);
    
    if (is_ready_.load()) {
        close();
    }
    
    return setup_environment(db_path);
}

void DatabaseDAL_LMDB::close() {
    std::lock_guard<std::mutex> lock(db_mutex_);
    cleanup();
}

bool DatabaseDAL_LMDB::is_ready() const {
    return is_ready_.load();
}

std::unique_ptr<ITransaction> DatabaseDAL_LMDB::begin_read_transaction() {
    if (!is_ready_.load()) {
        return nullptr;
    }
    
    MDB_txn* txn;
    int rc = mdb_txn_begin(env_, nullptr, MDB_RDONLY, &txn);
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Failed to begin read transaction: " + std::string(mdb_strerror(rc)));
        return nullptr;
    }
    
    return std::make_unique<LMDBTransaction>(txn, true);
}

std::unique_ptr<ITransaction> DatabaseDAL_LMDB::begin_write_transaction() {
    if (!is_ready_.load()) {
        return nullptr;
    }
    
    MDB_txn* txn;
    int rc = mdb_txn_begin(env_, nullptr, 0, &txn);
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Failed to begin write transaction: " + std::string(mdb_strerror(rc)));
        return nullptr;
    }
    
    return std::make_unique<LMDBTransaction>(txn, false);
}

IDatabaseDAL::BatchResult DatabaseDAL_LMDB::execute_batch(std::function<bool(ITransaction&, IImageDataAccess&, IThumbnailDataAccess&)> operations) {
    BatchResult result;
    
    auto txn = begin_write_transaction();
    if (!txn) {
        result.error_message = "Failed to begin transaction";
        return result;
    }
    
    try {
        bool success = operations(*txn, image_access_, thumbnail_access_);
        
        if (success && txn->commit()) {
            result.success = true;
            result.processed_count = 1; // At least one operation was attempted
        } else {
            result.error_message = "Batch operation failed or commit failed";
            txn->abort();
        }
    } catch (const std::exception& e) {
        result.error_message = "Exception during batch operation: " + std::string(e.what());
        txn->abort();
    }
    
    return result;
}

IDatabaseDAL::DatabaseStats DatabaseDAL_LMDB::get_stats() {
    DatabaseStats stats;
    
    if (!is_ready_.load()) {
        return stats;
    }
    
    auto txn = begin_read_transaction();
    if (!txn) {
        return stats;
    }
    
    // Count images and thumbnails
    auto image_hashes = image_access_.get_all_image_hashes(*txn);
    stats.image_count = image_hashes.size();
    
    for (const auto& hash : image_hashes) {
        auto sizes = thumbnail_access_.get_available_thumbnail_sizes(*txn, hash);
        stats.thumbnail_count += sizes.size();
    }
    
    // Get database size
    MDB_envinfo info;
    if (mdb_env_info(env_, &info) == 0) {
        // Get page size from a stat call
        MDB_stat stat;
        auto temp_txn = begin_read_transaction();
        if (temp_txn) {
            auto* lmdb_txn = dynamic_cast<LMDBTransaction*>(temp_txn.get());
            if (lmdb_txn && mdb_stat(lmdb_txn->get_txn(), dbi_, &stat) == 0) {
                stats.database_size_bytes = info.me_last_pgno * stat.ms_psize;
            }
        }
    }
    
    return stats;
}

bool DatabaseDAL_LMDB::compact() {
    // LMDB doesn't support online compaction
    // This would require closing, copying to a new database, and reopening
    LOG_SCAN_BASIC("DAL: Database compaction not implemented for LMDB");
    return false;
}

// Helper methods
bool DatabaseDAL_LMDB::setup_environment(const std::string& db_path) {
    int rc = mdb_env_create(&env_);
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Failed to create LMDB environment: " + std::string(mdb_strerror(rc)));
        return false;
    }
    
    // Set map size
    rc = mdb_env_set_mapsize(env_, MAX_DB_SIZE);
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Failed to set LMDB map size: " + std::string(mdb_strerror(rc)));
        mdb_env_close(env_);
        env_ = nullptr;
        return false;
    }
    
    // Check if this is a new database for optimization
    bool is_new_db = !fs::exists(db_path);
    unsigned int flags = MDB_NOSUBDIR;
    if (is_new_db) {
        flags |= MDB_NOSYNC; // Faster bulk insert for new databases
    }
    
    rc = mdb_env_open(env_, db_path.c_str(), flags, 0664);
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Failed to open LMDB database at " + db_path + ": " + std::string(mdb_strerror(rc)));
        mdb_env_close(env_);
        env_ = nullptr;
        return false;
    }
    
    // Open the DBI handle
    MDB_txn* setup_txn;
    rc = mdb_txn_begin(env_, nullptr, 0, &setup_txn);
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Failed to begin setup transaction: " + std::string(mdb_strerror(rc)));
        mdb_env_close(env_);
        env_ = nullptr;
        return false;
    }
    
    rc = mdb_dbi_open(setup_txn, nullptr, MDB_CREATE, &dbi_);
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Failed to open DBI: " + std::string(mdb_strerror(rc)));
        mdb_txn_abort(setup_txn);
        mdb_env_close(env_);
        env_ = nullptr;
        return false;
    }
    
    rc = mdb_txn_commit(setup_txn);
    if (rc != 0) {
        LOG_SCAN_BASIC("DAL: Failed to commit setup transaction: " + std::string(mdb_strerror(rc)));
        mdb_env_close(env_);
        env_ = nullptr;
        return false;
    }
    
    // Update the data access objects with the DBI handle
    image_access_ = LMDBImageDataAccess(dbi_, event_bus_);
    thumbnail_access_ = LMDBThumbnailDataAccess(dbi_, event_bus_);
    
    is_ready_.store(true);
    LOG_SCAN_BASIC("DAL: Successfully initialized LMDB database at " + db_path);
    
    return true;
}

void DatabaseDAL_LMDB::cleanup() {
    if (env_) {
        mdb_env_close(env_);
        env_ = nullptr;
    }
    is_ready_.store(false);
}

//==========================================================================
// Factory Function
//==========================================================================

std::unique_ptr<IDatabaseDAL> create_database_dal() {
    return std::make_unique<DatabaseDAL_LMDB>();
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s