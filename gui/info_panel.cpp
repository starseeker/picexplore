#include "info_panel.h"
#include <FL/fl_draw.H>
#include <FL/Fl_Button.H>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <filesystem>
#include "../third_party/TinyEXIF.h"

// ── helpers ────────────────────────────────────────────────────────────────

static std::string format_size(uintmax_t bytes) {
    const char* suffixes[] = {"B", "KB", "MB", "GB"};
    int s = 0;
    double count = bytes;
    while (count >= 1024 && s < 3) { s++; count /= 1024; }
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << count << " " << suffixes[s];
    return ss.str();
}

static std::string format_time(uintmax_t t) {
    std::time_t time = static_cast<std::time_t>(t);
    char buf[100];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&time)))
        return buf;
    return "Unknown";
}

// ── construction ───────────────────────────────────────────────────────────

InfoPanel::InfoPanel(int X, int Y, int W, int H, const char* L)
    : Fl_Group(X, Y, W, H, L) {

    box(FL_FLAT_BOX);
    color(FL_DARK2);

    // Breadcrumb strip — initially height 0 (no image selected yet).
    // rebuild_breadcrumb() will size it correctly when an image is clicked.
    breadcrumb_bar_ = new Fl_Group(X, Y, W, 0);
    breadcrumb_bar_->box(FL_FLAT_BOX);
    breadcrumb_bar_->color(fl_darker(FL_DARK2));
    breadcrumb_bar_->end();

    // Text display fills the whole panel until an image is selected.
    text_buffer_  = new Fl_Text_Buffer();
    text_display_ = new Fl_Text_Display(X + 5, Y + 5, W - 10, H - 10);
    text_display_->buffer(text_buffer_);
    text_display_->box(FL_FLAT_BOX);
    text_display_->color(FL_DARK2);
    text_display_->textcolor(FL_FOREGROUND_COLOR);
    text_display_->textfont(FL_HELVETICA);
    text_display_->textsize(font_size_);
    text_display_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

    text_buffer_->text("No image selected.\nClick an image to view details.");

    end();
}

InfoPanel::~InfoPanel() {
    delete text_display_;
    delete text_buffer_;
}

// ── layout ─────────────────────────────────────────────────────────────────

void InfoPanel::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    // rebuild_breadcrumb() reflows the breadcrumb for the new width / height
    // and resizes both breadcrumb_bar_ and text_display_ itself.
    rebuild_breadcrumb();
}

// ── public API ─────────────────────────────────────────────────────────────

void InfoPanel::set_font_size(int size) {
    font_size_ = size;
    text_display_->textsize(size);
    // Reflow the breadcrumb — row height and label widths both depend on font size.
    rebuild_breadcrumb();
}

void InfoPanel::set_root_dir(const std::string& root) {
    root_dir_ = root;
    // Normalise: remove trailing slash
    while (root_dir_.size() > 1 && root_dir_.back() == '/')
        root_dir_.pop_back();
}

void InfoPanel::clear_info() {
    current_filepath_.clear();
    clear_breadcrumb();
    text_buffer_->text("No image selected.\nClick an image to view details.");
}

// ── breadcrumb ─────────────────────────────────────────────────────────────

// Per-button data bundle: stored as user_data so the non-capturing callback
// has access to both the InfoPanel* and the directory path without relying
// on a second user_data() write that would clobber the first.
struct CrumbData {
    InfoPanel*   panel;
    std::string  path;
};

// Free heap-allocated CrumbData* from every child that has one, then clear
// the group.  We collect the pointers first, then call clear() to destroy
// the widgets (which kills FLTK's tooltip reference), then delete the data —
// so the tooltip const char* is never dangling while a widget still exists.
void InfoPanel::clear_breadcrumb() {
    std::vector<CrumbData*> to_delete;
    for (int i = 0; i < breadcrumb_bar_->children(); ++i) {
        if (void* ud = breadcrumb_bar_->child(i)->user_data())
            to_delete.push_back(static_cast<CrumbData*>(ud));
    }
    breadcrumb_bar_->clear();   // destroys widgets — tooltip pointers now stale but unreachable
    breadcrumb_bar_->redraw();
    for (CrumbData* d : to_delete) delete d;  // safe to free now
}

void InfoPanel::rebuild_breadcrumb() {
    clear_breadcrumb();

    int panel_x = x(), panel_y = y(), panel_w = w(), panel_h = h();

    if (current_filepath_.empty() || root_dir_.empty()) {
        // No breadcrumb — text display fills the whole panel.
        crumb_h_ = 0;
        breadcrumb_bar_->resize(panel_x, panel_y, panel_w, 0);
        text_display_->resize(panel_x + 5, panel_y + 5, panel_w - 10, panel_h - 10);
        text_display_->redraw();
        return;
    }

    // ── build segment list ────────────────────────────────────────────────
    namespace fs = std::filesystem;

    fs::path root(root_dir_);
    fs::path file(current_filepath_);

    std::vector<std::pair<std::string, std::string>> segments; // {label, abs_path}
    {
        fs::path fp_parent = file.parent_path();
        std::vector<fs::path> chain;
        fs::path cur = fp_parent;
        while (true) {
            chain.push_back(cur);
            if (cur == root || cur == root.parent_path() || cur == cur.parent_path())
                break;
            cur = cur.parent_path();
        }
        std::reverse(chain.begin(), chain.end());

        size_t start = 0;
        for (size_t i = 0; i < chain.size(); ++i) {
            if (chain[i] == root) { start = i; break; }
        }
        for (size_t i = start; i < chain.size(); ++i)
            segments.emplace_back(chain[i].filename().string(), chain[i].string());
        segments.emplace_back(file.filename().string(), ""); // filename — not clickable
    }

    if (segments.empty()) {
        crumb_h_ = 0;
        breadcrumb_bar_->resize(panel_x, panel_y, panel_w, 0);
        text_display_->resize(panel_x + 5, panel_y + 5, panel_w - 10, panel_h - 10);
        text_display_->redraw();
        return;
    }

    // ── measure & wrap ────────────────────────────────────────────────────
    // ROW_H scales with font size so everything stays proportional.
    fl_font(FL_HELVETICA, font_size_);
    const int ROW_H  = font_size_ + 10; // button height per row
    const int SEP_W  = static_cast<int>(fl_width("/")) + 6;
    const int PAD    = 6;   // horizontal label padding inside each widget
    const int MARGIN = 6;   // left edge inset
    const int max_x  = panel_w - MARGIN; // right boundary (panel-relative)

    // The maximum width any single item may occupy (panel minus both margins).
    const int item_max_w = max_x - MARGIN;

    // Helper: truncate label with "\u2026" until it fits within max_pixels.
    auto truncate = [&](const std::string& s, int max_pixels) -> std::string {
        if (static_cast<int>(fl_width(s.c_str())) + PAD * 2 <= max_pixels)
            return s;
        const std::string ellipsis = "\u2026"; // UTF-8 HORIZONTAL ELLIPSIS
        int ew = static_cast<int>(fl_width(ellipsis.c_str()));
        std::string r = s;
        // Trim one character at a time; fast enough for typical directory names.
        while (!r.empty() &&
               static_cast<int>(fl_width(r.c_str())) + ew + PAD * 2 > max_pixels)
            r.pop_back();
        return r + ellipsis;
    };

    // Pre-compute widget widths and display labels for each segment.
    // display_label may be shorter than the segment name if it had to be truncated.
    struct SegmentLayout {
        int         item_w;
        int         sep_w;         // 0 for the last segment
        std::string display_label; // possibly truncated version of the segment name
    };
    std::vector<SegmentLayout> layouts;
    layouts.reserve(segments.size());
    for (size_t i = 0; i < segments.size(); ++i) {
        const std::string& name = segments[i].first;
        std::string dlabel = truncate(name, item_max_w);
        int iw = std::max(static_cast<int>(fl_width(dlabel.c_str())) + PAD * 2, 16);
        iw = std::min(iw, item_max_w); // never wider than the panel
        int sw = (i + 1 < segments.size()) ? SEP_W : 0;
        layouts.push_back({iw, sw, std::move(dlabel)});
    }

    // Single pass: determine (row, x_offset) for every item and separator.
    struct PlacedWidget {
        int row, x;   // row index and panel-relative x
        int w, h;     // size
        size_t seg_idx;
        bool is_sep;
    };
    std::vector<PlacedWidget> placed;
    placed.reserve(segments.size() * 2);

    int cur_x   = MARGIN;
    int cur_row = 0;

    for (size_t i = 0; i < segments.size(); ++i) {
        int iw = layouts[i].item_w;
        int sw = layouts[i].sep_w;

        // Wrap before item if it won't fit (but never wrap an empty row).
        if (cur_x + iw > max_x && cur_x > MARGIN) {
            cur_x = MARGIN;
            cur_row++;
        }
        placed.push_back({cur_row, cur_x, iw, ROW_H, i, false});
        cur_x += iw;

        if (sw > 0) {
            // Wrap before separator if it won't fit either.
            if (cur_x + sw > max_x && cur_x > MARGIN) {
                cur_x = MARGIN;
                cur_row++;
            }
            placed.push_back({cur_row, cur_x, sw, ROW_H, i, true});
            cur_x += sw;
        }
    }

    // Total bar height: rows * (ROW_H + row gap) + top/bottom margins.
    const int ROW_GAP = 4;
    crumb_h_ = (cur_row + 1) * (ROW_H + ROW_GAP) + ROW_GAP * 2;

    // ── create widgets ───────────────────────────────────────────────────
    breadcrumb_bar_->resize(panel_x, panel_y, panel_w, crumb_h_);

    breadcrumb_bar_->begin();
    for (const auto& pw : placed) {
        int bx = panel_x + pw.x;
        int by = panel_y + ROW_GAP + pw.row * (ROW_H + ROW_GAP);

        if (pw.is_sep) {
            Fl_Box* sep = new Fl_Box(bx, by, pw.w, pw.h, "/");
            sep->box(FL_NO_BOX);
            sep->labelcolor(fl_rgb_color(140, 140, 140));
            sep->labelsize(font_size_);
        } else {
            const auto& [label, abs_path] = segments[pw.seg_idx];
            // Use the pre-computed (possibly truncated) display label.
            const std::string& dlabel = layouts[pw.seg_idx].display_label;
            bool is_dir = !abs_path.empty();
            if (is_dir) {
                auto* d = new CrumbData{this, abs_path};
                Fl_Button* btn = new Fl_Button(bx, by, pw.w, pw.h);
                btn->copy_label(dlabel.c_str());
                btn->box(FL_FLAT_BOX);
                btn->color(fl_darker(FL_DARK2));
                btn->labelcolor(fl_rgb_color(100, 180, 255));
                btn->labelsize(font_size_);
                btn->labelfont(FL_HELVETICA);
                // Tooltip shows the full path so truncated labels remain accessible.
                btn->tooltip(d->path.c_str());
                btn->callback([](Fl_Widget* w, void* ud) {
                    auto* d = static_cast<CrumbData*>(ud);
                    if (d->panel->on_dir_clicked) d->panel->on_dir_clicked(d->path);
                }, d);
            } else {
                Fl_Box* box = new Fl_Box(bx, by, pw.w, pw.h);
                box->copy_label(dlabel.c_str());
                box->box(FL_NO_BOX);
                box->labelcolor(fl_rgb_color(200, 200, 200));
                box->labelsize(font_size_);
                box->labelfont(FL_HELVETICA);
            }
        }
    }
    breadcrumb_bar_->end();

    // Resize text display to occupy whatever height remains below the breadcrumb.
    int td_y = panel_y + crumb_h_;
    int td_h = panel_h - crumb_h_;
    text_display_->resize(panel_x + 5, td_y + 5, panel_w - 10, std::max(td_h - 10, 1));

    breadcrumb_bar_->redraw();
    text_display_->redraw();
}

// ── display_info ───────────────────────────────────────────────────────────

void InfoPanel::display_info(const ImageEntry& entry) {
    current_filepath_ = entry.filepath;
    rebuild_breadcrumb();

    std::stringstream ss;

    // Full path is shown here as selectable text; the breadcrumb above shows
    // the same path as clickable navigation segments (possibly truncated).
    std::string filename = std::filesystem::path(entry.filepath).filename().string();
    ss << "=== File Information ===\n";
    ss << "Path: " << entry.filepath << "\n";
    ss << "Name: " << filename << "\n";


    if (entry.metadata_known) {
        ss << "Dimensions: " << entry.original_width << " x " << entry.original_height << "\n";
        ss << "Size: " << format_size(entry.file_size) << "\n";
        ss << "Modified: " << format_time(entry.file_timestamp) << "\n";
    } else {
        ss << "Metadata: Not yet loaded\n";
    }
    ss << "\n";

    // EXIF
    std::ifstream file(entry.filepath, std::ios::binary);
    if (file.is_open()) {
        TinyEXIF::EXIFInfo exif;
        if (exif.parseFrom(file) == TinyEXIF::PARSE_SUCCESS) {
            bool has_exif = false;

            if (!exif.Make.empty() || !exif.Model.empty()) {
                ss << "=== Camera Info ===\n";
                if (!exif.Make.empty())  ss << "Make: "  << exif.Make  << "\n";
                if (!exif.Model.empty()) ss << "Model: " << exif.Model << "\n";
                has_exif = true;
            }

            if (exif.FocalLength > 0 || exif.ExposureTime > 0 ||
                exif.FNumber > 0 || exif.ISOSpeedRatings > 0) {
                ss << "\n=== Exposure ===\n";
                if (exif.FocalLength > 0)
                    ss << "Focal Length: " << exif.FocalLength << "mm\n";
                if (exif.ExposureTime > 0)
                    ss << "Exposure: 1/" << static_cast<int>(1.0 / exif.ExposureTime + 0.5) << "s\n";
                if (exif.FNumber > 0)
                    ss << "Aperture: f/" << exif.FNumber << "\n";
                if (exif.ISOSpeedRatings > 0)
                    ss << "ISO: " << exif.ISOSpeedRatings << "\n";
                has_exif = true;
            }

            if (!exif.DateTimeOriginal.empty()) {
                ss << "\n=== Date/Time ===\n";
                ss << "Original: " << exif.DateTimeOriginal << "\n";
                has_exif = true;
            }

            if (exif.GeoLocation.hasLatLon()) {
                ss << "\n=== Location ===\n";
                ss << "Lat: " << exif.GeoLocation.Latitude  << "\n";
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
