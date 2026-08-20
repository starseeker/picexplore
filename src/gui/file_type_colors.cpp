#include "file_type_colors.h"
#include <filesystem>
#include <algorithm>
#include <FL/fl_draw.H>

namespace FileTypeColors {

ColorRGB get_color_rgb(const std::string& filepath) {
    namespace fs = std::filesystem;
    std::string ext;
    try {
        ext = fs::path(filepath).extension().string();
    } catch (...) {
        ext.clear();
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".jpg" || ext == ".jpeg") {
        return {230, 126, 34};  // Warm Amber / Orange
    } else if (ext == ".png") {
        return {41, 128, 185};   // Bright Azure Blue
    } else if (ext == ".webp") {
        return {39, 174, 96};    // Emerald Green
    } else if (ext == ".gif") {
        return {142, 68, 173};   // Orchid Purple
    } else if (ext == ".tif" || ext == ".tiff") {
        return {74, 105, 189};   // Royal Indigo / Slate
    } else if (ext == ".bmp") {
        return {87, 96, 111};    // Steel Blue
    } else if (ext == ".tga") {
        return {26, 188, 156};   // Turquoise
    } else if (ext == ".pdf") {
        return {231, 76, 60};    // Crimson Red
    } else if (ext == ".svg") {
        return {22, 160, 133};   // Deep Teal
    }

    return {116, 125, 140};      // Default Slate Gray
}

Fl_Color get_fl_color(const std::string& filepath) {
    ColorRGB c = get_color_rgb(filepath);
    return fl_rgb_color(c.r, c.g, c.b);
}

std::string get_category_name(const std::string& filepath) {
    namespace fs = std::filesystem;
    std::string ext;
    try {
        ext = fs::path(filepath).extension().string();
    } catch (...) {
        ext.clear();
    }
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (!ext.empty() && ext[0] == '.') {
        ext = ext.substr(1);
    }
    for (auto& ch : ext) {
        ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
    }
    return ext.empty() ? "OTHER" : ext;
}

} // namespace FileTypeColors
