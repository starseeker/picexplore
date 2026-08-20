#pragma once

#include <FL/Enumerations.H>
#include <string>
#include <vector>
#include <cstdint>

namespace FileTypeColors {

struct ColorRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct LegendEntry {
    std::string name;
    ColorRGB color;
};

ColorRGB get_color_rgb(const std::string& filepath);
Fl_Color get_fl_color(const std::string& filepath);
std::string get_category_name(const std::string& filepath);
std::vector<LegendEntry> get_legend_entries();

} // namespace FileTypeColors
