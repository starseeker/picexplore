# Picexplore Plugin System

## Overview

Picexplore now includes a powerful plugin system that allows extending functionality at runtime through secure, well-defined APIs. This system provides multiple extension points for customizing image processing, file format support, user interface components, database backends, and metadata extraction.

## Quick Start

### Using Plugins

Plugins are automatically discovered and loaded from these directories:
- `./plugins/` (relative to executable)
- `~/.picexplore/plugins/`
- `/usr/local/lib/picexplore/plugins/`
- `/usr/lib/picexplore/plugins/`

Simply place plugin `.so` files in any of these directories and restart picexplore.

### Building Sample Plugins

The repository includes two sample plugins:

```bash
cd build
make sepia_filter_plugin        # Image processing plugin
make simple_metadata_plugin     # Metadata extraction plugin
```

### Developing New Plugins

See the comprehensive [Plugin Development Guide](docs/PLUGIN_DEVELOPMENT_GUIDE.md) for detailed instructions on creating custom plugins.

## Available Extension Points

### 1. Image Processing (`IImageProcessor`)
- Apply filters and effects to images
- Custom enhancement algorithms
- Real-time image transformations

**Example**: Sepia filter plugin that applies vintage tone effects

### 2. File Format Support (`IImageFormat`)
- Add support for new image formats
- Custom import/export capabilities
- Specialized format handling

**Example**: Support for RAW camera formats, scientific imaging formats

### 3. UI Components (`IUIComponent`)
- Custom widgets and controls
- Alternative layout algorithms
- Specialized visualization tools

**Example**: Custom metadata display panels, alternative thumbnail viewers

### 4. Database Backends (`IDatabaseBackend`)
- Alternative storage systems
- Cloud storage integration
- Performance optimizations

**Example**: PostgreSQL backend, distributed storage systems

### 5. Metadata Extraction (`IMetadataExtractor`)
- Custom metadata parsing
- AI-powered content analysis
- Specialized format metadata

**Example**: Simple metadata plugin extracts file properties and basic image information

## Plugin Architecture

### Core Components

- **Plugin Manager**: Central loading and lifecycle management
- **Extension Registry**: Discovery and registration of plugin capabilities
- **Plugin Context**: Secure API access to picexplore functionality
- **Version System**: Compatibility checking and dependency management

### Security Features

- **Permission System**: Capability-based access control
- **Resource Limits**: Memory and processing constraints
- **Safe Loading**: Error handling and recovery
- **API Isolation**: Controlled access to core functionality

### Integration Points

- **Event System**: Subscribe to and publish application events
- **State Management**: Access to application state and data
- **Background Processing**: Submit tasks to thread pool
- **Configuration**: Plugin-specific settings and preferences

## Sample Plugins

### Sepia Filter Plugin (`sepia_filter`)

Demonstrates image processing extension point:
- Applies sepia tone effect to images
- Configurable intensity parameter
- Processes RGBA image data
- Shows proper plugin lifecycle management

### Simple Metadata Plugin (`simple_metadata`)

Demonstrates metadata extraction extension point:
- Extracts basic file properties (size, permissions, timestamps)
- Identifies image formats by extension
- Provides metadata keys for UI display
- Shows file system integration

## Plugin Development

### Basic Plugin Structure

```cpp
#include "plugin_interface.hpp"
#include "extension_points.hpp"

class MyPlugin : public IPlugin {
public:
    // Implement required interface methods
    const PluginMetadata& get_metadata() const override;
    bool initialize(PluginContext* context) override;
    void shutdown() override;
    // ... other methods
};

// Register the plugin
REGISTER_PLUGIN(MyPlugin)
```

### Extension Point Implementation

```cpp
class MyImageProcessor : public IImageProcessor {
public:
    std::string get_name() const override { return "MyProcessor"; }
    
    std::unique_ptr<uint8_t[]> process_image(
        const uint8_t* image_data, int width, int height, int channels,
        const std::unordered_map<std::string, std::string>& parameters) override {
        // Process image data
        return processed_data;
    }
    // ... other methods
};
```

### Plugin Registration

```cpp
bool MyPlugin::initialize(PluginContext* context) {
    auto processor = std::make_unique<MyImageProcessor>();
    ExtensionPointRegistry::register_extension_point(
        std::unique_ptr<IExtensionPoint>(processor.release())
    );
    return true;
}
```

## Building and Installation

### Prerequisites

- C++17 compatible compiler
- CMake 3.12 or later
- Standard development libraries (already required for picexplore)

### Building

```bash
mkdir build && cd build
cmake ..
make picexplore_mvc              # Main application with plugin support
make sepia_filter_plugin         # Sample image processing plugin
make simple_metadata_plugin      # Sample metadata extraction plugin
make plugin_manager_test         # Plugin system unit tests
```

### Testing

```bash
# Run plugin system tests
./test/plugin_manager_test

# Test application with plugins
./src/picexplore_mvc --help
```

## API Reference

### Core Interfaces

- **`IPlugin`**: Base interface all plugins must implement
- **`PluginContext`**: Provides access to picexplore APIs
- **`PluginMetadata`**: Plugin information and version data
- **`PluginConfig`**: Security and permission configuration

### Extension Interfaces

- **`IImageProcessor`**: Image processing and filtering
- **`IImageFormat`**: File format import/export support
- **`IUIComponent`**: User interface extensions
- **`IDatabaseBackend`**: Storage backend implementations
- **`IMetadataExtractor`**: Custom metadata extraction

### Management Classes

- **`PluginManager`**: Plugin loading and lifecycle management
- **`ExtensionPointRegistry`**: Extension discovery and registration
- **`PluginVersion`**: Version compatibility handling

## Future Extensions

The plugin system is designed for easy extension with additional extension points:

- **Similarity Analyzers**: Custom image similarity algorithms
- **Export Formats**: PDF, web gallery, and report generators
- **Network Providers**: Cloud storage and sharing services
- **Authentication**: User management and access control
- **Scripting**: Integration with Python, Lua, or JavaScript

## Contributing

To contribute new extension points or improvements to the plugin system:

1. Review the [Plugin Development Guide](docs/PLUGIN_DEVELOPMENT_GUIDE.md)
2. Study existing sample plugins for patterns
3. Ensure thread safety and proper error handling
4. Add comprehensive tests for new functionality
5. Update documentation with new capabilities

The plugin system provides a robust foundation for extending picexplore while maintaining security, performance, and stability.