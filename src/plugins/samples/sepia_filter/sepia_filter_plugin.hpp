/*
 * sepia_filter_plugin.hpp - Sample sepia filter plugin for picexplore
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

#include "../../../plugin_interface.hpp"
#include "../../../extension_points.hpp"
#include <memory>

/**
 * Sepia tone image filter implementation
 */
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

private:
    void apply_sepia_tone(uint8_t* data, int width, int height, int channels, float intensity);
};

/**
 * Sample sepia filter plugin
 */
class SepiaFilterPlugin : public IPlugin {
public:
    SepiaFilterPlugin();
    virtual ~SepiaFilterPlugin();
    
    // IPlugin implementation
    const PluginMetadata& get_metadata() const override;
    bool initialize(PluginContext* context) override;
    void shutdown() override;
    PluginVersion get_api_version() const override;
    bool is_active() const override;
    void set_active(bool active) override;

private:
    PluginMetadata metadata_;
    PluginContext* context_;
    std::unique_ptr<SepiaImageProcessor> processor_;
    bool active_;
    bool initialized_;
};

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s