#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scrollbar.H>
#include <FL/Fl_Box.H>
#include "image_store.h"
#include "layout_engine.h"
#include "virtual_viewport.h"
#include "scan_coordinator.h"
#include "thumbnail_pipeline.h"
#include "info_panel.h"
#include "full_res_loader.h"
#include <string>

class MainWindow : public Fl_Double_Window {
public:
    MainWindow(int w, int h, const char* title, const std::string& directory);
    ~MainWindow();

    void start();

    void enter_single_image_mode(size_t raw_idx, const std::string& filepath);
    void exit_single_image_mode();

private:
    std::string directory_;
    ImageStore store_;
    LayoutEngine layout_engine_;
    LayoutEngine::LayoutResult layout_result_;
    
    VirtualViewport* viewport_;
    Fl_Scrollbar*    scrollbar_;
    InfoPanel*       info_panel_;
    class Fl_Menu_Bar* menubar_;
    Fl_Box*          statusbar_;
    bool info_panel_visible_ = false;

    // Directory filter (empty string = no filter = show all)
    std::string directory_filter_;
    void apply_directory_filter(const std::string& dir);
    void reset_directory_filter();
    void update_statusbar();

    moodycamel::ConcurrentQueue<UpdateEvent> update_queue_;
    ScanCoordinator* scanner_ = nullptr;
    ThumbnailPipeline* pipeline_ = nullptr;
    FullResLoader* full_res_loader_ = nullptr;
    class FileWatcher* watcher_ = nullptr;
    class DatabaseManager* db_ = nullptr;

    std::string pre_viewer_filter_;
    std::string current_selected_filepath_;

    bool layout_dirty_ = true;
    
    std::vector<size_t> last_visible_;
    double last_target_height_ = 0.0;
    int last_viewport_width_ = 0;
    double target_height_ = 150.0;
    uint64_t current_generation_ = 0;

    static void timer_cb(void* data);
    void poll_events();
    
    void reprioritize_thumbnails();
    void resize(int X, int Y, int W, int H) override;
    
    static void scroll_cb(Fl_Widget* w, void* data);
    static void menu_cb(Fl_Widget* w, void* data);
    int handle(int event) override;
};
