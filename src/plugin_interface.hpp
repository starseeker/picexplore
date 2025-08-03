/*
 * plugin_interface.hpp - Base plugin interface for picexplore plugin system
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

/**
 * Plugin version structure for compatibility checking
 */
struct PluginVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
    
    PluginVersion(int maj = 0, int min = 0, int p = 0) 
        : major(maj), minor(min), patch(p) {}
    
    std::string to_string() const {
        return std::to_string(major) + "." + 
               std::to_string(minor) + "." + 
               std::to_string(patch);
    }
    
    bool is_compatible_with(const PluginVersion& api_version) const {
        // Major version must match, minor version can be lower or equal
        return major == api_version.major && minor <= api_version.minor;
    }
};

/**
 * Plugin metadata structure
 */
struct PluginMetadata {
    std::string name;
    std::string description;
    std::string author;
    PluginVersion version;
    PluginVersion required_api_version;
    std::vector<std::string> dependencies;
    std::vector<std::string> capabilities;
    
    PluginMetadata() = default;
    PluginMetadata(const std::string& plugin_name, 
                   const std::string& plugin_desc,
                   const std::string& plugin_author,
                   const PluginVersion& plugin_ver,
                   const PluginVersion& api_ver = {1, 0, 0})
        : name(plugin_name), description(plugin_desc), author(plugin_author),
          version(plugin_ver), required_api_version(api_ver) {}
};

/**
 * Plugin configuration for security and permissions
 */
struct PluginConfig {
    bool allow_file_access = true;
    bool allow_network_access = false;
    bool allow_ui_extensions = true;
    bool allow_database_modifications = true;
    std::vector<std::string> allowed_directories;
    std::vector<std::string> blocked_capabilities;
    size_t max_memory_usage = 100 * 1024 * 1024; // 100MB default
};

/**
 * Plugin context - provides access to picexplore APIs
 */
class PluginContext {
public:
    virtual ~PluginContext() = default;
    
    /**
     * Get reference to the event bus for plugin communication
     */
    virtual class EventBus* get_event_bus() = 0;
    
    /**
     * Get reference to the state store for accessing application state
     */
    virtual class StateStore* get_state_store() = 0;
    
    /**
     * Get reference to the thread manager for background operations
     */
    virtual class ThreadManager* get_thread_manager() = 0;
    
    /**
     * Log a message from the plugin
     */
    virtual void log(const std::string& level, const std::string& message) = 0;
    
    /**
     * Get plugin configuration value
     */
    virtual std::string get_config_value(const std::string& key, 
                                       const std::string& default_value = "") = 0;
    
    /**
     * Set plugin configuration value
     */
    virtual void set_config_value(const std::string& key, const std::string& value) = 0;
};

/**
 * Base plugin interface
 * All plugins must inherit from this interface
 */
class IPlugin {
public:
    virtual ~IPlugin() = default;
    
    /**
     * Get plugin metadata
     */
    virtual const PluginMetadata& get_metadata() const = 0;
    
    /**
     * Initialize the plugin with the provided context
     * Called when the plugin is loaded
     */
    virtual bool initialize(PluginContext* context) = 0;
    
    /**
     * Shutdown and cleanup plugin resources
     * Called when the plugin is unloaded
     */
    virtual void shutdown() = 0;
    
    /**
     * Get the API version this plugin was compiled against
     */
    virtual PluginVersion get_api_version() const = 0;
    
    /**
     * Check if the plugin is currently active
     */
    virtual bool is_active() const = 0;
    
    /**
     * Enable or disable the plugin
     */
    virtual void set_active(bool active) = 0;
};

/**
 * Plugin factory function type
 * Each plugin library must export a create_plugin function
 */
using CreatePluginFunction = std::unique_ptr<IPlugin>(*)();

/**
 * Plugin factory registration macro
 * Use this in plugin implementation files to register the plugin
 */
#define REGISTER_PLUGIN(plugin_class) \
    extern "C" { \
        std::unique_ptr<IPlugin> create_plugin() { \
            return std::make_unique<plugin_class>(); \
        } \
    }

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s