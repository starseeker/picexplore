#pragma once

#include <FL/Fl_Group.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <string>
#include "image_store.h"

class InfoPanel : public Fl_Group {
public:
    InfoPanel(int X, int Y, int W, int H, const char* L = 0);
    virtual ~InfoPanel();

    void display_info(const ImageEntry& entry);
    void clear_info();

    virtual void resize(int X, int Y, int W, int H) override;

private:
    Fl_Text_Display* text_display_;
    Fl_Text_Buffer* text_buffer_;
};
