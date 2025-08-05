/*
 * simple_metadata_plugin.cpp - Sample metadata extractor plugin for picexplore
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

#include "../../../plugin_interface.hpp"
#include "../../../extension_points.hpp"
#include <filesystem>
#include <fstream>
#include <memory>

namespace fs = std::filesystem;

/**
 * Simple metadata extractor implementation
 */
class SimpleMetadataExtractor : public IMetadataExtractor {
public:
    std::string get_name() const override {
        return "SimpleMetadataExtractor";
    }
    
    std::vector<std::string> get_capabilities() const override {
        return {"basic_metadata", "file_properties"};
    }
    
    bool extract_metadata(const std::string& file_path,
                         std::unordered_map<std::string, std::string>& metadata) override {
        try {
            if (!fs::exists(file_path)) {
                return false;
            }
            
            auto file_stat = fs::status(file_path);
            auto file_size = fs::file_size(file_path);
            auto write_time = fs::last_write_time(file_path);
            
            // Extract basic file metadata
            metadata["file_name"] = fs::path(file_path).filename().string();
            metadata["file_extension"] = fs::path(file_path).extension().string();
            metadata["file_size"] = std::to_string(file_size);
            
            // Convert write time to string (simplified)
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                write_time - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            auto time_t = std::chrono::system_clock::to_time_t(sctp);
            metadata["modified_time"] = std::to_string(time_t);
            
            // Add file permissions
            auto perms = file_stat.permissions();
            metadata["readable"] = (perms & fs::perms::owner_read) != fs::perms::none ? "true" : "false";
            metadata["writable"] = (perms & fs::perms::owner_write) != fs::perms::none ? "true" : "false";
            
            // Simple image detection based on extension
            std::string ext = fs::path(file_path).extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (ext == ".jpg" || ext == ".jpeg") {
                metadata["image_format"] = "JPEG";
                metadata["has_exif"] = "potentially";
            } else if (ext == ".png") {
                metadata["image_format"] = "PNG";
                metadata["has_transparency"] = "potentially";
            } else if (ext == ".bmp") {
                metadata["image_format"] = "BMP";
            } else if (ext == ".tga") {
                metadata["image_format"] = "TGA";
            }
            
            return true;
            
        } catch (const std::exception& e) {
            return false;
        }
    }
    
    std::vector<std::string> get_metadata_keys() const override {
        return {
            "file_name", "file_extension", "file_size", "modified_time",
            "readable", "writable", "image_format", "has_exif", "has_transparency"
        };
    }
    
    std::vector<std::string> get_supported_file_types() const override {
        return {"jpg", "jpeg", "png", "bmp", "tga"};
    }
};

/**
 * Simple metadata plugin
 */
class SimpleMetadataPlugin : public IPlugin {
public:
    SimpleMetadataPlugin()
        : metadata_("SimpleMetadata", 
                   "Extracts basic file and image metadata",
                   "Picexplore Team",
                   PluginVersion{1, 0, 0},
                   PluginVersion{1, 0, 0}),
          context_(nullptr),
          active_(false),
          initialized_(false) {
    }
    
    virtual ~SimpleMetadataPlugin() {
        if (initialized_) {
            shutdown();
        }
    }
    
    const PluginMetadata& get_metadata() const override {
        return metadata_;
    }
    
    bool initialize(PluginContext* context) override {
        if (initialized_) {
            return true;
        }
        
        if (!context) {
            return false;
        }
        
        context_ = context;
        
        try {
            // Create and register the metadata extractor extension
            auto extractor = std::make_unique<SimpleMetadataExtractor>();
            ExtensionPointRegistry::register_extension_point(
                std::unique_ptr<IExtensionPoint>(extractor.release())
            );
            
            context_->log("INFO", "Simple metadata plugin initialized successfully");
            
            initialized_ = true;
            active_ = true;
            return true;
            
        } catch (const std::exception& e) {
            context_->log("ERROR", "Failed to initialize simple metadata plugin: " + std::string(e.what()));
            return false;
        }
    }
    
    void shutdown() override {
        if (!initialized_) {
            return;
        }
        
        if (context_) {
            context_->log("INFO", "Shutting down simple metadata plugin");
        }
        
        active_ = false;
        initialized_ = false;
        context_ = nullptr;
    }
    
    PluginVersion get_api_version() const override {
        return PluginVersion{1, 0, 0};
    }
    
    bool is_active() const override {
        return active_ && initialized_;
    }
    
    void set_active(bool active) override {
        if (initialized_) {
            active_ = active;
            if (context_) {
                context_->log("INFO", active ? "Simple metadata plugin activated" : "Simple metadata plugin deactivated");
            }
        }
    }

private:
    PluginMetadata metadata_;
    PluginContext* context_;
    bool active_;
    bool initialized_;
};

// Plugin registration
REGISTER_PLUGIN(SimpleMetadataPlugin)

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s