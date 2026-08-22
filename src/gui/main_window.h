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
#include "tile_manager.h"
#include "file_type_colors.h"
#include <string>
#include <unordered_set>
#include <chrono>

class FileTypeLegendWidget : public Fl_Widget {
public:
    FileTypeLegendWidget(int X, int Y, int W, int H)
        : Fl_Widget(X, Y, W, H, "") {}

    void draw() override;
};

class MainWindow : public Fl_Double_Window {
public:
    MainWindow(int w, int h, const char* title, const std::string& directory, const std::string& db_path = "");
    ~MainWindow();

    void start();

    void enter_single_image_mode(size_t raw_idx, const std::string& filepath);
    void exit_single_image_mode();
    void navigate_single_image(int delta);

    void set_layout_mode(LayoutEngine::LayoutType type);
    LayoutEngine::LayoutType layout_mode() const { return active_layout_; }

    void set_treemap_metric(LayoutEngine::TreemapMetric metric);
    LayoutEngine::TreemapMetric treemap_metric() const { return treemap_metric_; }

    void set_treemap_style(VirtualViewport::TreemapRenderStyle style);
    VirtualViewport::TreemapRenderStyle treemap_style() const { return treemap_style_; }

    void resize(int X, int Y, int W, int H) override;

protected:
    void draw() override;

private:
    std::string directory_;
    std::string db_path_;
    ImageStore store_;
    LayoutEngine layout_engine_;
    LayoutEngine::LayoutResult layout_result_;
    
    LayoutEngine::LayoutType active_layout_ = LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP;
    LayoutEngine::TreemapMetric treemap_metric_ = LayoutEngine::TreemapMetric::FILE_SIZE;
    VirtualViewport::TreemapRenderStyle treemap_style_ = VirtualViewport::TreemapRenderStyle::ALL_THUMBNAILS;
    ImageStore::SortCriteria current_sort_criteria_ = ImageStore::SortCriteria::ALPHABETICAL;
    bool sort_ascending_ = true;

    TileManager*     tile_manager_ = nullptr;
    VirtualViewport* viewport_;
    Fl_Scrollbar*    scrollbar_;
    InfoPanel*       info_panel_;
    class Fl_Menu_Bar* menubar_;
    Fl_Box*          statusbar_;
    Fl_Box*          statusbar_hint_;
    FileTypeLegendWidget* legend_widget_;
    
    bool info_panel_visible_ = false;
    int  info_panel_width_ = 280;
    bool dragging_h_splitter_ = false;
    bool in_splitter_hover_ = false;
    int  drag_start_x_ = 0;
    int  drag_start_info_w_ = 0;

    // Database build progress tracking
    int db_build_total_ = 0;
    std::unordered_set<size_t> pending_db_build_;
    bool scan_complete_ = false;

    // Directory filter (empty string = no filter = show all)
    std::string directory_filter_;
    void apply_directory_filter(const std::string& dir);
    void reset_directory_filter();
    void update_statusbar();
    void rebuild_menu();
    void recompute_layout(bool reprioritize = true);

    std::vector<std::string> reconcile_and_get_duplicates(const std::string& hash, const std::string& current_filepath);

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
    int last_viewport_height_ = 0;
    LayoutEngine::LayoutType last_active_layout_ = LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP;
    std::string last_directory_filter_;
    LayoutEngine::TreemapMetric last_treemap_metric_ = LayoutEngine::TreemapMetric::FILE_SIZE;
    VirtualViewport::TreemapRenderStyle last_treemap_style_ = VirtualViewport::TreemapRenderStyle::ALL_THUMBNAILS;
    double target_height_ = 150.0;
    uint64_t current_generation_ = 0;
    bool reprioritize_pending_ = false;
    std::chrono::steady_clock::time_point last_resize_time_;
    std::chrono::steady_clock::time_point last_layout_recompute_time_;

    static void timer_cb(void* data);
    void poll_events();
    
    void reprioritize_thumbnails();
    
    static void scroll_cb(Fl_Widget* w, void* data);
    static void menu_cb(Fl_Widget* w, void* data);
    int handle(int event) override;
};
