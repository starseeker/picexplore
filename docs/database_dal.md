# Database Abstraction Layer (DAL) Documentation

The Database Abstraction Layer (DAL) provides a clean, transactional interface for all database operations in picexplore. It encapsulates low-level database details and ensures data consistency through proper transaction management.

## Overview

The DAL consists of several key interfaces:

- **IDatabaseDAL**: Main interface for database operations and transaction management
- **ITransaction**: RAII-style transaction management with automatic rollback
- **IImageDataAccess**: Interface for image metadata operations (paths, aspect ratios)
- **IThumbnailDataAccess**: Interface for thumbnail data operations

## Key Features

- **Transaction Safety**: All operations use proper ACID transactions
- **Batch Operations**: Support for atomic batch updates
- **Clean Separation**: Database implementation details are hidden
- **RAII Transactions**: Automatic cleanup and rollback on exceptions
- **Type Safety**: Strong typing prevents common database errors

## Basic Usage

### Initializing the Database

```cpp
#include "database_dal.hpp"

auto dal = create_database_dal();
if (!dal->initialize("/path/to/database.db")) {
    // Handle initialization error
    return false;
}
```

### Basic Read Operations

```cpp
// Start a read transaction
auto txn = dal->begin_read_transaction();
if (!txn) {
    // Handle transaction creation error
    return;
}

// Get image path
auto path = dal->images().get_image_path(*txn, "image_hash");
if (path.has_value()) {
    std::cout << "Image path: " << path.value() << std::endl;
}

// Get image metadata
auto aspect_ratio = dal->images().get_image_metadata(*txn, "image_hash");
if (aspect_ratio.has_value()) {
    std::cout << "Aspect ratio: " << aspect_ratio.value() << std::endl;
}

// Get thumbnail
auto thumbnail = dal->thumbnails().get_thumbnail(*txn, "image_hash", 256);
if (thumbnail.has_value()) {
    std::cout << "Thumbnail size: " << thumbnail.value().size() << " bytes" << std::endl;
}

// Transaction is automatically committed/aborted when txn goes out of scope
```

### Basic Write Operations

```cpp
// Start a write transaction
auto txn = dal->begin_write_transaction();
if (!txn) {
    return;
}

// Store image metadata
bool success = dal->images().store_image_path(*txn, "new_hash", "/path/to/image.jpg");
success &= dal->images().store_image_metadata(*txn, "new_hash", 1.777);

// Store thumbnail
std::vector<uint8_t> thumbnail_data = load_thumbnail_data();
success &= dal->thumbnails().store_thumbnail(*txn, "new_hash", 256, thumbnail_data);

if (success && txn->commit()) {
    std::cout << "Data stored successfully!" << std::endl;
} else {
    // Transaction will be automatically aborted
    std::cout << "Failed to store data" << std::endl;
}
```

### Batch Operations

For better performance and consistency, use batch operations for multiple related updates:

```cpp
auto result = dal->execute_batch([](ITransaction& txn, IImageDataAccess& images, IThumbnailDataAccess& thumbnails) {
    bool success = true;
    
    // Process multiple images in a single transaction
    for (const auto& image : image_list) {
        success &= images.store_image_path(txn, image.hash, image.path);
        success &= images.store_image_metadata(txn, image.hash, image.aspect_ratio);
        
        // Store multiple thumbnail sizes
        for (int size : {64, 128, 256, 512}) {
            auto thumb_data = generate_thumbnail(image, size);
            success &= thumbnails.store_thumbnail(txn, image.hash, size, thumb_data);
        }
    }
    
    return success;
});

if (result.success) {
    std::cout << "Batch operation completed successfully!" << std::endl;
    std::cout << "Processed " << result.processed_count << " items" << std::endl;
} else {
    std::cout << "Batch operation failed: " << result.error_message << std::endl;
}
```

## Integration with DatabaseManager

The existing `DatabaseManager` class has been updated to use the DAL:

```cpp
DatabaseManager db_manager;
if (!db_manager.open("/path/to/database.db")) {
    return false;
}

// Access the DAL directly
auto* dal = db_manager.get_dal();

// Use DAL for modern operations
auto txn = dal->begin_read_transaction();
auto all_hashes = dal->images().get_all_image_hashes(*txn);

// Legacy methods still work but are deprecated
auto all_images = db_manager.get_all_images();  // Now uses DAL internally
bool has_thumbs = db_manager.has_thumbnails("some_hash");  // Now uses DAL internally
```

## Migration Guide

### From Direct LMDB Usage

**Old way (deprecated):**
```cpp
MDB_txn* txn;
db_manager.begin_write_transaction(txn);
db_manager.store_key_value(txn, "hash:path", "/path/to/image.jpg");
db_manager.commit_transaction(txn);
```

**New way (recommended):**
```cpp
auto dal = db_manager.get_dal();
auto txn = dal->begin_write_transaction();
dal->images().store_image_path(*txn, "hash", "/path/to/image.jpg");
txn->commit();
```

### Benefits of Migration

1. **Type Safety**: Methods are strongly typed and prevent common errors
2. **Automatic Cleanup**: RAII ensures transactions are properly cleaned up
3. **Better Error Handling**: Clear return types and optional values
4. **Consistency**: All database operations go through the same interface
5. **Testability**: Easy to mock and test database operations

## Database Statistics

Get insights into your database:

```cpp
auto stats = dal->get_stats();
std::cout << "Images: " << stats.image_count << std::endl;
std::cout << "Thumbnails: " << stats.thumbnail_count << std::endl;
std::cout << "Database size: " << stats.database_size_bytes << " bytes" << std::endl;
```

## Error Handling

The DAL uses modern C++ error handling patterns:

```cpp
auto txn = dal->begin_read_transaction();
if (!txn) {
    // Transaction creation failed
    LOG_ERROR("Failed to create transaction");
    return;
}

auto path = dal->images().get_image_path(*txn, "hash");
if (!path.has_value()) {
    // Image not found
    LOG_WARNING("Image not found: hash");
    return;
}

// Use path.value() safely
process_image(path.value());
```

## Performance Considerations

1. **Batch Operations**: Use `execute_batch()` for multiple related operations
2. **Transaction Scope**: Keep transactions as short as possible
3. **Read Transactions**: Use read transactions for queries to allow concurrent access
4. **Connection Pooling**: The DAL manages database connections efficiently

## Thread Safety

The DAL is designed to be thread-safe:

- Multiple read transactions can run concurrently
- Write transactions are serialized automatically
- Each transaction is isolated and consistent
- RAII ensures proper cleanup even with exceptions

## Future Extensions

The DAL interface is designed to be extensible:

- Additional data types can be added easily
- New backend implementations (e.g., SQLite, PostgreSQL) can be plugged in
- Caching layers can be added transparently
- Event notification systems can be integrated

## Testing

The DAL includes comprehensive unit tests:

```bash
# Run DAL unit tests
./build/test/database_dal_test

# Run integration tests
./build/test/database_manager_dal_integration_test
```

## Conclusion

The Database Abstraction Layer provides a modern, safe, and efficient interface for all database operations in picexplore. It maintains backward compatibility while encouraging migration to better patterns and practices.

For questions or issues, refer to the unit tests for additional usage examples.