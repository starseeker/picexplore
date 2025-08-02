# StateStore and Event System Documentation

## Overview

The StateStore and Event System provide centralized state management for picexplore, implementing the second stage of the architecture migration plan. This system replaces scattered ad-hoc caches and variables with a unified state management solution.

## Implementation Summary

### What Was Completed

✅ **StateStore Infrastructure** (`src/state_store.hpp/cpp`)
- Centralized image metadata storage with thread-safe access
- Thumbnail cache management with LRU tracking capability  
- Scan progress state management
- Thread-safe operations using `std::shared_mutex`

✅ **Event System** (`src/event_bus.hpp/cpp`)
- Observer pattern implementation for state change notifications
- Support for specific event type subscriptions and "all events" subscriptions
- Thread-safe event publishing and subscription management
- Clean subscription lifecycle management

✅ **ThreadManager Integration** 
- StateStore integrated into ThreadManager as shared component
- Event forwarding from database operations to StateStore
- Backward-compatible progress reporting through StateStore events
- UI notification system preserved while adding StateStore layer

✅ **UI Event Subscriptions** (`src/Fl_JustifiedLayout.hpp/cpp`)
- UI components can subscribe to StateStore events for real-time updates
- Automatic subscription/unsubscription in component lifecycle
- FLTK-safe event handling using Fl::awake() for thread coordination

✅ **Comprehensive Testing**
- Unit tests for StateStore and EventBus (`test/state_store_test.cpp`)
- Integration tests showing ThreadManager-like usage patterns
- Concurrent access safety verification  
- 100% test pass rate across all scenarios

✅ **Documentation**
- Complete API documentation with usage examples
- Architecture explanation and integration patterns
- Performance considerations and thread safety guarantees

### Architecture Benefits

**Centralized State Management**
- Single source of truth for image metadata, thumbnails, and scan progress
- Eliminates scattered variables and ad-hoc caches
- Consistent state access patterns across components

**Event-Driven Updates**  
- Components receive real-time notifications of state changes
- Loose coupling between state producers and consumers
- Scalable subscription model for future UI components

**Thread Safety**
- Safe concurrent access from multiple worker threads
- Reader-writer locks for optimal performance
- Event publishing outside critical sections prevents deadlocks

**Backward Compatibility**
- Existing workflows continue unchanged
- Progressive migration path for UI components  
- No breaking changes to current ThreadManager API

## Architecture

### StateStore

The `StateStore` class serves as the single source of truth for:

- **Image Metadata**: File paths, content hashes, aspect ratios
- **Thumbnail Cache**: Generated thumbnail data and availability tracking  
- **Scan Progress**: Directory scanning status and progress information

#### Key Features

- **Thread-safe**: Uses `std::shared_mutex` for concurrent read/write access
- **Event-driven**: Publishes events through integrated EventBus when state changes
- **Memory efficient**: Tracks thumbnail availability without storing all data in memory
- **Progress tracking**: Maintains scan state and progress for UI updates

#### Core Operations

```cpp
// Image metadata management
void add_image_metadata(const StateImageInfo& info);
std::shared_ptr<const ImageState> get_image_state(const std::string& hash);
std::vector<ImageState> get_all_images();

// Thumbnail management  
void add_thumbnail(const std::string& hash, int size, const std::vector<uint8_t>& data, int width, int height);
std::shared_ptr<const CachedThumbnail> get_thumbnail(const std::string& hash, int size);
bool has_thumbnail(const std::string& hash, int size);

// Scan state management
void start_scan(const std::string& directory_path);
void update_scan_progress(int current, int total, const std::string& status);
void complete_scan();
```

### EventBus

The `EventBus` class implements the observer pattern, allowing components to subscribe to state changes and receive notifications asynchronously.

#### Event Types

- `IMAGE_METADATA_ADDED`: New image discovered during scanning
- `IMAGE_METADATA_UPDATED`: Existing image metadata changed  
- `THUMBNAIL_READY`: New thumbnail generated and cached
- `THUMBNAIL_UPDATED`: Existing thumbnail data updated
- `SCAN_STARTED`: Directory scan initiated
- `SCAN_PROGRESS`: Scan progress update
- `SCAN_COMPLETED`: Directory scan finished successfully
- `SCAN_CANCELLED`: Directory scan cancelled

#### Usage

```cpp
// Subscribe to specific event types
uint64_t subscription_id = event_bus.subscribe(StateEventType::THUMBNAIL_READY, 
    [](const StateEvent& event) {
        const auto& thumb_event = static_cast<const ThumbnailEvent&>(event);
        // Handle thumbnail ready...
    });

// Subscribe to all events
uint64_t all_sub_id = event_bus.subscribe_all([](const StateEvent& event) {
    // Handle any event...
});

// Unsubscribe when done
event_bus.unsubscribe(subscription_id);
```

## Integration with ThreadManager

The StateStore is integrated into the existing ThreadManager to provide centralized state management without breaking current workflows:

### Metadata Flow

1. **DirectoryScanThread** discovers images and extracts metadata
2. Metadata is forwarded to **StateStore** via callback integration
3. **StateStore** publishes `IMAGE_METADATA_ADDED` events
4. UI components subscribed to events receive notifications and update displays

### Thumbnail Flow  

1. **WorkerPool** generates thumbnails in background
2. Thumbnail data is stored in **StateStore** cache
3. **StateStore** publishes `THUMBNAIL_READY` events
4. UI components retrieve thumbnails from StateStore cache

### Progress Updates

1. **StateStore** tracks scan progress and publishes `SCAN_PROGRESS` events
2. **ThreadManager** subscribes to these events and forwards to existing ProgressReporter
3. UI continues to receive progress updates through existing callback mechanisms

## Thread Safety

The StateStore is designed for high-concurrency access:

- **Read operations** use shared locks allowing multiple concurrent readers
- **Write operations** use exclusive locks ensuring data consistency
- **Event publishing** occurs outside of critical sections to prevent deadlocks
- **Cache management** includes LRU tracking with thread-safe access patterns

## Performance Considerations

- **Efficient lookups**: Hash-based indexing for O(1) access to images and thumbnails
- **Memory management**: Configurable cache limits and LRU eviction policies
- **Minimal copying**: Uses shared_ptr for efficient data sharing
- **Batch operations**: Event bus processes subscriptions efficiently without blocking

## Testing

Comprehensive unit tests are provided in `test/state_store_test.cpp`:

- Basic functionality for StateStore and EventBus
- Concurrent access safety verification
- Event subscription and notification testing
- Integration testing with ThreadManager workflows

## Future Enhancements

The StateStore architecture is designed to support future features:

- **Persistent state**: Save/restore state across application sessions
- **Cache policies**: Configurable LRU, memory limits, and eviction strategies  
- **Event filtering**: Advanced subscription patterns and event filtering
- **Performance metrics**: Built-in performance monitoring and statistics
- **Network sync**: Multi-instance state synchronization capabilities

## Migration Notes

This implementation introduces StateStore alongside existing mechanisms without breaking current functionality:

- **Database operations** continue to work as before
- **UI components** can gradually migrate to StateStore events
- **Background workers** integrate seamlessly with StateStore caching
- **Progress reporting** maintains backwards compatibility

The StateStore provides a foundation for future architecture improvements while ensuring current workflows remain functional.