# Cache System Documentation

The picexplore cache system provides unified LRU (Least Recently Used) caching with memory limits and automatic invalidation for thumbnail data and other cached content.

## Architecture

### CacheProvider<T>

Generic LRU cache implementation with the following features:

- **Memory Management**: Configurable memory limits with automatic eviction
- **Item Limits**: Configurable maximum number of cached items
- **Thread Safety**: Full thread-safe implementation using mutexes
- **LRU Eviction**: Automatic eviction of least recently used items
- **Statistics**: Hit/miss ratios, memory usage, eviction counts
- **Event Integration**: Eviction callbacks for cleanup and monitoring

### StateStore Integration

The StateStore uses CacheProvider for thumbnail caching with these benefits:

- **Unified Interface**: All caching goes through the same provider
- **Automatic Invalidation**: Cache entries are automatically removed when images are deleted
- **Event Publishing**: Cache operations publish events through the EventBus
- **Memory Efficiency**: Smart eviction prevents memory bloat

## Configuration

### Default Configuration

```cpp
// Default StateStore configuration
StateStore state_store;  // 50MB memory limit, 1000 item limit
```

### Custom Configuration

```cpp
// Custom cache configuration
CacheConfig config;
config.max_memory_mb = 100;  // 100MB memory limit
config.max_items = 2000;     // 2000 item limit
config.enable_stats = true;  // Enable statistics

StateStore state_store(config);
```

### Runtime Configuration Updates

```cpp
// Update configuration at runtime
CacheConfig new_config;
new_config.max_memory_mb = 200;  // Increase to 200MB
new_config.max_items = 5000;     // Increase to 5000 items

state_store.update_cache_config(new_config);

// Or update individual limits
state_store.set_cache_memory_limit(150);  // 150MB
state_store.set_cache_item_limit(3000);   // 3000 items
```

## Events

The cache system publishes several events through the EventBus:

### Cache Events

- **CACHE_EVICTED**: Published when items are evicted due to memory/size limits
- **CACHE_CLEARED**: Published when the cache is manually cleared

### Thumbnail Events

- **THUMBNAIL_READY**: New thumbnail added to cache
- **THUMBNAIL_UPDATED**: Existing thumbnail updated
- **THUMBNAIL_INVALIDATED**: Thumbnail removed due to image deletion

### Example Event Subscription

```cpp
// Subscribe to cache eviction events
state_store.get_event_bus().subscribe(StateEventType::CACHE_EVICTED,
    [](const StateEvent& event) {
        const auto& cache_event = static_cast<const CacheEvent&>(event);
        std::cout << "Cache evicted: " << cache_event.cache_key 
                  << ", freed " << cache_event.memory_freed << " bytes" << std::endl;
    });
```

## Usage Examples

### Basic Thumbnail Caching

```cpp
StateStore state_store;

// Add image metadata
StateImageInfo info;
info.path = "/path/to/image.jpg";
info.hash = "abc123";
info.aspect_ratio = 1.33;
state_store.add_image_metadata(info);

// Add thumbnail to cache
std::vector<uint8_t> jpeg_data = load_thumbnail("abc123", 256);
state_store.add_thumbnail("abc123", 256, jpeg_data, 256, 192);

// Retrieve thumbnail from cache
auto thumbnail = state_store.get_thumbnail("abc123", 256);
if (thumbnail) {
    // Use cached thumbnail data
    display_thumbnail(thumbnail->data, thumbnail->width, thumbnail->height);
}
```

### Cache Statistics Monitoring

```cpp
auto stats = state_store.get_cache_stats();
std::cout << "Cache Statistics:" << std::endl;
std::cout << "  Images: " << stats.image_count << std::endl;
std::cout << "  Thumbnails: " << stats.thumbnail_count << std::endl;
std::cout << "  Memory usage: " << stats.cache_memory_usage / (1024*1024) << " MB" << std::endl;
std::cout << "  Hit ratio: " << (stats.cache_hit_ratio * 100) << "%" << std::endl;
std::cout << "  Total hits: " << stats.cache_hit_count << std::endl;
std::cout << "  Total misses: " << stats.cache_miss_count << std::endl;
```

### Automatic Invalidation

```cpp
// When an image is removed, all its thumbnails are automatically invalidated
state_store.remove_image("abc123");

// The cache will automatically:
// 1. Remove all thumbnails for hash "abc123"
// 2. Publish THUMBNAIL_INVALIDATED events for each removed thumbnail
// 3. Publish IMAGE_REMOVED event
// 4. Free memory used by the removed thumbnails
```

## Memory Management

### Size Calculation

The cache automatically calculates memory usage for different data types:

- **std::vector<uint8_t>**: Uses actual data size plus vector overhead
- **Custom types**: Can override `calculate_size()` for specialized size calculation
- **Explicit sizes**: Can provide explicit size when adding items to cache

### Eviction Policy

When memory or item limits are exceeded:

1. **LRU Eviction**: Least recently used items are evicted first
2. **Immediate Eviction**: Eviction happens immediately when limits are exceeded
3. **Callback Notification**: Eviction callbacks are called for cleanup
4. **Event Publishing**: CACHE_EVICTED events are published

### Memory Efficiency

- Items are stored efficiently using shared_ptr to avoid unnecessary copying
- Memory tracking is atomic for thread safety and performance
- Size calculations include overhead to provide accurate memory accounting

## Performance Considerations

### Thread Safety

- All cache operations are thread-safe using shared_mutex for reader-writer locks
- Multiple threads can read from cache simultaneously
- Write operations (put, remove, evict) are properly synchronized

### Access Patterns

- `get()` operations update LRU order and are not const (use mutable cache in const methods)
- `contains()` operations do not affect LRU order and can be used for existence checks
- Frequent access to the same items keeps them in cache longer (LRU behavior)

### Configuration Tuning

- **Memory Limits**: Set based on available system memory and expected thumbnail sizes
- **Item Limits**: Set based on expected number of unique images and thumbnail sizes
- **Statistics**: Enable for monitoring, but may have slight performance overhead

## Integration with Other Components

### Database Integration

The cache works alongside the database system:
- Database stores persistent thumbnail data
- Cache provides fast in-memory access to frequently used thumbnails
- Cache misses can trigger database lookups
- Database updates can trigger cache invalidation

### UI Integration

The cache integrates with UI components through events:
- UI subscribes to THUMBNAIL_READY events for display updates
- UI can monitor cache statistics for performance information
- Cache eviction events can trigger UI cleanup

### Background Processing

The cache supports background thumbnail generation:
- Thumbnails can be added to cache as they are generated
- Cache provides immediate access to completed thumbnails
- Memory limits prevent runaway memory usage during batch processing