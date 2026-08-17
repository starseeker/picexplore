#pragma once

#include <FL/Fl_Group.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Box.H>
#include <string>
#include <functional>
#include <vector>
#include "image_store.h"

class InfoPanel : public Fl_Group {
public:
    InfoPanel(int X, int Y, int W, int H, const char* L = 0);
    virtual ~InfoPanel();

    void display_info(const ImageEntry& entry);
    void clear_info();
    void set_font_size(int size);
    int  get_font_size() const { return font_size_; }

    // Set the root directory (launch-time path). Segments above this are never shown.
    void set_root_dir(const std::string& root);

    // Called when a breadcrumb directory segment is clicked.
    // Receives the absolute path of the selected directory.
    // Only wired when the info panel is already visible.
    std::function<void(const std::string&)> on_dir_clicked;

    virtual void resize(int X, int Y, int W, int H) override;

private:
    // crumb_h_ is computed dynamically by rebuild_breadcrumb() based on how
    // many rows the breadcrumb needs at the current font size and panel width.
    int              crumb_h_ = 0;

    Fl_Group*        breadcrumb_bar_;
    Fl_Text_Display* text_display_;
    Fl_Text_Buffer*  text_buffer_;
    int              font_size_ = 12;
    std::string      root_dir_;
    std::string      current_filepath_; // path of last selected image

    void clear_breadcrumb();  // frees user_data strings then clears the group
    void rebuild_breadcrumb();
};
