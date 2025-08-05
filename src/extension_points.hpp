/*
 * extension_points.hpp - Extension point interfaces for picexplore plugin system
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

// Forward declarations for image data structures
struct ImageInfo;

/**
 * Extension point types enumeration
 */
enum class ExtensionPointType {
    IMAGE_PROCESSOR,    // Image processing and filtering
    IMAGE_FORMAT,       // Import/export file format support
    UI_COMPONENT,       // User interface components and views
    DATABASE_BACKEND,   // Database storage backends  
    METADATA_EXTRACTOR, // Metadata extraction from images
    THUMBNAIL_GENERATOR,// Custom thumbnail generation
    SIMILARITY_ANALYZER // Image similarity analysis
};

/**
 * Base extension point interface
 */
class IExtensionPoint {
public:
    virtual ~IExtensionPoint() = default;
    
    /**
     * Get the extension point type
     */
    virtual ExtensionPointType get_type() const = 0;
    
    /**
     * Get a human-readable name for this extension point
     */
    virtual std::string get_name() const = 0;
    
    /**
     * Get supported capabilities/features
     */
    virtual std::vector<std::string> get_capabilities() const = 0;
};

/**
 * Image processing extension point
 * Allows plugins to process images during scanning or display
 */
class IImageProcessor : public IExtensionPoint {
public:
    ExtensionPointType get_type() const override { return ExtensionPointType::IMAGE_PROCESSOR; }
    
    /**
     * Process image data and return modified version
     * @param image_data Raw image data (RGBA format)
     * @param width Image width in pixels
     * @param height Image height in pixels
     * @param channels Number of color channels (3 for RGB, 4 for RGBA)
     * @param parameters Processing parameters (filter-specific)
     * @return Processed image data or nullptr if processing failed
     */
    virtual std::unique_ptr<uint8_t[]> process_image(
        const uint8_t* image_data, 
        int width, 
        int height, 
        int channels,
        const std::unordered_map<std::string, std::string>& parameters) = 0;
    
    /**
     * Get list of supported processing parameters
     */
    virtual std::vector<std::string> get_parameter_names() const = 0;
    
    /**
     * Get default value for a parameter
     */
    virtual std::string get_parameter_default(const std::string& param_name) const = 0;
};

/**
 * Image format extension point
 * Allows plugins to add support for new image file formats
 */
class IImageFormat : public IExtensionPoint {
public:
    ExtensionPointType get_type() const override { return ExtensionPointType::IMAGE_FORMAT; }
    
    /**
     * Get supported file extensions
     */
    virtual std::vector<std::string> get_supported_extensions() const = 0;
    
    /**
     * Check if this format can handle a specific file
     */
    virtual bool can_handle_file(const std::string& file_path) const = 0;
    
    /**
     * Load image from file
     * @param file_path Path to image file
     * @param width Output: image width
     * @param height Output: image height  
     * @param channels Output: number of channels
     * @return Image data in RGBA format or nullptr if failed
     */
    virtual std::unique_ptr<uint8_t[]> load_image(
        const std::string& file_path,
        int& width,
        int& height,
        int& channels) = 0;
    
    /**
     * Save image to file
     * @param file_path Output file path
     * @param image_data Image data in RGBA format
     * @param width Image width
     * @param height Image height
     * @param channels Number of channels
     * @param quality Quality setting (0-100, format-dependent)
     * @return true if save succeeded
     */
    virtual bool save_image(
        const std::string& file_path,
        const uint8_t* image_data,
        int width,
        int height,
        int channels,
        int quality = 90) = 0;
};

/**
 * UI component extension point
 * Allows plugins to add custom UI components and views
 */
class IUIComponent : public IExtensionPoint {
public:
    ExtensionPointType get_type() const override { return ExtensionPointType::UI_COMPONENT; }
    
    /**
     * Create the UI component
     * @param parent Parent widget/window
     * @param x X position
     * @param y Y position  
     * @param width Component width
     * @param height Component height
     * @return Pointer to created widget (FLTK Fl_Widget*)
     */
    virtual void* create_component(void* parent, int x, int y, int width, int height) = 0;
    
    /**
     * Update component with new data
     */
    virtual void update_component(void* component, const std::unordered_map<std::string, std::string>& data) = 0;
    
    /**
     * Get preferred size for the component
     */
    virtual void get_preferred_size(int& width, int& height) const = 0;
    
    /**
     * Handle component events
     */
    virtual bool handle_event(void* component, const std::string& event_type, const std::string& event_data) = 0;
};

/**
 * Database backend extension point
 * Allows plugins to provide alternative database storage implementations
 */
class IDatabaseBackend : public IExtensionPoint {
public:
    ExtensionPointType get_type() const override { return ExtensionPointType::DATABASE_BACKEND; }
    
    /**
     * Open/create database
     */
    virtual bool open_database(const std::string& database_path) = 0;
    
    /**
     * Close database
     */
    virtual void close_database() = 0;
    
    /**
     * Store image metadata
     */
    virtual bool store_image_info(const ImageInfo& image_info) = 0;
    
    /**
     * Retrieve image metadata
     */
    virtual bool get_image_info(const std::string& hash, ImageInfo& image_info) = 0;
    
    /**
     * List all image hashes
     */
    virtual std::vector<std::string> get_all_image_hashes() = 0;
    
    /**
     * Delete image metadata
     */
    virtual bool delete_image_info(const std::string& hash) = 0;
    
    /**
     * Store thumbnail data
     */
    virtual bool store_thumbnail(const std::string& hash, int size, 
                               const std::vector<uint8_t>& thumbnail_data) = 0;
    
    /**
     * Retrieve thumbnail data
     */
    virtual bool get_thumbnail(const std::string& hash, int size, 
                             std::vector<uint8_t>& thumbnail_data) = 0;
};

/**
 * Metadata extractor extension point
 * Allows plugins to extract custom metadata from images
 */
class IMetadataExtractor : public IExtensionPoint {
public:
    ExtensionPointType get_type() const override { return ExtensionPointType::METADATA_EXTRACTOR; }
    
    /**
     * Extract metadata from image file
     * @param file_path Path to image file
     * @param metadata Output metadata key-value pairs
     * @return true if extraction succeeded
     */
    virtual bool extract_metadata(const std::string& file_path,
                                std::unordered_map<std::string, std::string>& metadata) = 0;
    
    /**
     * Get list of metadata keys this extractor provides
     */
    virtual std::vector<std::string> get_metadata_keys() const = 0;
    
    /**
     * Get supported file types for metadata extraction
     */
    virtual std::vector<std::string> get_supported_file_types() const = 0;
};

/**
 * Extension point hook function type
 * Used to register callbacks for extension points
 */
using ExtensionHook = std::function<void(IExtensionPoint*)>;

/**
 * Extension point registry
 * Manages registration and lookup of extension points
 */
class ExtensionPointRegistry {
public:
    /**
     * Register an extension point implementation
     */
    static void register_extension_point(std::unique_ptr<IExtensionPoint> extension) {
        if (!extension) {
            return;
        }
        
        std::lock_guard<std::mutex> lock(get_mutex());
        
        ExtensionPointType type = extension->get_type();
        std::string name = extension->get_name();
        
        // Store by type
        get_extensions()[type].push_back(std::move(extension));
        
        // Store by name for quick lookup
        IExtensionPoint* ext_ptr = get_extensions()[type].back().get();
        get_extensions_by_name()[name] = ext_ptr;
        
        // Notify registered hooks
        if (get_hooks().find(type) != get_hooks().end()) {
            for (const auto& hook : get_hooks()[type]) {
                hook(ext_ptr);
            }
        }
    }
    
    /**
     * Get all extension points of a specific type
     */
    static std::vector<IExtensionPoint*> get_extension_points(ExtensionPointType type) {
        std::lock_guard<std::mutex> lock(get_mutex());
        
        std::vector<IExtensionPoint*> result;
        if (get_extensions().find(type) != get_extensions().end()) {
            for (const auto& ext : get_extensions()[type]) {
                result.push_back(ext.get());
            }
        }
        return result;
    }
    
    /**
     * Get extension point by name
     */
    static IExtensionPoint* get_extension_point(const std::string& name) {
        std::lock_guard<std::mutex> lock(get_mutex());
        
        auto it = get_extensions_by_name().find(name);
        return (it != get_extensions_by_name().end()) ? it->second : nullptr;
    }
    
    /**
     * Register hook to be called when new extension points are added
     */
    static void register_hook(ExtensionPointType type, ExtensionHook hook) {
        std::lock_guard<std::mutex> lock(get_mutex());
        
        get_hooks()[type].push_back(hook);
        
        // Call hook immediately for any existing extensions of this type
        if (get_extensions().find(type) != get_extensions().end()) {
            for (const auto& ext : get_extensions()[type]) {
                hook(ext.get());
            }
        }
    }
    
    /**
     * Unregister all extension points (for cleanup)
     */
    static void clear_all() {
        std::lock_guard<std::mutex> lock(get_mutex());
        
        get_extensions().clear();
        get_extensions_by_name().clear();
        get_hooks().clear();
    }

private:
    // Use static functions to avoid initialization order issues
    static std::unordered_map<ExtensionPointType, std::vector<std::unique_ptr<IExtensionPoint>>>& get_extensions() {
        static std::unordered_map<ExtensionPointType, std::vector<std::unique_ptr<IExtensionPoint>>> extensions;
        return extensions;
    }
    
    static std::unordered_map<std::string, IExtensionPoint*>& get_extensions_by_name() {
        static std::unordered_map<std::string, IExtensionPoint*> extensions_by_name;
        return extensions_by_name;
    }
    
    static std::unordered_map<ExtensionPointType, std::vector<ExtensionHook>>& get_hooks() {
        static std::unordered_map<ExtensionPointType, std::vector<ExtensionHook>> hooks;
        return hooks;
    }
    
    static std::mutex& get_mutex() {
        static std::mutex registry_mutex;
        return registry_mutex;
    }
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s