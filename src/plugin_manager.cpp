/*
 * plugin_manager.cpp - Plugin management system implementation
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

#include "plugin_manager.hpp"
#include "event_bus.hpp"
#include "state_store.hpp"
#include "thread_manager.hpp"
#include "logging.hpp"
#include <filesystem>
#include <algorithm>
#include <dlfcn.h>

namespace fs = std::filesystem;

// API version for plugin compatibility
static const PluginVersion PLUGIN_API_VERSION{1, 0, 0};

PluginContextImpl::PluginContextImpl(EventBus* event_bus, StateStore* state_store,
                                     ThreadManager* thread_manager, const std::string& plugin_name,
                                     const PluginConfig& config)
    : event_bus_(event_bus), state_store_(state_store), thread_manager_(thread_manager),
      plugin_name_(plugin_name), config_(config) {
}

EventBus* PluginContextImpl::get_event_bus() {
    return event_bus_;
}

StateStore* PluginContextImpl::get_state_store() {
    return state_store_;
}

ThreadManager* PluginContextImpl::get_thread_manager() {
    return thread_manager_;
}

void PluginContextImpl::log(const std::string& level, const std::string& message) {
    std::string formatted_message = "[Plugin:" + plugin_name_ + "] " + message;
    
    if (level == "ERROR") {
        std::cerr << "[ERROR] " << formatted_message << std::endl;
    } else if (level == "WARN") {
        std::cerr << "[WARN] " << formatted_message << std::endl;
    } else if (level == "INFO") {
        std::cout << "[INFO] " << formatted_message << std::endl;
    } else {
        std::cout << "[DEBUG] " << formatted_message << std::endl;
    }
}

std::string PluginContextImpl::get_config_value(const std::string& key, const std::string& default_value) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    auto it = config_values_.find(key);
    return (it != config_values_.end()) ? it->second : default_value;
}

void PluginContextImpl::set_config_value(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    config_values_[key] = value;
}

bool PluginContextImpl::has_permission(const std::string& capability) const {
    // Check if capability is explicitly blocked
    return std::find(config_.blocked_capabilities.begin(), 
                    config_.blocked_capabilities.end(), 
                    capability) == config_.blocked_capabilities.end();
}

PluginManager::PluginManager(EventBus* event_bus, StateStore* state_store, ThreadManager* thread_manager)
    : event_bus_(event_bus), state_store_(state_store), thread_manager_(thread_manager) {
}

PluginManager::~PluginManager() {
    shutdown();
}

bool PluginManager::initialize() {
    if (initialized_) {
        return true;
    }
    
    std::cout << "[INFO] Initializing plugin manager" << std::endl;
    
    // Clear any existing extension points
    ExtensionPointRegistry::clear_all();
    
    initialized_ = true;
    return true;
}

void PluginManager::shutdown() {
    if (!initialized_) {
        return;
    }
    
    std::cout << "[INFO] Shutting down plugin manager" << std::endl;
    
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    // Unload all plugins
    for (auto& [name, plugin_info] : plugins_) {
        if (plugin_info->plugin && plugin_info->is_loaded) {
            try {
                plugin_info->plugin->shutdown();
            } catch (const std::exception& e) {
                std::cerr << "[ERROR] Error shutting down plugin '" + name + "': " + e.what() << std::endl;
            }
        }
        
        if (plugin_info->library_handle) {
            unload_library(plugin_info->library_handle);
        }
    }
    
    plugins_.clear();
    ExtensionPointRegistry::clear_all();
    initialized_ = false;
}

PluginLoadResult PluginManager::load_plugin(const std::string& plugin_path, const PluginConfig& config) {
    if (!initialized_) {
        return PluginLoadResult(PluginLoadStatus::INITIALIZATION_FAILED, "Plugin manager not initialized");
    }
    
    if (!fs::exists(plugin_path)) {
        return PluginLoadResult(PluginLoadStatus::FILE_NOT_FOUND, "Plugin file not found: " + plugin_path);
    }
    
    if (!is_valid_plugin_file(plugin_path)) {
        return PluginLoadResult(PluginLoadStatus::INVALID_PLUGIN, "Invalid plugin file format: " + plugin_path);
    }
    
    // Load the library
    void* library_handle = load_library(plugin_path);
    if (!library_handle) {
        return PluginLoadResult(PluginLoadStatus::INVALID_PLUGIN, "Failed to load library: " + plugin_path);
    }
    
    // Get the plugin factory function
    CreatePluginFunction create_plugin = get_plugin_factory(library_handle);
    if (!create_plugin) {
        unload_library(library_handle);
        return PluginLoadResult(PluginLoadStatus::INVALID_PLUGIN, "Plugin factory function not found");
    }
    
    // Create the plugin instance
    std::unique_ptr<IPlugin> plugin;
    try {
        plugin = create_plugin();
    } catch (const std::exception& e) {
        unload_library(library_handle);
        return PluginLoadResult(PluginLoadStatus::INVALID_PLUGIN, "Failed to create plugin instance: " + std::string(e.what()));
    }
    
    if (!plugin) {
        unload_library(library_handle);
        return PluginLoadResult(PluginLoadStatus::INVALID_PLUGIN, "Plugin factory returned null");
    }
    
    // Get plugin metadata
    const PluginMetadata& metadata = plugin->get_metadata();
    
    // Check if plugin is already loaded
    {
        std::lock_guard<std::mutex> lock(plugins_mutex_);
        if (plugins_.find(metadata.name) != plugins_.end()) {
            unload_library(library_handle);
            return PluginLoadResult(PluginLoadStatus::ALREADY_LOADED, "Plugin already loaded: " + metadata.name);
        }
    }
    
    // Check compatibility
    if (!is_plugin_compatible(metadata)) {
        unload_library(library_handle);
        return PluginLoadResult(PluginLoadStatus::VERSION_INCOMPATIBLE, 
                               "Plugin API version incompatible: " + metadata.required_api_version.to_string());
    }
    
    // Check dependencies
    if (!check_dependencies(metadata)) {
        unload_library(library_handle);
        return PluginLoadResult(PluginLoadStatus::DEPENDENCY_MISSING, "Plugin dependencies not satisfied");
    }
    
    // Validate permissions
    if (!validate_plugin_permissions(metadata, config)) {
        unload_library(library_handle);
        return PluginLoadResult(PluginLoadStatus::PERMISSION_DENIED, "Plugin permissions validation failed");
    }
    
    // Create plugin context
    auto context = std::make_unique<PluginContextImpl>(event_bus_, state_store_, thread_manager_, 
                                                      metadata.name, config);
    
    // Initialize the plugin
    bool init_success = false;
    try {
        init_success = plugin->initialize(context.get());
    } catch (const std::exception& e) {
        unload_library(library_handle);
        return PluginLoadResult(PluginLoadStatus::INITIALIZATION_FAILED, 
                               "Plugin initialization failed: " + std::string(e.what()));
    }
    
    if (!init_success) {
        unload_library(library_handle);
        return PluginLoadResult(PluginLoadStatus::INITIALIZATION_FAILED, "Plugin initialization returned false");
    }
    
    // Create plugin info and store it
    auto plugin_info = std::make_unique<PluginInfo>();
    plugin_info->file_path = plugin_path;
    plugin_info->plugin = std::move(plugin);
    plugin_info->context = std::move(context);
    plugin_info->library_handle = library_handle;
    plugin_info->config = config;
    plugin_info->is_loaded = true;
    plugin_info->is_active = true;
    
    {
        std::lock_guard<std::mutex> lock(plugins_mutex_);
        plugins_[metadata.name] = std::move(plugin_info);
    }
    
    std::cout << "[INFO] Successfully loaded plugin: " + metadata.name + " v" + metadata.version.to_string() << std::endl;
    return PluginLoadResult(PluginLoadStatus::SUCCESS, "Plugin loaded successfully", metadata.name);
}

bool PluginManager::unload_plugin(const std::string& plugin_name) {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    auto it = plugins_.find(plugin_name);
    if (it == plugins_.end()) {
        return false;
    }
    
    auto& plugin_info = it->second;
    
    // Shutdown the plugin
    if (plugin_info->plugin && plugin_info->is_loaded) {
        try {
            plugin_info->plugin->shutdown();
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Error shutting down plugin '" + plugin_name + "': " + e.what() << std::endl;
        }
    }
    
    // Unload the library
    if (plugin_info->library_handle) {
        unload_library(plugin_info->library_handle);
    }
    
    // Remove from map
    plugins_.erase(it);
    
    std::cout << "[INFO] Unloaded plugin: " + plugin_name << std::endl;
    return true;
}

std::vector<std::string> PluginManager::get_loaded_plugins() const {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    std::vector<std::string> result;
    for (const auto& [name, plugin_info] : plugins_) {
        if (plugin_info->is_loaded) {
            result.push_back(name);
        }
    }
    return result;
}

const PluginMetadata* PluginManager::get_plugin_metadata(const std::string& plugin_name) const {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    auto it = plugins_.find(plugin_name);
    if (it != plugins_.end() && it->second->plugin) {
        return &it->second->plugin->get_metadata();
    }
    return nullptr;
}

bool PluginManager::set_plugin_active(const std::string& plugin_name, bool active) {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    auto it = plugins_.find(plugin_name);
    if (it != plugins_.end() && it->second->plugin) {
        it->second->plugin->set_active(active);
        it->second->is_active = active;
        return true;
    }
    return false;
}

bool PluginManager::is_plugin_active(const std::string& plugin_name) const {
    std::lock_guard<std::mutex> lock(plugins_mutex_);
    
    auto it = plugins_.find(plugin_name);
    if (it != plugins_.end()) {
        return it->second->is_active && it->second->plugin && it->second->plugin->is_active();
    }
    return false;
}

std::vector<PluginLoadResult> PluginManager::scan_and_load_plugins(const std::string& directory_path,
                                                                  const PluginConfig& default_config) {
    std::vector<PluginLoadResult> results;
    
    if (!fs::exists(directory_path) || !fs::is_directory(directory_path)) {
        results.emplace_back(PluginLoadStatus::FILE_NOT_FOUND, "Plugin directory not found: " + directory_path);
        return results;
    }
    
    std::string pattern = get_plugin_file_pattern();
    
    try {
        for (const auto& entry : fs::directory_iterator(directory_path)) {
            if (entry.is_regular_file() && is_valid_plugin_file(entry.path().string())) {
                auto result = load_plugin(entry.path().string(), default_config);
                results.push_back(result);
            }
        }
    } catch (const std::exception& e) {
        results.emplace_back(PluginLoadStatus::FILE_NOT_FOUND, "Error scanning directory: " + std::string(e.what()));
    }
    
    return results;
}

std::vector<std::string> PluginManager::get_default_plugin_directories() const {
    std::vector<std::string> dirs;
    
    // Add common plugin directories
    dirs.push_back("./plugins");
    dirs.push_back("~/.picexplore/plugins");
    dirs.push_back("/usr/local/lib/picexplore/plugins");
    dirs.push_back("/usr/lib/picexplore/plugins");
    
    return dirs;
}

void PluginManager::register_extension_hook(ExtensionPointType type, ExtensionHook hook) {
    ExtensionPointRegistry::register_hook(type, hook);
}

std::vector<IExtensionPoint*> PluginManager::get_extension_points(ExtensionPointType type) const {
    return ExtensionPointRegistry::get_extension_points(type);
}

PluginVersion PluginManager::get_api_version() {
    return PLUGIN_API_VERSION;
}

bool PluginManager::is_plugin_compatible(const PluginMetadata& metadata) const {
    return metadata.required_api_version.is_compatible_with(PLUGIN_API_VERSION);
}

// Private helper methods

void* PluginManager::load_library(const std::string& library_path) {
    void* handle = dlopen(library_path.c_str(), RTLD_LAZY);
    if (!handle) {
        std::cerr << "[ERROR] Failed to load library: " + std::string(dlerror()) << std::endl;
    }
    return handle;
}

void PluginManager::unload_library(void* library_handle) {
    if (library_handle) {
        if (dlclose(library_handle) != 0) {
            std::cerr << "[ERROR] Failed to unload library: " + std::string(dlerror()) << std::endl;
        }
    }
}

CreatePluginFunction PluginManager::get_plugin_factory(void* library_handle) {
    if (!library_handle) {
        return nullptr;
    }
    
    void* symbol = dlsym(library_handle, "create_plugin");
    if (!symbol) {
        std::cerr << "[ERROR] Plugin factory function not found: " + std::string(dlerror()) << std::endl;
        return nullptr;
    }
    
    return reinterpret_cast<CreatePluginFunction>(symbol);
}

bool PluginManager::is_valid_plugin_file(const std::string& file_path) const {
    // Check file extension
    std::string extension = fs::path(file_path).extension().string();
    return extension == ".so" || extension == ".dylib" || extension == ".dll";
}

std::string PluginManager::get_plugin_file_pattern() const {
#ifdef _WIN32
    return "*.dll";
#elif defined(__APPLE__)
    return "*.dylib";
#else
    return "*.so";
#endif
}

bool PluginManager::validate_plugin_permissions(const PluginMetadata& metadata, const PluginConfig& config) const {
    // Basic permission validation - can be extended
    return true;
}

bool PluginManager::check_dependencies(const PluginMetadata& metadata) const {
    // For now, assume all dependencies are satisfied
    // This could be extended to check for specific plugins or libraries
    return true;
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s