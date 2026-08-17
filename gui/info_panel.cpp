#include "info_panel.h"
#include <FL/fl_draw.H>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <filesystem>
#include "../third_party/TinyEXIF.h"

InfoPanel::InfoPanel(int X, int Y, int W, int H, const char* L)
    : Fl_Group(X, Y, W, H, L) {
    
    box(FL_FLAT_BOX);
    color(FL_DARK2);

    text_buffer_ = new Fl_Text_Buffer();
    text_display_ = new Fl_Text_Display(X + 5, Y + 5, W - 10, H - 10);
    text_display_->buffer(text_buffer_);
    text_display_->box(FL_FLAT_BOX);
    text_display_->color(FL_DARK2);
    text_display_->textcolor(FL_FOREGROUND_COLOR);
    text_display_->textfont(FL_HELVETICA);
    text_display_->textsize(12);
    text_display_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

    // Initial message
    text_buffer_->text("No image selected.\nClick an image to view details.");

    end();
}

InfoPanel::~InfoPanel() {
    delete text_display_;
    delete text_buffer_;
}

void InfoPanel::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    text_display_->resize(X + 5, Y + 5, W - 10, H - 10);
}

static std::string format_size(uintmax_t bytes) {
    const char* suffixes[] = {"B", "KB", "MB", "GB"};
    int s = 0;
    double count = bytes;
    while (count >= 1024 && s < 3) {
        s++;
        count /= 1024;
    }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << count << " " << suffixes[s];
    return ss.str();
}

static std::string format_time(uintmax_t t) {
    std::time_t time = static_cast<std::time_t>(t);
    char buf[100];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&time))) {
        return buf;
    }
    return "Unknown";
}

void InfoPanel::clear_info() {
    text_buffer_->text("No image selected.\nClick an image to view details.");
}

void InfoPanel::display_info(const ImageEntry& entry) {
    std::stringstream ss;

    ss << "=== File Information ===\n";
    ss << "Path: " << entry.filepath << "\n";
    
    std::string filename = std::filesystem::path(entry.filepath).filename().string();
    ss << "Name: " << filename << "\n";

    if (entry.metadata_known) {
        ss << "Dimensions: " << entry.original_width << " x " << entry.original_height << "\n";
        ss << "Size: " << format_size(entry.file_size) << "\n";
        ss << "Modified: " << format_time(entry.file_timestamp) << "\n";
    } else {
        ss << "Metadata: Not yet loaded\n";
    }
    ss << "\n";

    // Attempt to parse EXIF data
    std::ifstream file(entry.filepath, std::ios::binary);
    if (file.is_open()) {
        TinyEXIF::EXIFInfo exif;
        if (exif.parseFrom(file) == TinyEXIF::PARSE_SUCCESS) {
            bool has_exif = false;
            
            if (!exif.Make.empty() || !exif.Model.empty()) {
                ss << "=== Camera Info ===\n";
                if (!exif.Make.empty()) ss << "Make: " << exif.Make << "\n";
                if (!exif.Model.empty()) ss << "Model: " << exif.Model << "\n";
                has_exif = true;
            }

            if (exif.FocalLength > 0 || exif.ExposureTime > 0 || exif.FNumber > 0 || exif.ISOSpeedRatings > 0) {
                ss << "\n=== Exposure ===\n";
                if (exif.FocalLength > 0) ss << "Focal Length: " << exif.FocalLength << "mm\n";
                if (exif.ExposureTime > 0) ss << "Exposure: 1/" << static_cast<int>(1.0 / exif.ExposureTime + 0.5) << "s\n";
                if (exif.FNumber > 0) ss << "Aperture: f/" << exif.FNumber << "\n";
                if (exif.ISOSpeedRatings > 0) ss << "ISO: " << exif.ISOSpeedRatings << "\n";
                has_exif = true;
            }

            if (!exif.DateTimeOriginal.empty()) {
                ss << "\n=== Date/Time ===\n";
                ss << "Original: " << exif.DateTimeOriginal << "\n";
                has_exif = true;
            }

            if (exif.GeoLocation.hasLatLon()) {
                ss << "\n=== Location ===\n";
                ss << "Lat: " << exif.GeoLocation.Latitude << "\n";
                ss << "Lon: " << exif.GeoLocation.Longitude << "\n";
                has_exif = true;
            }

            if (!has_exif) {
                ss << "No detailed EXIF metadata found.\n";
            }
        }
    }

    text_buffer_->text(ss.str().c_str());
}
