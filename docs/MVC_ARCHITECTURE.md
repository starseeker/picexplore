# Model-View-Controller Architecture for PicExplore

This document describes the new MVC architecture implemented in the fifth stage of the picexplore architecture migration plan.

## Overview

The MVC refactoring separates the application into three distinct layers:

- **Model**: Data management and business logic (StateStore, DatabaseManager, ThreadManager)
- **View**: User interface presentation and interaction (FLTK widgets, dialogs)
- **Controller**: Application logic and coordination between model and view

## Architecture Components

### Controller Layer

#### ApplicationController
- **Purpose**: Main application coordinator and entry point
- **Responsibilities**:
  - Initialize and coordinate sub-controllers
  - Handle application-level operations (directory scanning, PDF generation)
  - Manage application lifecycle and shutdown
  - Bridge between main view and business logic

#### ScanController
- **Purpose**: Manage directory scanning operations
- **Responsibilities**:
  - Start/stop directory scanning
  - Monitor scan progress and status
  - Handle scan completion and error states
  - Coordinate with ThreadManager for background scanning

#### GalleryController
- **Purpose**: Manage gallery display and interaction
- **Responsibilities**:
  - Load and manage image display data
  - Handle image selection and user interaction
  - Request thumbnails for visible images
  - Update display configuration (layout, spacing, etc.)

### View Layer

#### View Interfaces
- **IView**: Base interface for all views
- **PicExploreView**: Main application window interface
- **GalleryView**: Gallery thumbnail display interface

#### FLTK Implementations
- **FLTKPicExploreView**: FLTK-based main window
- **FLTKGalleryView**: FLTK-based gallery view
- **Fl_JustifiedLayout_View**: Pure view component for justified layout

#### View Factory
- **ViewFactory**: Abstract factory for creating views
- **FLTKViewFactory**: Concrete factory for FLTK-based views

### Model Layer

#### StateStore (Enhanced)
- **Purpose**: Centralized state management
- **Features**:
  - Thread-safe image metadata storage
  - Thumbnail caching with memory management
  - Scan progress tracking
  - Event-driven state change notifications

#### EventBus
- **Purpose**: Loose coupling through event-driven communication
- **Events**:
  - `IMAGE_METADATA_ADDED/UPDATED`
  - `THUMBNAIL_READY`
  - `SCAN_STARTED/PROGRESS/COMPLETED`
  - `CACHE_EVICTED`

## Key Design Patterns

### Observer Pattern
Controllers subscribe to StateStore events to react to state changes:

```cpp
// Controller subscribes to events
auto subscription_id = state_store->get_event_bus().subscribe(
    StateEventType::IMAGE_METADATA_ADDED,
    [this](const StateEvent& event) {
        // Handle new image metadata
    }
);
```

### Factory Pattern
Views are created through factories, allowing different UI implementations:

```cpp
// Create views through factory
auto factory = std::make_unique<FLTKViewFactory>();
auto main_view = factory->create_main_view();
```

### Command Pattern
Controllers encapsulate operations and coordinate their execution:

```cpp
// Controller handles complex operations
bool success = scan_controller->start_directory_scan(
    directory_path,
    completion_callback,
    progress_callback
);
```

## Communication Flow

### Image Loading Flow
1. User selects directory → View notifies ApplicationController
2. ApplicationController → ScanController starts scan
3. ScanController → ThreadManager performs background scan
4. ThreadManager → StateStore stores image metadata
5. StateStore → EventBus publishes IMAGE_METADATA_ADDED
6. GalleryController receives event → updates view

### Thumbnail Display Flow
1. View scrolls → GalleryView notifies GalleryController
2. GalleryController identifies visible images
3. GalleryController → ThreadManager requests thumbnails
4. ThreadManager generates thumbnails → StateStore
5. StateStore → EventBus publishes THUMBNAIL_READY
6. GalleryController receives event → updates view display

## Benefits of MVC Architecture

### Separation of Concerns
- **Model**: Focused on data management and business logic
- **View**: Focused only on presentation and user interaction
- **Controller**: Focused on application flow and coordination

### Testability
- Controllers can be unit tested without UI dependencies
- Model components are isolated and testable
- Mock views can be used for testing controllers

### Flexibility
- Different UI implementations (FLTK, Qt, web) through view factories
- Business logic changes don't affect UI code
- UI changes don't affect business logic

### Maintainability
- Clear boundaries between components
- Loose coupling through event-driven communication
- Single responsibility principle enforced

## Migration Path

### Original vs MVC
- **Original**: `picexplore` - monolithic architecture
- **MVC**: `picexplore_mvc` - refactored architecture
- Both executables built and supported during transition

### Backwards Compatibility
- Existing command-line interface preserved
- All original functionality maintained
- Same configuration files and databases

## Usage Examples

### Creating MVC Application
```cpp
// Initialize MVC application
PicExploreApplication app;
app.initialize();

// Run in GUI mode
app.run_gui_mode(argc, argv);

// Or run in command-line mode
app.run_scan_only_mode(argc, argv);
```

### Testing Controllers
```cpp
// Test controller without GUI
auto state_store = std::make_shared<StateStore>();
auto thread_manager = std::make_shared<ThreadManager>();
auto controller = std::make_shared<GalleryController>(state_store, thread_manager);

controller->initialize();
// Test controller operations
```

## Future Enhancements

### Additional UI Frameworks
- Qt implementation through new factory
- Web interface through HTTP API and view factory
- Console interface for server environments

### Advanced Patterns
- Command pattern for undo/redo operations
- Strategy pattern for different layout algorithms
- Decorator pattern for view enhancements

### Plugin Architecture
- Controller plugins for new operations
- View plugins for custom displays
- Model plugins for additional data sources

## Testing

### MVC Core Tests
- `mvc_core_test`: Tests core MVC patterns without GUI dependencies
- Validates state management, event communication, and separation of concerns

### Integration Tests  
- `mvc_components_test`: Tests full MVC stack with all dependencies
- Validates controller coordination and view integration

### Existing Tests
- All existing tests continue to pass
- MVC refactoring maintains backwards compatibility
- New architecture doesn't break existing functionality

## Build Configuration

### CMake Targets
```bash
# Build MVC version
make picexplore_mvc

# Build original version
make picexplore

# Build and run MVC tests
make mvc_core_test && ./test/mvc_core_test
```

### Dependencies
- Same dependencies as original version
- No additional external libraries required
- FLTK dependency isolated to view layer

This MVC architecture provides a solid foundation for future development, making the codebase more maintainable, testable, and extensible while preserving all existing functionality.