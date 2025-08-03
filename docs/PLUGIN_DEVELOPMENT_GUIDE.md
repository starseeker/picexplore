# Picexplore Plugin System Developer Guide

## Overview

The picexplore plugin system allows developers to extend the functionality of picexplore at runtime through a robust and secure plugin architecture. This document provides comprehensive guidance for developing, building, and deploying plugins.

## Architecture

The plugin system is built around several key components:

- **Plugin Manager**: Central management for loading, unloading, and lifecycle management of plugins
- **Extension Points**: Well-defined interfaces for extending specific functionality 
- **Plugin Interface**: Base interface that all plugins must implement
- **Extension Registry**: Registration and discovery system for plugin capabilities

## Extension Points

The plugin system defines several extension points where plugins can hook into picexplore's functionality:

### 1. Image Processor Extensions (`IImageProcessor`)

Process and filter images during scanning or display.

**Capabilities:**
- Apply filters and effects to images
- Custom image enhancement algorithms
- Color space transformations

**Example Use Cases:**
- Sepia tone filter
- Noise reduction
- Contrast enhancement
- Custom artistic effects

### 2. Image Format Extensions (`IImageFormat`) 

Add support for new image file formats.

**Capabilities:**
- Load/decode new image formats
- Save/encode images in custom formats
- Support for proprietary or specialized formats

**Example Use Cases:**
- RAW camera formats
- Scientific imaging formats
- Legacy or specialized formats

### 3. UI Component Extensions (`IUIComponent`)

Extend the user interface with custom components and views.

**Capabilities:**
- Custom widgets and controls
- Alternative layout algorithms
- Specialized visualization tools

**Example Use Cases:**
- Custom image metadata displays
- Alternative thumbnail layouts
- Specialized image viewers

### 4. Database Backend Extensions (`IDatabaseBackend`)

Provide alternative storage implementations.

**Capabilities:**
- Alternative database systems
- Cloud storage backends
- Specialized storage optimizations

**Example Use Cases:**
- PostgreSQL backend
- Cloud storage integration
- Distributed storage systems

### 5. Metadata Extractor Extensions (`IMetadataExtractor`)

Extract custom metadata from images.

**Capabilities:**
- Custom EXIF parsing
- Proprietary metadata formats
- AI-powered metadata extraction

**Example Use Cases:**
- GPS coordinate extraction
- Camera-specific metadata
- AI-generated tags and descriptions

## Creating a Plugin

### 1. Basic Plugin Structure

Every plugin must inherit from `IPlugin` and implement the required methods:

```cpp
#include "plugin_interface.hpp"
#include "extension_points.hpp"

class MyPlugin : public IPlugin {
public:
    MyPlugin();
    virtual ~MyPlugin();
    
    // Required IPlugin methods
    const PluginMetadata& get_metadata() const override;
    bool initialize(PluginContext* context) override;
    void shutdown() override;
    PluginVersion get_api_version() const override;
    bool is_active() const override;
    void set_active(bool active) override;

private:
    PluginMetadata metadata_;
    PluginContext* context_;
    bool active_;
    bool initialized_;
};

// Register the plugin
REGISTER_PLUGIN(MyPlugin)
```

### 2. Plugin Metadata

Define your plugin's metadata in the constructor:

```cpp
MyPlugin::MyPlugin()
    : metadata_("MyPlugin", 
               "Description of my plugin",
               "Your Name",
               PluginVersion{1, 0, 0},
               PluginVersion{1, 0, 0}),
      context_(nullptr),
      active_(false),
      initialized_(false) {
}
```

### 3. Initialize Method

The initialize method is called when the plugin is loaded:

```cpp
bool MyPlugin::initialize(PluginContext* context) {
    if (initialized_) {
        return true;
    }
    
    if (!context) {
        return false;
    }
    
    context_ = context;
    
    try {
        // Create and register extension points
        auto processor = std::make_unique<MyImageProcessor>();
        ExtensionPointRegistry::register_extension_point(
            std::unique_ptr<IExtensionPoint>(processor.release())
        );
        
        context_->log("INFO", "MyPlugin initialized successfully");
        
        initialized_ = true;
        active_ = true;
        return true;
        
    } catch (const std::exception& e) {
        context_->log("ERROR", "Failed to initialize: " + std::string(e.what()));
        return false;
    }
}
```

## Example: Sepia Filter Plugin

Here's a complete example of a simple image processing plugin:

### Header File (`sepia_filter_plugin.hpp`)

```cpp
#include "plugin_interface.hpp"
#include "extension_points.hpp"

class SepiaImageProcessor : public IImageProcessor {
public:
    std::string get_name() const override {
        return "SepiaFilter";
    }
    
    std::vector<std::string> get_capabilities() const override {
        return {"image_filtering", "color_processing"};
    }
    
    std::unique_ptr<uint8_t[]> process_image(
        const uint8_t* image_data,
        int width,
        int height,
        int channels,
        const std::unordered_map<std::string, std::string>& parameters) override;
    
    std::vector<std::string> get_parameter_names() const override {
        return {"intensity"};
    }
    
    std::string get_parameter_default(const std::string& param_name) const override {
        if (param_name == "intensity") {
            return "1.0";
        }
        return "";
    }
};

class SepiaFilterPlugin : public IPlugin {
    // ... implementation details
};
```

### Implementation File (`sepia_filter_plugin.cpp`)

```cpp
#include "sepia_filter_plugin.hpp"

std::unique_ptr<uint8_t[]> SepiaImageProcessor::process_image(
    const uint8_t* image_data,
    int width,
    int height,
    int channels,
    const std::unordered_map<std::string, std::string>& parameters) {
    
    // Get intensity parameter
    float intensity = 1.0f;
    auto it = parameters.find("intensity");
    if (it != parameters.end()) {
        intensity = std::stof(it->second);
    }
    
    // Allocate output buffer
    size_t data_size = width * height * channels;
    auto output = std::make_unique<uint8_t[]>(data_size);
    
    // Apply sepia transformation
    for (int i = 0; i < width * height; ++i) {
        int idx = i * channels;
        uint8_t r = image_data[idx];
        uint8_t g = image_data[idx + 1];
        uint8_t b = image_data[idx + 2];
        
        // Sepia formula
        float new_r = (r * 0.393f + g * 0.769f + b * 0.189f) * intensity;
        float new_g = (r * 0.349f + g * 0.686f + b * 0.168f) * intensity;
        float new_b = (r * 0.272f + g * 0.534f + b * 0.131f) * intensity;
        
        output[idx] = std::min(255, static_cast<int>(new_r));
        output[idx + 1] = std::min(255, static_cast<int>(new_g));
        output[idx + 2] = std::min(255, static_cast<int>(new_b));
    }
    
    return output;
}

// Register the plugin
REGISTER_PLUGIN(SepiaFilterPlugin)
```

## Building Plugins

### CMakeLists.txt for Plugin

Create a `CMakeLists.txt` file for your plugin:

```cmake
# My Plugin
add_library(my_plugin SHARED
    my_plugin.cpp
)

target_include_directories(my_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_compile_options(my_plugin PRIVATE -g)

# Set output properties
set_target_properties(my_plugin PROPERTIES
    OUTPUT_NAME "my_plugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
)
```

### Building

1. Place your plugin source files in the appropriate directory
2. Add your plugin's CMakeLists.txt to the build system
3. Build with make:

```bash
cd build
make my_plugin
```

## Plugin Configuration

Plugins can be configured through the `PluginConfig` structure:

```cpp
PluginConfig config;
config.allow_file_access = true;
config.allow_network_access = false;
config.blocked_capabilities.push_back("dangerous_operation");
config.max_memory_usage = 50 * 1024 * 1024; // 50MB limit
```

## Security Considerations

The plugin system includes several security features:

### Permission System
- Plugins can be restricted from accessing files, network, or specific capabilities
- Memory usage limits can be enforced
- Capability-based security model

### Safe Loading
- Version compatibility checking
- Dependency validation
- Graceful error handling and recovery

### Sandboxing
- Plugins run in isolated contexts
- Limited access to core application APIs
- Controlled resource access

## Plugin Lifecycle

1. **Discovery**: Plugin files are discovered in search directories
2. **Loading**: Plugin library is loaded and factory function called
3. **Validation**: Metadata and compatibility checks performed
4. **Initialization**: Plugin's `initialize()` method called
5. **Registration**: Extension points registered with the system
6. **Active**: Plugin is available for use
7. **Deactivation**: Plugin can be temporarily disabled
8. **Shutdown**: Plugin's `shutdown()` method called during unload
9. **Unloading**: Plugin library unloaded from memory

## Integration Points

### Event System Integration

Plugins can interact with picexplore's event system:

```cpp
// Subscribe to events
auto subscription_id = context->get_event_bus()->subscribe(
    StateEventType::IMAGE_METADATA_ADDED,
    [this](const StateEvent& event) {
        handle_new_image(event);
    }
);

// Publish events
ImageEvent event(StateEventType::IMAGE_METADATA_UPDATED, hash, path);
context->get_event_bus()->publish(event);
```

### State Access

Plugins can access application state:

```cpp
auto state_store = context->get_state_store();
auto image_state = state_store->get_image_state(hash);
```

### Background Processing

Plugins can submit background tasks:

```cpp
auto thread_manager = context->get_thread_manager();
thread_manager->submit_task([this]() {
    // Background processing
});
```

## Best Practices

1. **Error Handling**: Always handle exceptions gracefully in plugin code
2. **Resource Management**: Clean up resources in the shutdown method
3. **Thread Safety**: Ensure plugin code is thread-safe when needed
4. **Performance**: Avoid blocking operations in main thread callbacks
5. **Compatibility**: Test with different API versions
6. **Documentation**: Provide clear documentation for plugin capabilities
7. **Testing**: Include unit tests for plugin functionality

## Troubleshooting

### Common Issues

1. **Plugin Not Loading**: Check file permissions and library dependencies
2. **Version Incompatibility**: Verify API version requirements
3. **Missing Symbols**: Ensure all required symbols are exported
4. **Initialization Failure**: Check plugin initialization logic and dependencies

### Debugging

- Enable verbose logging in plugin initialization
- Use debugger to step through plugin loading process
- Check plugin search directories and file permissions
- Verify plugin metadata and version compatibility

## API Reference

### Core Interfaces

- `IPlugin`: Base plugin interface
- `PluginContext`: Access to picexplore APIs
- `PluginMetadata`: Plugin information structure
- `PluginConfig`: Security and permission configuration

### Extension Points

- `IImageProcessor`: Image processing extensions
- `IImageFormat`: File format support extensions  
- `IUIComponent`: User interface extensions
- `IDatabaseBackend`: Database backend extensions
- `IMetadataExtractor`: Metadata extraction extensions

### Utility Classes

- `PluginManager`: Central plugin management
- `ExtensionPointRegistry`: Extension point registration
- `PluginVersion`: Version compatibility handling

This plugin system provides a powerful and flexible way to extend picexplore's functionality while maintaining security and stability. Follow this guide to create robust plugins that integrate seamlessly with the picexplore ecosystem.