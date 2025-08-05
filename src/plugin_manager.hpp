/*
 * plugin_manager.hpp - Plugin management system for picexplore
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
#include <unordered_map>
#include <functional>
#include <mutex>
#include "plugin_interface.hpp"
#include "extension_points.hpp"

// Forward declarations
class EventBus;
class StateStore;
class ThreadManager;

/**
 * Plugin load result status
 */
enum class PluginLoadStatus {
    SUCCESS,
    FILE_NOT_FOUND,
    INVALID_PLUGIN,
    VERSION_INCOMPATIBLE,
    DEPENDENCY_MISSING,
    INITIALIZATION_FAILED,
    ALREADY_LOADED,
    PERMISSION_DENIED
};

/**
 * Plugin load result
 */
struct PluginLoadResult {
    PluginLoadStatus status;
    std::string message;
    std::string plugin_name;
    
    PluginLoadResult(PluginLoadStatus s, const std::string& msg = "", const std::string& name = "")
        : status(s), message(msg), plugin_name(name) {}
    
    bool is_success() const { return status == PluginLoadStatus::SUCCESS; }
};

/**
 * Internal plugin context implementation
 */
class PluginContextImpl : public PluginContext {
public:
    PluginContextImpl(EventBus* event_bus, StateStore* state_store, 
                      ThreadManager* thread_manager, const std::string& plugin_name,
                      const PluginConfig& config);
    
    // PluginContext implementation
    EventBus* get_event_bus() override;
    StateStore* get_state_store() override;
    ThreadManager* get_thread_manager() override;
    void log(const std::string& level, const std::string& message) override;
    std::string get_config_value(const std::string& key, const std::string& default_value) override;
    void set_config_value(const std::string& key, const std::string& value) override;
    
    /**
     * Check if plugin has permission for a specific capability
     */
    bool has_permission(const std::string& capability) const;
    
    /**
     * Get plugin configuration
     */
    const PluginConfig& get_config() const { return config_; }

private:
    EventBus* event_bus_;
    StateStore* state_store_;
    ThreadManager* thread_manager_;
    std::string plugin_name_;
    PluginConfig config_;
    std::unordered_map<std::string, std::string> config_values_;
    mutable std::mutex config_mutex_;
};

/**
 * Plugin information structure
 */
struct PluginInfo {
    std::string file_path;
    std::unique_ptr<IPlugin> plugin;
    std::unique_ptr<PluginContextImpl> context;
    void* library_handle = nullptr;
    PluginConfig config;
    bool is_loaded = false;
    bool is_active = false;
    
    PluginInfo() = default;
    PluginInfo(PluginInfo&& other) noexcept
        : file_path(std::move(other.file_path)),
          plugin(std::move(other.plugin)),
          context(std::move(other.context)),
          library_handle(other.library_handle),
          config(std::move(other.config)),
          is_loaded(other.is_loaded),
          is_active(other.is_active) {
        other.library_handle = nullptr;
    }
    
    PluginInfo& operator=(PluginInfo&& other) noexcept {
        if (this != &other) {
            file_path = std::move(other.file_path);
            plugin = std::move(other.plugin);
            context = std::move(other.context);
            library_handle = other.library_handle;
            config = std::move(other.config);
            is_loaded = other.is_loaded;
            is_active = other.is_active;
            other.library_handle = nullptr;
        }
        return *this;
    }
};

/**
 * Plugin manager - Central management system for plugins
 */
class PluginManager {
public:
    /**
     * Constructor
     */
    PluginManager(EventBus* event_bus, StateStore* state_store, ThreadManager* thread_manager);
    
    /**
     * Destructor - unloads all plugins
     */
    ~PluginManager();
    
    /**
     * Initialize the plugin manager
     */
    bool initialize();
    
    /**
     * Shutdown plugin manager and unload all plugins
     */
    void shutdown();
    
    /**
     * Load a plugin from a shared library file
     */
    PluginLoadResult load_plugin(const std::string& plugin_path, 
                                const PluginConfig& config = PluginConfig{});
    
    /**
     * Unload a plugin by name
     */
    bool unload_plugin(const std::string& plugin_name);
    
    /**
     * Get list of loaded plugin names
     */
    std::vector<std::string> get_loaded_plugins() const;
    
    /**
     * Get plugin metadata by name
     */
    const PluginMetadata* get_plugin_metadata(const std::string& plugin_name) const;
    
    /**
     * Enable or disable a plugin
     */
    bool set_plugin_active(const std::string& plugin_name, bool active);
    
    /**
     * Check if a plugin is active
     */
    bool is_plugin_active(const std::string& plugin_name) const;
    
    /**
     * Scan a directory for plugin files and load them
     */
    std::vector<PluginLoadResult> scan_and_load_plugins(const std::string& directory_path,
                                                       const PluginConfig& default_config = PluginConfig{});
    
    /**
     * Get default plugin search directories
     */
    std::vector<std::string> get_default_plugin_directories() const;
    
    /**
     * Register an extension point hook
     */
    void register_extension_hook(ExtensionPointType type, ExtensionHook hook);
    
    /**
     * Get all extension points of a specific type from loaded plugins
     */
    std::vector<IExtensionPoint*> get_extension_points(ExtensionPointType type) const;
    
    /**
     * Get current API version
     */
    static PluginVersion get_api_version();
    
    /**
     * Validate plugin compatibility
     */
    bool is_plugin_compatible(const PluginMetadata& metadata) const;

private:
    EventBus* event_bus_;
    StateStore* state_store_;
    ThreadManager* thread_manager_;
    
    mutable std::mutex plugins_mutex_;
    std::unordered_map<std::string, std::unique_ptr<PluginInfo>> plugins_;
    
    bool initialized_ = false;
    
    // Helper methods
    void* load_library(const std::string& library_path);
    void unload_library(void* library_handle);
    CreatePluginFunction get_plugin_factory(void* library_handle);
    bool is_valid_plugin_file(const std::string& file_path) const;
    std::string get_plugin_file_pattern() const;
    void notify_extension_hooks(ExtensionPointType type, IExtensionPoint* extension);
    
    // Security validation
    bool validate_plugin_permissions(const PluginMetadata& metadata, const PluginConfig& config) const;
    bool check_dependencies(const PluginMetadata& metadata) const;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s