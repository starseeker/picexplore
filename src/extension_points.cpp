/*
 * extension_points.cpp - Extension point registry implementation
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

#include "extension_points.hpp"
#include <algorithm>
#include <mutex>

// Static member definitions
std::unordered_map<ExtensionPointType, std::vector<std::unique_ptr<IExtensionPoint>>> 
    ExtensionPointRegistry::extensions_;

std::unordered_map<std::string, IExtensionPoint*> 
    ExtensionPointRegistry::extensions_by_name_;

std::unordered_map<ExtensionPointType, std::vector<ExtensionHook>> 
    ExtensionPointRegistry::hooks_;

// Thread safety mutex
static std::mutex registry_mutex;

void ExtensionPointRegistry::register_extension_point(std::unique_ptr<IExtensionPoint> extension) {
    if (!extension) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    ExtensionPointType type = extension->get_type();
    std::string name = extension->get_name();
    
    // Store by type
    extensions_[type].push_back(std::move(extension));
    
    // Store by name for quick lookup
    IExtensionPoint* ext_ptr = extensions_[type].back().get();
    extensions_by_name_[name] = ext_ptr;
    
    // Notify registered hooks
    if (hooks_.find(type) != hooks_.end()) {
        for (const auto& hook : hooks_[type]) {
            hook(ext_ptr);
        }
    }
}

std::vector<IExtensionPoint*> ExtensionPointRegistry::get_extension_points(ExtensionPointType type) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    std::vector<IExtensionPoint*> result;
    if (extensions_.find(type) != extensions_.end()) {
        for (const auto& ext : extensions_[type]) {
            result.push_back(ext.get());
        }
    }
    return result;
}

IExtensionPoint* ExtensionPointRegistry::get_extension_point(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    auto it = extensions_by_name_.find(name);
    return (it != extensions_by_name_.end()) ? it->second : nullptr;
}

void ExtensionPointRegistry::register_hook(ExtensionPointType type, ExtensionHook hook) {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    hooks_[type].push_back(hook);
    
    // Call hook immediately for any existing extensions of this type
    if (extensions_.find(type) != extensions_.end()) {
        for (const auto& ext : extensions_[type]) {
            hook(ext.get());
        }
    }
}

void ExtensionPointRegistry::clear_all() {
    std::lock_guard<std::mutex> lock(registry_mutex);
    
    extensions_.clear();
    extensions_by_name_.clear();
    hooks_.clear();
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s