#pragma once

#include <FL/Enumerations.H>
#include <string>
#include <cstdint>

namespace FileTypeColors {

struct ColorRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

ColorRGB get_color_rgb(const std::string& filepath);
Fl_Color get_fl_color(const std::string& filepath);
std::string get_category_name(const std::string& filepath);

} // namespace FileTypeColors
