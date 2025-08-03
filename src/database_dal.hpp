/*
 * database_dal.hpp - Database Abstraction Layer for picexplore
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
#include <functional>
#include <optional>

// Forward declarations
struct ImageInfo;

/**
 * Database transaction interface
 * Provides RAII-style transaction management with automatic rollback on destruction
 */
class ITransaction {
public:
    virtual ~ITransaction() = default;
    
    /**
     * Commit the transaction
     * Returns true on success, false on failure
     */
    virtual bool commit() = 0;
    
    /**
     * Abort/rollback the transaction
     */
    virtual void abort() = 0;
    
    /**
     * Check if transaction is still active (not committed or aborted)
     */
    virtual bool is_active() const = 0;
};

/**
 * Image data access interface
 * Encapsulates all operations related to image metadata
 */
class IImageDataAccess {
public:
    virtual ~IImageDataAccess() = default;
    
    /**
     * Store image path mapping
     * @param txn Transaction context
     * @param hash Image hash
     * @param path File path
     * @return true on success
     */
    virtual bool store_image_path(ITransaction& txn, const std::string& hash, const std::string& path) = 0;
    
    /**
     * Store image metadata (aspect ratio, dimensions, etc.)
     * @param txn Transaction context
     * @param hash Image hash
     * @param aspect_ratio Image aspect ratio
     * @return true on success
     */
    virtual bool store_image_metadata(ITransaction& txn, const std::string& hash, double aspect_ratio) = 0;
    
    /**
     * Get image path by hash
     * @param txn Transaction context
     * @param hash Image hash
     * @return Path if found, empty optional if not found
     */
    virtual std::optional<std::string> get_image_path(ITransaction& txn, const std::string& hash) = 0;
    
    /**
     * Get image metadata by hash
     * @param txn Transaction context
     * @param hash Image hash
     * @return Aspect ratio if found, empty optional if not found
     */
    virtual std::optional<double> get_image_metadata(ITransaction& txn, const std::string& hash) = 0;
    
    /**
     * Get all image hashes
     * @param txn Transaction context
     * @return Vector of image hashes
     */
    virtual std::vector<std::string> get_all_image_hashes(ITransaction& txn) = 0;
    
    /**
     * Remove image data (path and metadata)
     * @param txn Transaction context
     * @param hash Image hash
     * @return true on success
     */
    virtual bool remove_image(ITransaction& txn, const std::string& hash) = 0;
    
    /**
     * Check if image exists
     * @param txn Transaction context
     * @param hash Image hash
     * @return true if image exists
     */
    virtual bool image_exists(ITransaction& txn, const std::string& hash) = 0;
};

/**
 * Thumbnail data access interface
 * Encapsulates all operations related to thumbnail data
 */
class IThumbnailDataAccess {
public:
    virtual ~IThumbnailDataAccess() = default;
    
    /**
     * Store thumbnail data
     * @param txn Transaction context
     * @param hash Image hash
     * @param size Thumbnail size (32, 64, 128, etc.)
     * @param data JPEG thumbnail data
     * @return true on success
     */
    virtual bool store_thumbnail(ITransaction& txn, const std::string& hash, int size, 
                                const std::vector<uint8_t>& data) = 0;
    
    /**
     * Get thumbnail data
     * @param txn Transaction context
     * @param hash Image hash
     * @param size Thumbnail size
     * @return Thumbnail data if found, empty optional if not found
     */
    virtual std::optional<std::vector<uint8_t>> get_thumbnail(ITransaction& txn, const std::string& hash, int size) = 0;
    
    /**
     * Get available thumbnail sizes for an image
     * @param txn Transaction context
     * @param hash Image hash
     * @return Vector of available thumbnail sizes
     */
    virtual std::vector<int> get_available_thumbnail_sizes(ITransaction& txn, const std::string& hash) = 0;
    
    /**
     * Remove all thumbnails for an image
     * @param txn Transaction context
     * @param hash Image hash
     * @return true on success
     */
    virtual bool remove_thumbnails(ITransaction& txn, const std::string& hash) = 0;
    
    /**
     * Remove specific thumbnail
     * @param txn Transaction context
     * @param hash Image hash
     * @param size Thumbnail size
     * @return true on success
     */
    virtual bool remove_thumbnail(ITransaction& txn, const std::string& hash, int size) = 0;
    
    /**
     * Check if thumbnail exists
     * @param txn Transaction context
     * @param hash Image hash
     * @param size Thumbnail size
     * @return true if thumbnail exists
     */
    virtual bool thumbnail_exists(ITransaction& txn, const std::string& hash, int size) = 0;
};

/**
 * Database abstraction layer interface
 * Main interface for all database operations with transaction management
 */
class IDatabaseDAL {
public:
    virtual ~IDatabaseDAL() = default;
    
    /**
     * Initialize the database connection
     * @param db_path Path to database file
     * @return true on success
     */
    virtual bool initialize(const std::string& db_path) = 0;
    
    /**
     * Close the database connection
     */
    virtual void close() = 0;
    
    /**
     * Check if database is open and ready
     */
    virtual bool is_ready() const = 0;
    
    //==========================================================================
    // Transaction Management
    //==========================================================================
    
    /**
     * Begin a read-only transaction
     * @return Transaction object or nullptr on failure
     */
    virtual std::unique_ptr<ITransaction> begin_read_transaction() = 0;
    
    /**
     * Begin a read-write transaction
     * @return Transaction object or nullptr on failure
     */
    virtual std::unique_ptr<ITransaction> begin_write_transaction() = 0;
    
    //==========================================================================
    // Data Access Interfaces
    //==========================================================================
    
    /**
     * Get image data access interface
     */
    virtual IImageDataAccess& images() = 0;
    
    /**
     * Get thumbnail data access interface
     */
    virtual IThumbnailDataAccess& thumbnails() = 0;
    
    //==========================================================================
    // Batch Operations
    //==========================================================================
    
    /**
     * Batch operation result
     */
    struct BatchResult {
        bool success = false;
        size_t processed_count = 0;
        size_t failed_count = 0;
        std::string error_message;
    };
    
    /**
     * Execute a batch of operations in a single transaction
     * @param operations Function that performs multiple operations using the transaction
     * @return BatchResult with operation statistics
     */
    virtual BatchResult execute_batch(std::function<bool(ITransaction&, IImageDataAccess&, IThumbnailDataAccess&)> operations) = 0;
    
    //==========================================================================
    // Utility Methods
    //==========================================================================
    
    /**
     * Get database statistics
     */
    struct DatabaseStats {
        size_t image_count = 0;
        size_t thumbnail_count = 0;
        size_t database_size_bytes = 0;
    };
    
    virtual DatabaseStats get_stats() = 0;
    
    /**
     * Compact/optimize the database
     * @return true on success
     */
    virtual bool compact() = 0;
};

/**
 * Factory function to create database DAL implementation
 * @return Unique pointer to database DAL instance
 */
std::unique_ptr<IDatabaseDAL> create_database_dal();

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s