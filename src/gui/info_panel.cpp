#include "info_panel.h"
#include <FL/fl_draw.H>
#include <FL/Fl_Button.H>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <cstring>
#include <zlib.h>
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

// Helper to read big-endian 32-bit integers for PNG parsing
static uint32_t read_be32(std::ifstream& file) {
    uint8_t bytes[4];
    if (file.read(reinterpret_cast<char*>(bytes), 4)) {
        return (bytes[0] << 24) | (bytes[1] << 16) | (bytes[2] << 8) | bytes[3];
    }
    return 0;
}

// Helper to decode a hex profile string embedded in tEXt/zTXt chunks
static std::vector<uint8_t> decode_hex_profile(const std::string& text) {
    std::string hex_str;
    for (char c : text) {
        if (std::isxdigit(c)) {
            hex_str.push_back(c);
        }
    }
    
    std::vector<uint8_t> bytes;
    bytes.reserve(hex_str.length() / 2);
    for (size_t i = 0; i + 1 < hex_str.length(); i += 2) {
        std::string byteString = hex_str.substr(i, 2);
        uint8_t byte = (uint8_t) strtol(byteString.c_str(), nullptr, 16);
        bytes.push_back(byte);
    }
    
    std::vector<uint8_t> payload;
    payload.reserve(bytes.size() + 6);
    const char header[6] = {'E','x','i','f','\0','\0'};
    payload.insert(payload.end(), header, header + 6);
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    
    return payload;
}

static std::vector<uint8_t> extract_png_exif(std::ifstream& file) {
    // Read signature
    uint8_t sig[8];
    if (!file.read(reinterpret_cast<char*>(sig), 8)) return {};
    
    // Check if it's a valid PNG
    const uint8_t png_sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (std::memcmp(sig, png_sig, 8) != 0) return {};
    
    while (file.good()) {
        uint32_t length = read_be32(file);
        if (!file.good()) break;
        
        char type[5] = {0};
        if (!file.read(type, 4)) break;
        
        if (std::string(type) == "eXIf") {
            // PNG eXIf chunk does not contain the Exif\0\0 header. 
            // We prepend it so TinyEXIF can parse it seamlessly.
            std::vector<uint8_t> payload(length + 6);
            std::memcpy(payload.data(), "Exif\0\0", 6);
            if (length > 0 && file.read(reinterpret_cast<char*>(payload.data() + 6), length)) {
                return payload;
            }
            break;
        } else if (std::string(type) == "tEXt" || std::string(type) == "zTXt") {
            std::vector<uint8_t> chunk_data(length);
            if (length > 0 && file.read(reinterpret_cast<char*>(chunk_data.data()), length)) {
                size_t null_pos = 0;
                while (null_pos < length && chunk_data[null_pos] != '\0') {
                    null_pos++;
                }
                
                if (null_pos < length) {
                    std::string keyword(reinterpret_cast<char*>(chunk_data.data()), null_pos);
                    if (keyword == "Raw profile type exif" || keyword == "Raw profile type APP1") {
                        if (std::string(type) == "tEXt") {
                            std::string text(reinterpret_cast<char*>(chunk_data.data() + null_pos + 1), length - null_pos - 1);
                            return decode_hex_profile(text);
                        } else if (std::string(type) == "zTXt") {
                            if (null_pos + 2 < length) { // +1 for null, +1 for compression method
                                size_t comp_len = length - null_pos - 2;
                                uLongf dest_len = comp_len * 4 + 1024;
                                std::vector<uint8_t> uncomp(dest_len);
                                while (true) {
                                    int res = uncompress(uncomp.data(), &dest_len, chunk_data.data() + null_pos + 2, comp_len);
                                    if (res == Z_OK) {
                                        std::string text(reinterpret_cast<char*>(uncomp.data()), dest_len);
                                        return decode_hex_profile(text);
                                    } else if (res == Z_BUF_ERROR) {
                                        dest_len *= 2;
                                        uncomp.resize(dest_len);
                                    } else {
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else if (std::string(type) == "IEND") {
            break;
        } else {
            // Skip chunk data and CRC (4 bytes) if not already read
            file.seekg(length + 4, std::ios::cur);
            continue;
        }
        
        // Skip CRC for read chunks
        file.seekg(4, std::ios::cur);
    }
    return {};
}

// Container for duplicate header + duplicate browser inside Fl_Tile
class DupTileGroup : public Fl_Group {
public:
    DupTileGroup(int X, int Y, int W, int H, const char* L = 0)
        : Fl_Group(X, Y, W, H, L) {}

    void set_children(Fl_Box* header, Fl_Hold_Browser* browser) {
        header_ = header;
        browser_ = browser;
    }

    void set_header_height(int hh) {
        header_h_ = hh;
        layout_children();
    }

    void resize(int X, int Y, int W, int H) override {
        Fl_Group::resize(X, Y, W, H);
        layout_children();
        if (on_resized) on_resized(H);
    }

    void layout_children() {
        if (header_ && browser_) {
            header_->resize(x() + 5, y() + 2, w() - 10, header_h_);
            int bh = std::max(h() - header_h_ - 6, 10);
            browser_->resize(x() + 5, y() + header_h_ + 2, w() - 10, bh);
        }
    }

    void draw() override {
        Fl_Group::draw();
        // Draw a subtle horizontal splitter divider line at the top seam
        fl_color(fl_lighter(FL_DARK2));
        fl_line(x() + 5, y(), x() + w() - 5, y());
    }

    std::function<void(int)> on_resized;

private:
    int header_h_ = 22;
    Fl_Box* header_ = nullptr;
    Fl_Hold_Browser* browser_ = nullptr;
};

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

    // Tile group allows user-adjustable vertical split between text info and duplicate list
    tile_group_ = new Fl_Tile(X, Y, W, H - ACTION_BTN_H - 10);
    tile_group_->box(FL_FLAT_BOX);
    tile_group_->color(FL_DARK2);

    // Text display occupies the top pane of tile_group_
    text_buffer_  = new Fl_Text_Buffer();
    text_display_ = new Fl_Text_Display(X + 5, Y + 5, W - 10, H - ACTION_BTN_H - 20);
    text_display_->buffer(text_buffer_);
    text_display_->box(FL_FLAT_BOX);
    text_display_->color(FL_DARK2);
    text_display_->textcolor(FL_FOREGROUND_COLOR);
    text_display_->textfont(FL_HELVETICA);
    text_display_->textsize(font_size_);
    text_display_->wrap_mode(Fl_Text_Display::WRAP_AT_BOUNDS, 0);

    text_buffer_->text("No image selected.\nClick an image to view details.");

    // DupTileGroup occupies the bottom pane of tile_group_
    dup_group_ = new DupTileGroup(X, Y + H / 2, W, H / 2);
    dup_group_->box(FL_FLAT_BOX);
    dup_group_->color(FL_DARK2);

    dup_header_ = new Fl_Box(X + 5, Y + H / 2 + 2, W - 10, font_size_ + 10, "Duplicate Copies:");
    dup_header_->box(FL_FLAT_BOX);
    dup_header_->color(FL_DARK2);
    dup_header_->labelcolor(fl_rgb_color(220, 220, 220));
    dup_header_->labelsize(font_size_);
    dup_header_->labelfont(FL_HELVETICA_BOLD);
    dup_header_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

    dup_browser_ = new Fl_Hold_Browser(X + 5, Y + H / 2 + font_size_ + 12, W - 10, 100);
    dup_browser_->box(FL_FLAT_BOX);
    dup_browser_->color(fl_darker(FL_DARK2));
    dup_browser_->textcolor(FL_FOREGROUND_COLOR);
    dup_browser_->textfont(FL_HELVETICA);
    dup_browser_->textsize(font_size_);
    dup_browser_->selection_color(fl_rgb_color(60, 160, 255));
    dup_browser_->scrollbar.box(FL_FLAT_BOX);
    dup_browser_->scrollbar.color(fl_rgb_color(32, 32, 32));
    dup_browser_->scrollbar.selection_color(fl_rgb_color(70, 70, 70));
    dup_browser_->scrollbar.labelcolor(fl_rgb_color(210, 210, 210));
    dup_browser_->callback([](Fl_Widget* w, void* ud) {
        auto* panel = static_cast<InfoPanel*>(ud);
        int val = panel->dup_browser_->value();
        if (val > 0 && val <= static_cast<int>(panel->current_duplicates_.size())) {
            const std::string& path = panel->current_duplicates_[val - 1];
            if (Fl::event_clicks() > 0 && panel->on_duplicate_double_clicked) {
                panel->on_duplicate_double_clicked(path);
            } else if (panel->on_duplicate_clicked) {
                panel->on_duplicate_clicked(path);
            }
        }
    }, this);

    dup_group_->set_children(dup_header_, dup_browser_);
    dup_group_->on_resized = [this](int h) {
        if (!current_duplicates_.empty() && dup_group_ && dup_group_->visible()) {
            user_dup_height_ = h;
        }
    };
    dup_group_->end();
    dup_group_->hide();

    tile_group_->end();

    action_btn_ = new Fl_Button(X + 5, Y + H - ACTION_BTN_H - 5, W - 10, ACTION_BTN_H, "Scroll to Image");
    action_btn_->hide();
    action_btn_->callback([](Fl_Widget* w, void* ud) {
        auto* panel = static_cast<InfoPanel*>(ud);
        if (panel->is_single_image_mode_) {
            if (panel->on_exit_image_view) panel->on_exit_image_view();
        } else {
            if (panel->on_scroll_to_image) panel->on_scroll_to_image(panel->current_filepath_);
        }
    }, this);

    end();
}

InfoPanel::~InfoPanel() {
    clear_breadcrumb();
    if (text_display_) {
        text_display_->buffer(nullptr);
    }
    delete text_buffer_;
    text_buffer_ = nullptr;
}

// ── layout ─────────────────────────────────────────────────────────────────

void InfoPanel::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    // rebuild_breadcrumb() reflows the breadcrumb for the new width / height
    // and resizes both breadcrumb_bar_ and text_display_ itself.
    if (visible()) {
        rebuild_breadcrumb();
    }
}

void InfoPanel::show() {
    Fl_Group::show();
    if (breadcrumb_bar_) breadcrumb_bar_->show();
    if (tile_group_) tile_group_->show();
    if (text_display_) text_display_->show();
    rebuild_breadcrumb();
    update_action_button();
}

void InfoPanel::hide() {
    Fl_Group::hide();
    if (breadcrumb_bar_) breadcrumb_bar_->hide();
    if (tile_group_) tile_group_->hide();
    if (text_display_) text_display_->hide();
    if (dup_group_) dup_group_->hide();
    if (action_btn_) action_btn_->hide();
}

// ── public API ─────────────────────────────────────────────────────────────

void InfoPanel::set_font_size(int size) {
    font_size_ = size;
    text_display_->textsize(size);
    if (dup_header_) dup_header_->labelsize(size);
    if (dup_browser_) dup_browser_->textsize(size);
    if (dup_group_) dup_group_->set_header_height(size + 10);
    // Reflow the breadcrumb — row height and label widths both depend on font size.
    if (visible()) {
        rebuild_breadcrumb();
    }
}

void InfoPanel::set_root_dir(const std::string& root) {
    root_dir_ = root;
    // Normalise: remove trailing slash
    while (root_dir_.size() > 1 && root_dir_.back() == '/')
        root_dir_.pop_back();
}

void InfoPanel::set_single_image_mode(bool single_image) {
    is_single_image_mode_ = single_image;
    if (visible()) {
        update_action_button();
    }
}

void InfoPanel::update_action_button() {
    if (!visible()) {
        if (action_btn_) action_btn_->hide();
        return;
    }

    int panel_x = x(), panel_y = y(), panel_w = w(), panel_h = h();
    if (is_single_image_mode_) {
        action_btn_->copy_label("Exit Image View");
        action_btn_->show();
        action_btn_->resize(panel_x + 5, panel_y + panel_h - ACTION_BTN_H - 5, panel_w - 10, ACTION_BTN_H);
    } else {
        action_btn_->copy_label("Scroll to Image");
        if (current_filepath_.empty() || root_dir_.empty()) {
            action_btn_->hide();
        } else {
            action_btn_->show();
            action_btn_->resize(panel_x + 5, panel_y + panel_h - ACTION_BTN_H - 5, panel_w - 10, ACTION_BTN_H);
        }
    }

    int tile_y = panel_y + crumb_h_;
    int text_bottom_margin = action_btn_->visible() ? ACTION_BTN_H + 10 : 5;
    int total_avail_h = std::max(panel_h - crumb_h_ - text_bottom_margin, 1);

    tile_group_->resize(panel_x, tile_y, panel_w, total_avail_h);

    if (current_duplicates_.empty() || !dup_group_ || !dup_browser_ || !dup_header_) {
        if (dup_group_) dup_group_->hide();
        text_display_->resize(panel_x + 5, tile_y + 5, panel_w - 10, std::max(total_avail_h - 10, 1));
        tile_group_->init_sizes();
    } else {
        dup_group_->show();
        int header_h = font_size_ + 10;
        dup_group_->set_header_height(header_h);

        int dup_h = user_dup_height_ > 0 ? user_dup_height_ : (total_avail_h * 35 / 100);
        int min_dup_h = header_h + 30;
        int min_td_h = 60;
        dup_h = std::clamp(dup_h, min_dup_h, std::max(min_dup_h, total_avail_h - min_td_h));
        int td_h = std::max(total_avail_h - dup_h, min_td_h);

        text_display_->resize(panel_x + 5, tile_y + 5, panel_w - 10, td_h - 5);
        dup_group_->resize(panel_x, tile_y + td_h, panel_w, dup_h);
        tile_group_->init_sizes();
    }

    action_btn_->redraw();
    tile_group_->redraw();
}

void InfoPanel::clear_info() {
    current_filepath_.clear();
    current_duplicates_.clear();
    if (dup_browser_) dup_browser_->clear();
    clear_breadcrumb();
    text_buffer_->text("No image selected.\nClick an image to view details.");
    if (visible()) {
        update_action_button();
    }
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
        update_action_button();
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
        update_action_button();
        return;
    }

    update_action_button();

    // ── measure & layout: one line per directory ───────────────────────────
    fl_font(FL_HELVETICA, font_size_);
    const int ROW_H   = font_size_ + 8; // button height per row
    const int ROW_GAP = 2;              // gap between rows
    const int MARGIN  = 6;              // left/right edge inset
    const int PAD     = 6;              // label padding inside widget

    // Helper: truncate label with "\u2026" until it fits within max_pixels.
    auto truncate = [&](const std::string& s, int max_pixels) -> std::string {
        if (static_cast<int>(fl_width(s.c_str())) + PAD * 2 <= max_pixels)
            return s;
        const std::string ellipsis = "\u2026"; // UTF-8 HORIZONTAL ELLIPSIS
        int ew = static_cast<int>(fl_width(ellipsis.c_str()));
        std::string r = s;
        while (!r.empty() &&
               static_cast<int>(fl_width(r.c_str())) + ew + PAD * 2 > max_pixels)
            r.pop_back();
        return r + ellipsis;
    };

    // Total bar height: rows * (ROW_H + row gap) + top/bottom margins.
    crumb_h_ = static_cast<int>(segments.size()) * (ROW_H + ROW_GAP) + ROW_GAP * 2;
    breadcrumb_bar_->resize(panel_x, panel_y, panel_w, crumb_h_);

    breadcrumb_bar_->begin();
    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& [label, abs_path] = segments[i];
        bool is_dir = !abs_path.empty();

        int indent = std::min(static_cast<int>(i) * 8, std::max(0, (panel_w - 40) / 4));
        int bx = panel_x + MARGIN + indent;
        int by = panel_y + ROW_GAP + static_cast<int>(i) * (ROW_H + ROW_GAP);
        int bw = std::max(panel_w - MARGIN * 2 - indent, 20);

        std::string display_name = is_dir ? ("📁 " + label) : ("📄 " + label);
        std::string dlabel = truncate(display_name, bw);

        if (is_dir) {
            auto* d = new CrumbData{abs_path, this, false};
            Fl_Button* btn = new Fl_Button(bx, by, bw, ROW_H);
            btn->copy_label(dlabel.c_str());
            btn->box(FL_FLAT_BOX);
            btn->color(fl_darker(FL_DARK2));
            btn->labelcolor(fl_rgb_color(100, 180, 255));
            btn->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            btn->labelsize(font_size_);
            btn->labelfont(FL_HELVETICA);
            btn->tooltip(d->path.c_str());
            btn->user_data(d);
            btn->callback([](Fl_Widget*, void* ud) {
                auto* d = static_cast<CrumbData*>(ud);
                if (d->panel->on_dir_clicked) d->panel->on_dir_clicked(d->path);
            }, d);
        } else {
            auto* d = new CrumbData{current_filepath_, this, true};
            Fl_Button* btn = new Fl_Button(bx, by, bw, ROW_H);
            btn->copy_label(dlabel.c_str());
            btn->box(FL_FLAT_BOX);
            btn->color(fl_darker(FL_DARK2));
            btn->labelcolor(FL_WHITE);
            btn->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            btn->labelsize(font_size_);
            btn->labelfont(FL_HELVETICA);
            btn->tooltip(d->path.c_str());
            btn->user_data(d);
            btn->callback([](Fl_Widget*, void* ud) {
                auto* d = static_cast<CrumbData*>(ud);
                if (d->panel->on_file_clicked) d->panel->on_file_clicked(d->path);
            }, d);
        }
    }
    breadcrumb_bar_->end();

    update_action_button();
    breadcrumb_bar_->redraw();
}

// ── display_info ───────────────────────────────────────────────────────────

void InfoPanel::display_info(const ImageEntry& entry, const std::vector<std::string>& duplicates) {
    current_filepath_ = entry.filepath;
    current_duplicates_ = duplicates;

    if (dup_browser_ && dup_header_) {
        dup_browser_->clear();
        if (!duplicates.empty()) {
            std::string header = "=== Duplicate Copies (" + std::to_string(duplicates.size()) + ") ===";
            dup_header_->copy_label(header.c_str());
            for (const auto& dup : duplicates) {
                dup_browser_->add(dup.c_str());
            }
        }
    }

    rebuild_breadcrumb();

    std::stringstream ss;

    // Full path is shown here as selectable text; the breadcrumb above shows
    // the same path as clickable navigation segments (possibly truncated).
    std::string filename = std::filesystem::path(entry.filepath).filename().string();
    ss << "=== File Information ===\n";
    ss << "Path: " << entry.filepath << "\n";
    ss << "Name: " << filename << "\n";

    uintmax_t fsize = entry.file_size;
    uintmax_t ftime = entry.file_timestamp;
    if (fsize == 0 || ftime == 0) {
        try {
            fsize = std::filesystem::file_size(entry.filepath);
            ftime = std::chrono::duration_cast<std::chrono::seconds>(
                        std::filesystem::last_write_time(entry.filepath).time_since_epoch()).count();
        } catch (...) {}
    }

    if (entry.metadata_known) {
        ss << "Dimensions: " << entry.original_width << " x " << entry.original_height << "\n";
        ss << "Size: " << format_size(fsize) << "\n";
        ss << "Modified: " << format_time(ftime) << "\n";
    } else {
        ss << "Metadata: Not yet loaded\n";
    }

    if (duplicates.empty()) {
        ss << "Duplicates: None\n";
    } else {
        ss << "Duplicates: " << duplicates.size() << " other copy/copies (listed below)\n";
    }
    ss << "\n";

    // EXIF
    std::ifstream file(entry.filepath, std::ios::binary);
    if (file.is_open()) {
        TinyEXIF::EXIFInfo exif;
        bool parsed = false;

        // Check file signature to route parsing
        uint8_t sig[2];
        if (file.read(reinterpret_cast<char*>(sig), 2)) {
            file.seekg(0, std::ios::beg); // rewind
            
            if (sig[0] == 0xFF && sig[1] == 0xD8) {
                // JPEG
                parsed = (exif.parseFrom(file) == TinyEXIF::PARSE_SUCCESS);
            } else if (sig[0] == 0x89 && sig[1] == 0x50) {
                // PNG
                std::vector<uint8_t> png_exif_data = extract_png_exif(file);
                if (!png_exif_data.empty()) {
                    parsed = (exif.parseFromEXIFSegment(png_exif_data.data(), png_exif_data.size()) == TinyEXIF::PARSE_SUCCESS);
                }
            }
        }

        if (parsed) {
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
