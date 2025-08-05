/*
 * sepia_filter_plugin.cpp - Sample sepia filter plugin implementation
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

#include "sepia_filter_plugin.hpp"
#include <algorithm>
#include <cstring>
#include <cmath>

// SepiaImageProcessor implementation

std::unique_ptr<uint8_t[]> SepiaImageProcessor::process_image(
    const uint8_t* image_data,
    int width,
    int height,
    int channels,
    const std::unordered_map<std::string, std::string>& parameters) {
    
    if (!image_data || width <= 0 || height <= 0 || channels < 3) {
        return nullptr;
    }
    
    // Get intensity parameter
    float intensity = 1.0f;
    auto it = parameters.find("intensity");
    if (it != parameters.end()) {
        try {
            intensity = std::stof(it->second);
            intensity = std::clamp(intensity, 0.0f, 2.0f);
        } catch (...) {
            intensity = 1.0f;
        }
    }
    
    // Allocate output buffer
    size_t data_size = width * height * channels;
    auto output = std::make_unique<uint8_t[]>(data_size);
    
    // Copy input data
    std::memcpy(output.get(), image_data, data_size);
    
    // Apply sepia filter
    apply_sepia_tone(output.get(), width, height, channels, intensity);
    
    return output;
}

void SepiaImageProcessor::apply_sepia_tone(uint8_t* data, int width, int height, int channels, float intensity) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int index = (y * width + x) * channels;
            
            uint8_t r = data[index];
            uint8_t g = data[index + 1];
            uint8_t b = data[index + 2];
            
            // Sepia tone transformation matrix
            float new_r = (r * 0.393f + g * 0.769f + b * 0.189f) * intensity + r * (1.0f - intensity);
            float new_g = (r * 0.349f + g * 0.686f + b * 0.168f) * intensity + g * (1.0f - intensity);
            float new_b = (r * 0.272f + g * 0.534f + b * 0.131f) * intensity + b * (1.0f - intensity);
            
            // Clamp values to 0-255
            data[index] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, new_r)));
            data[index + 1] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, new_g)));
            data[index + 2] = static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, new_b)));
            
            // Keep alpha channel unchanged if present
            // data[index + 3] remains as is for RGBA
        }
    }
}

// SepiaFilterPlugin implementation

SepiaFilterPlugin::SepiaFilterPlugin()
    : metadata_("SepiaFilter", 
               "Applies sepia tone effect to images",
               "Picexplore Team",
               PluginVersion{1, 0, 0},
               PluginVersion{1, 0, 0}),
      context_(nullptr),
      active_(false),
      initialized_(false) {
}

SepiaFilterPlugin::~SepiaFilterPlugin() {
    if (initialized_) {
        shutdown();
    }
}

const PluginMetadata& SepiaFilterPlugin::get_metadata() const {
    return metadata_;
}

bool SepiaFilterPlugin::initialize(PluginContext* context) {
    if (initialized_) {
        return true;
    }
    
    if (!context) {
        return false;
    }
    
    context_ = context;
    
    try {
        // Create and register the image processor extension
        processor_ = std::make_unique<SepiaImageProcessor>();
        ExtensionPointRegistry::register_extension_point(
            std::unique_ptr<IExtensionPoint>(processor_.release())
        );
        
        context_->log("INFO", "Sepia filter plugin initialized successfully");
        
        initialized_ = true;
        active_ = true;
        return true;
        
    } catch (const std::exception& e) {
        context_->log("ERROR", "Failed to initialize sepia filter plugin: " + std::string(e.what()));
        return false;
    }
}

void SepiaFilterPlugin::shutdown() {
    if (!initialized_) {
        return;
    }
    
    if (context_) {
        context_->log("INFO", "Shutting down sepia filter plugin");
    }
    
    // Extension point registry will handle cleanup of the processor
    processor_.reset();
    active_ = false;
    initialized_ = false;
    context_ = nullptr;
}

PluginVersion SepiaFilterPlugin::get_api_version() const {
    return PluginVersion{1, 0, 0};
}

bool SepiaFilterPlugin::is_active() const {
    return active_ && initialized_;
}

void SepiaFilterPlugin::set_active(bool active) {
    if (initialized_) {
        active_ = active;
        if (context_) {
            context_->log("INFO", active ? "Sepia filter plugin activated" : "Sepia filter plugin deactivated");
        }
    }
}

// Plugin registration
REGISTER_PLUGIN(SepiaFilterPlugin)

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s