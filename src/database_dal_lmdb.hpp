/*
 * database_dal_lmdb.hpp - LMDB implementation of Database Abstraction Layer
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

#include "database_dal.hpp"
#include "lmdb.h"
#include <mutex>
#include <atomic>

// Forward declarations
class EventBus;

/**
 * LMDB transaction implementation
 */
class LMDBTransaction : public ITransaction {
public:
    LMDBTransaction(MDB_txn* txn, bool is_read_only);
    ~LMDBTransaction() override;
    
    bool commit() override;
    void abort() override;
    bool is_active() const override;
    
    // Internal access for LMDB operations
    MDB_txn* get_txn() const { return txn_; }

private:
    MDB_txn* txn_;
    bool is_read_only_;
    std::atomic<bool> is_active_;
};

/**
 * LMDB image data access implementation
 */
class LMDBImageDataAccess : public IImageDataAccess {
public:
    LMDBImageDataAccess(MDB_dbi dbi, EventBus* event_bus = nullptr);
    
    bool store_image_path(ITransaction& txn, const std::string& hash, const std::string& path) override;
    bool store_image_metadata(ITransaction& txn, const std::string& hash, double aspect_ratio) override;
    std::optional<std::string> get_image_path(ITransaction& txn, const std::string& hash) override;
    std::optional<double> get_image_metadata(ITransaction& txn, const std::string& hash) override;
    std::vector<std::string> get_all_image_hashes(ITransaction& txn) override;
    bool remove_image(ITransaction& txn, const std::string& hash) override;
    bool image_exists(ITransaction& txn, const std::string& hash) override;

private:
    MDB_dbi dbi_;
    EventBus* event_bus_;
    
    // Helper methods
    bool store_key_value(MDB_txn* txn, const std::string& key, const std::string& value);
    bool get_key_value(MDB_txn* txn, const std::string& key, std::string& value);
    bool delete_key(MDB_txn* txn, const std::string& key);
    std::string make_path_key(const std::string& hash);
    std::string make_metadata_key(const std::string& hash);
    std::string extract_hash_from_path_key(const std::string& key);
};

/**
 * LMDB thumbnail data access implementation
 */
class LMDBThumbnailDataAccess : public IThumbnailDataAccess {
public:
    LMDBThumbnailDataAccess(MDB_dbi dbi, EventBus* event_bus = nullptr);
    
    bool store_thumbnail(ITransaction& txn, const std::string& hash, int size, 
                        const std::vector<uint8_t>& data) override;
    std::optional<std::vector<uint8_t>> get_thumbnail(ITransaction& txn, const std::string& hash, int size) override;
    std::vector<int> get_available_thumbnail_sizes(ITransaction& txn, const std::string& hash) override;
    bool remove_thumbnails(ITransaction& txn, const std::string& hash) override;
    bool remove_thumbnail(ITransaction& txn, const std::string& hash, int size) override;
    bool thumbnail_exists(ITransaction& txn, const std::string& hash, int size) override;

private:
    MDB_dbi dbi_;
    EventBus* event_bus_;
    
    // Helper methods
    bool store_key_data(MDB_txn* txn, const std::string& key, const std::vector<uint8_t>& data);
    bool get_key_data(MDB_txn* txn, const std::string& key, std::vector<uint8_t>& data);
    bool delete_key(MDB_txn* txn, const std::string& key);
    std::string make_thumbnail_key(const std::string& hash, int size);
    bool extract_hash_and_size_from_thumbnail_key(const std::string& key, std::string& hash, int& size);
};

/**
 * LMDB database DAL implementation
 */
class DatabaseDAL_LMDB : public IDatabaseDAL {
public:
    DatabaseDAL_LMDB(EventBus* event_bus = nullptr);
    ~DatabaseDAL_LMDB() override;
    
    // Core interface
    bool initialize(const std::string& db_path) override;
    void close() override;
    bool is_ready() const override;
    
    // Transaction management
    std::unique_ptr<ITransaction> begin_read_transaction() override;
    std::unique_ptr<ITransaction> begin_write_transaction() override;
    
    // Data access interfaces
    IImageDataAccess& images() override { return image_access_; }
    IThumbnailDataAccess& thumbnails() override { return thumbnail_access_; }
    
    // Batch operations
    BatchResult execute_batch(std::function<bool(ITransaction&, IImageDataAccess&, IThumbnailDataAccess&)> operations) override;
    
    // Utility methods
    DatabaseStats get_stats() override;
    bool compact() override;

private:
    // LMDB environment and database
    MDB_env* env_;
    MDB_dbi dbi_;
    std::atomic<bool> is_ready_;
    
    // Event system integration
    EventBus* event_bus_;
    
    // Data access implementations
    LMDBImageDataAccess image_access_;
    LMDBThumbnailDataAccess thumbnail_access_;
    
    // Thread safety
    mutable std::mutex db_mutex_;
    
    // Configuration constants
    static constexpr size_t MAX_DB_SIZE = 549755813888; // 512GB
    
    // Helper methods
    bool setup_environment(const std::string& db_path);
    void cleanup();
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s