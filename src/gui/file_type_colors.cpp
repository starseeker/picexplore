#include "file_type_colors.h"
#include <string_view>
#include <algorithm>
#include <cctype>
#include <FL/fl_draw.H>

namespace FileTypeColors {

static std::string_view extract_ext(const std::string& filepath) {
    if (filepath.empty()) return std::string_view();
    const char* data = filepath.data();
    size_t len = filepath.size();
    for (size_t i = len; i > 0; --i) {
        char c = data[i - 1];
        if (c == '.') {
            return std::string_view(data + i - 1, len - (i - 1));
        }
        if (c == '/' || c == '\\') {
            break;
        }
    }
    return std::string_view();
}

ColorRGB get_color_rgb(const std::string& filepath) {
    std::string_view ext_view = extract_ext(filepath);
    if (ext_view.empty()) {
        return {116, 125, 140};
    }

    char buf[16];
    size_t len = std::min(ext_view.size(), sizeof(buf) - 1);
    for (size_t i = 0; i < len; ++i) {
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(ext_view[i])));
    }
    buf[len] = '\0';
    std::string_view ext(buf, len);

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
    std::string_view ext_view = extract_ext(filepath);
    if (ext_view.empty()) {
        return "OTHER";
    }

    if (ext_view[0] == '.') {
        ext_view.remove_prefix(1);
    }

    std::string result;
    result.reserve(ext_view.size());
    for (char ch : ext_view) {
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
    }
    return result.empty() ? "OTHER" : result;
}

std::vector<LegendEntry> get_legend_entries() {
    return {
        {"JPG", {230, 126, 34}},
        {"PNG", {41, 128, 185}},
        {"WEBP", {39, 174, 96}},
        {"GIF", {142, 68, 173}},
        {"TIFF", {74, 105, 189}},
        {"BMP", {87, 96, 111}},
        {"PDF", {231, 76, 60}},
        {"SVG", {22, 160, 133}},
        {"OTHER", {116, 125, 140}}
    };
}

} // namespace FileTypeColors
