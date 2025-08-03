/*
 * plugin_manager_test.cpp - Unit tests for plugin management system
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

#include <iostream>
#include <cassert>
#include <memory>
#include <filesystem>
#include "../src/plugin_interface.hpp"
#include "../src/extension_points.hpp"
#include "../src/event_bus.hpp"

namespace fs = std::filesystem;

void test_extension_point_registry() {
    std::cout << "Testing extension point registry..." << std::endl;
    
    // Clear registry first
    ExtensionPointRegistry::clear_all();
    
    // Test that registry starts empty
    auto extensions = ExtensionPointRegistry::get_extension_points(ExtensionPointType::IMAGE_PROCESSOR);
    assert(extensions.empty());
    
    // Test hook registration
    bool hook_called = false;
    ExtensionPointRegistry::register_hook(ExtensionPointType::IMAGE_PROCESSOR, 
        [&hook_called](IExtensionPoint* ext) {
            hook_called = true;
        });
    
    // Hooks should not be called for empty registry
    assert(!hook_called);
    
    std::cout << "✓ Extension point registry test passed" << std::endl;
}

void test_plugin_version_compatibility() {
    std::cout << "Testing plugin version compatibility..." << std::endl;
    
    PluginVersion v1_0_0(1, 0, 0);
    PluginVersion v1_1_0(1, 1, 0);
    PluginVersion v1_0_1(1, 0, 1);
    PluginVersion v2_0_0(2, 0, 0);
    
    // Same major, same or lower minor should be compatible
    assert(v1_0_0.is_compatible_with(v1_0_0));
    assert(v1_0_0.is_compatible_with(v1_1_0));
    assert(v1_0_1.is_compatible_with(v1_1_0));
    
    // Different major versions should not be compatible
    assert(!v2_0_0.is_compatible_with(v1_0_0));
    assert(!v1_0_0.is_compatible_with(v2_0_0));
    
    // Higher minor version plugin should not be compatible with lower API
    assert(!v1_1_0.is_compatible_with(v1_0_0));
    
    std::cout << "✓ Plugin version compatibility test passed" << std::endl;
}

void test_plugin_metadata() {
    std::cout << "Testing plugin metadata..." << std::endl;
    
    PluginMetadata metadata("TestPlugin", "A test plugin", "Test Author", 
                           PluginVersion(1, 2, 3), PluginVersion(1, 0, 0));
    
    assert(metadata.name == "TestPlugin");
    assert(metadata.description == "A test plugin");
    assert(metadata.author == "Test Author");
    assert(metadata.version.major == 1);
    assert(metadata.version.minor == 2);
    assert(metadata.version.patch == 3);
    
    std::cout << "✓ Plugin metadata test passed" << std::endl;
}

void test_plugin_config() {
    std::cout << "Testing plugin configuration..." << std::endl;
    
    PluginConfig config;
    config.allow_file_access = true;
    config.allow_network_access = false;
    config.blocked_capabilities.push_back("dangerous_operation");
    
    assert(config.allow_file_access);
    assert(!config.allow_network_access);
    assert(config.blocked_capabilities.size() == 1);
    assert(config.blocked_capabilities[0] == "dangerous_operation");
    
    std::cout << "✓ Plugin configuration test passed" << std::endl;
}

int main() {
    try {
        std::cout << "Running plugin system tests..." << std::endl;
        
        test_extension_point_registry();
        test_plugin_version_compatibility();
        test_plugin_metadata();
        test_plugin_config();
        
        std::cout << std::endl << "All plugin system tests passed! ✓" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s