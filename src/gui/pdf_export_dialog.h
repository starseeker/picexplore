/*
 * pdf_export_dialog.h - Interactive PDF Export Dialog with Live In-Dialog Preview
 *
 * Copyright (c) 2026 Clifford Yapp
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_Group.H>

#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <atomic>

#include "image_store.h"
#include "../cli/pdf.h"
#include "../database.h"

class PDFPreviewWidget : public Fl_Widget {
public:
    PDFPreviewWidget(int x, int y, int w, int h);
    ~PDFPreviewWidget() override;

    void update_preview(const ImageStore& store, const PDFOptions& options, size_t page_index, DatabaseManager* db);
    void draw() override;

    size_t total_pages() const { return total_pages_; }
    size_t current_page() const { return current_page_; }

private:
    std::vector<uint8_t> preview_buf_;
    int prev_img_w_ = 0;
    int prev_img_h_ = 0;
    int sheet_x_ = 0;
    int sheet_y_ = 0;
    int sheet_w_ = 0;
    int sheet_h_ = 0;
    size_t current_page_ = 0;
    size_t total_pages_ = 1;
    double page_margin_ratio_ = 0.05;
};

class PDFExportDialog : public Fl_Double_Window {
public:
    PDFExportDialog(int w, int h, const char* title,
                    const ImageStore& store,
                    const std::string& db_path,
                    LayoutEngine::LayoutType initial_layout,
                    LayoutEngine::TreemapMetric initial_metric,
                    const std::string& root_dir,
                    const std::string& dir_filter);
    ~PDFExportDialog() override;

    static void show_dialog(const ImageStore& store,
                            const std::string& db_path,
                            LayoutEngine::LayoutType initial_layout,
                            LayoutEngine::TreemapMetric initial_metric,
                            const std::string& root_dir,
                            const std::string& dir_filter);

private:
    void setup_ui();
    void sync_options_from_ui();
    void refresh_preview();
    void start_export(const std::string& output_path);
    void cancel_export();

    static void on_control_changed(Fl_Widget* w, void* data);
    static void on_prev_page(Fl_Widget* w, void* data);
    static void on_next_page(Fl_Widget* w, void* data);
    static void on_export_clicked(Fl_Widget* w, void* data);
    static void on_cancel_clicked(Fl_Widget* w, void* data);

    const ImageStore& store_;
    std::string db_path_;
    DatabaseManager preview_db_;
    PDFOptions options_;
    size_t current_page_index_ = 0;
    size_t total_pages_ = 1;

    // UI Widgets
    Fl_Choice* choice_layout_mode_ = nullptr;
    Fl_Choice* choice_paper_size_ = nullptr;
    Fl_Choice* choice_orientation_ = nullptr;
    Fl_Choice* choice_margins_ = nullptr;
    Fl_Choice* choice_dpi_ = nullptr;

    Fl_Group* group_justified_ = nullptr;
    Fl_Value_Slider* slider_row_height_ = nullptr;
    Fl_Value_Slider* slider_spacing_ = nullptr;

    Fl_Group* group_treemap_ = nullptr;
    Fl_Choice* choice_treemap_metric_ = nullptr;
    Fl_Choice* choice_treemap_style_ = nullptr;
    Fl_Check_Button* check_container_labels_ = nullptr;

    Fl_Box* label_page_summary_ = nullptr;

    // Preview
    PDFPreviewWidget* preview_widget_ = nullptr;
    Fl_Button* btn_prev_page_ = nullptr;
    Fl_Button* btn_next_page_ = nullptr;
    Fl_Box* label_page_nav_ = nullptr;

    // Bottom Action Bar
    Fl_Progress* progress_bar_ = nullptr;
    Fl_Box* label_status_ = nullptr;
    Fl_Button* btn_export_ = nullptr;
    Fl_Button* btn_close_ = nullptr;

    // Threading
    struct ProgressState {
        std::atomic<int> current{0};
        std::atomic<int> total{0};
        std::mutex msg_mutex;
        std::string message;
        std::atomic<bool> done{false};
        std::atomic<bool> success{false};
    } progress_;

    void poll_export_progress();
    static void timer_cb(void* data);

    std::thread export_thread_;
    std::atomic<bool> export_running_{false};
    std::atomic<bool> export_stop_requested_{false};
};
