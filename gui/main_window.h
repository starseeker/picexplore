#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scrollbar.H>
#include "image_store.h"
#include "layout_engine.h"
#include "virtual_viewport.h"
#include "scan_coordinator.h"
#include "thumbnail_pipeline.h"
#include <string>

class MainWindow : public Fl_Double_Window {
public:
    MainWindow(int w, int h, const char* title, const std::string& directory);
    ~MainWindow();

    void start();

private:
    std::string directory_;
    ImageStore store_;
    LayoutEngine layout_engine_;
    LayoutEngine::LayoutResult layout_result_;
    
    VirtualViewport* viewport_;
    Fl_Scrollbar* scrollbar_;
    class Fl_Output* info_bar_;
    class Fl_Menu_Bar* menubar_;

    moodycamel::ConcurrentQueue<UpdateEvent> update_queue_;
    ScanCoordinator* scanner_ = nullptr;
    ThumbnailPipeline* pipeline_ = nullptr;

    bool layout_dirty_ = false;

    static void timer_cb(void* data);
    void poll_events();
    
    void reprioritize_thumbnails();
    void resize(int X, int Y, int W, int H) override;
    
    static void scroll_cb(Fl_Widget* w, void* data);
    static void menu_cb(Fl_Widget* w, void* data);
    int handle(int event) override;
};
