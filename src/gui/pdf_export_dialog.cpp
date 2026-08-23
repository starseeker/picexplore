/*
 * pdf_export_dialog.cpp - Interactive PDF Export Dialog with Live In-Dialog Preview
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

#include "pdf_export_dialog.h"
#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_File_Chooser.H>
#include <iostream>
#include <algorithm>
#include <cmath>

// -----------------------------------------------------------------------------
// PDFPreviewWidget Implementation
// -----------------------------------------------------------------------------

PDFPreviewWidget::PDFPreviewWidget(int x, int y, int w, int h)
    : Fl_Widget(x, y, w, h) {
}

PDFPreviewWidget::~PDFPreviewWidget() {}

void PDFPreviewWidget::update_preview(const ImageStore& store, const PDFOptions& options, size_t page_index, DatabaseManager* db) {
    current_page_ = page_index;

    // Calculate total pages
    if (options.layout_type == LayoutEngine::LayoutType::JUSTIFIED) {
        std::vector<std::pair<size_t, double>> indexed_aspects;
        indexed_aspects.reserve(store.size());
        for (size_t i = 0; i < store.size(); ++i) {
            const auto& entry = store.get(i);
            double ar = (entry.aspect_ratio > 0.0) ? entry.aspect_ratio : 1.0;
            indexed_aspects.push_back({entry.index, ar});
        }
        std::vector<std::vector<Item>> boxes_per_page;
        PDFGenerator::calculate_justified_pagination(indexed_aspects, boxes_per_page, options);
        total_pages_ = std::max<size_t>(1, boxes_per_page.size());
    } else {
        total_pages_ = 1;
    }

    if (current_page_ >= total_pages_) {
        current_page_ = total_pages_ - 1;
    }

    // Determine preview sheet rectangle fitting within widget bounds with padding
    int avail_w = w() - 40;
    int avail_h = h() - 40;

    double page_w_in = options.effective_width_inches();
    double page_h_in = options.effective_height_inches();
    double page_aspect = page_w_in / page_h_in;

    double fit_w = avail_w;
    double fit_h = fit_w / page_aspect;
    if (fit_h > avail_h) {
        fit_h = avail_h;
        fit_w = fit_h * page_aspect;
    }

    sheet_w_ = std::max(50, static_cast<int>(fit_w));
    sheet_h_ = std::max(50, static_cast<int>(fit_h));
    sheet_x_ = x() + (w() - sheet_w_) / 2;
    sheet_y_ = y() + (h() - sheet_h_) / 2;

    page_margin_ratio_ = options.page_margin_inches / options.effective_width_inches();

    prev_img_w_ = sheet_w_;
    prev_img_h_ = sheet_h_;
    preview_buf_ = PDFGenerator::render_preview_page(store, options, current_page_, prev_img_w_, prev_img_h_, db);

    redraw();
}

void PDFPreviewWidget::draw() {
    // 1. Fill background canvas
    fl_color(fl_rgb_color(38, 38, 38));
    fl_rectf(x(), y(), w(), h());

    if (sheet_w_ <= 0 || sheet_h_ <= 0) return;

    // 2. Draw subtle paper drop shadow
    fl_color(fl_rgb_color(18, 18, 18));
    fl_rectf(sheet_x_ + 5, sheet_y_ + 5, sheet_w_, sheet_h_);

    // 3. Draw paper white background
    fl_color(FL_WHITE);
    fl_rectf(sheet_x_, sheet_y_, sheet_w_, sheet_h_);

    // 4. Draw rendered preview image
    if (!preview_buf_.empty() && prev_img_w_ > 0 && prev_img_h_ > 0) {
        fl_draw_image(preview_buf_.data(), sheet_x_, sheet_y_, prev_img_w_, prev_img_h_, 3, 0);
    }

    // 5. Draw faint printable margin boundary
    int margin_px = static_cast<int>(sheet_w_ * page_margin_ratio_);
    if (margin_px > 2 && margin_px < sheet_w_ / 2) {
        fl_color(fl_rgb_color(180, 200, 240));
        fl_line_style(FL_DASH);
        fl_rect(sheet_x_ + margin_px, sheet_y_ + margin_px,
                sheet_w_ - 2 * margin_px, sheet_h_ - 2 * margin_px);
        fl_line_style(0); // Reset style
    }

    // 6. Draw paper outer border
    fl_color(fl_rgb_color(90, 90, 90));
    fl_rect(sheet_x_, sheet_y_, sheet_w_, sheet_h_);
}

// -----------------------------------------------------------------------------
// PDFExportDialog Implementation
// -----------------------------------------------------------------------------

PDFExportDialog::PDFExportDialog(int w, int h, const char* title,
                                 const ImageStore& store,
                                 const std::string& db_path,
                                 LayoutEngine::LayoutType initial_layout,
                                 LayoutEngine::TreemapMetric initial_metric,
                                 const std::string& root_dir,
                                 const std::string& dir_filter)
    : Fl_Double_Window(w, h, title),
      store_(store),
      db_path_(db_path)
{
    if (!db_path_.empty()) {
        preview_db_.open(db_path_);
    }

    options_.layout_type = initial_layout;
    options_.treemap_metric = initial_metric;
    options_.root_directory = root_dir;
    options_.directory_filter = dir_filter;
    options_.set_paper_preset(PaperSize::LETTER);
    options_.orientation = PageOrientation::PORTRAIT;
    options_.page_dpi = 300.0;
    options_.page_margin_inches = 0.5;
    options_.row_height = 150;
    options_.margin = 10;

    setup_ui();
    sync_options_from_ui();
    refresh_preview();

    Fl::add_timeout(0.03, timer_cb, this);
}

PDFExportDialog::~PDFExportDialog() {
    Fl::remove_timeout(timer_cb, this);
    cancel_export();
}

void PDFExportDialog::setup_ui() {
    color(fl_rgb_color(45, 45, 45));

    int lx = 15;
    int ly = 15;
    int lw = 350;

    // --- Left Pane: Settings ---
    Fl_Box* header_settings = new Fl_Box(lx, ly, lw, 24, "PDF Document Settings");
    header_settings->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    header_settings->labelcolor(FL_WHITE);
    header_settings->labelfont(FL_HELVETICA_BOLD);
    header_settings->labelsize(14);
    ly += 30;

    // Layout Mode
    choice_layout_mode_ = new Fl_Choice(lx + 120, ly, lw - 120, 26, "Layout View:");
    choice_layout_mode_->labelcolor(FL_WHITE);
    choice_layout_mode_->add("Justified Grid");
    choice_layout_mode_->add("Flat Treemap");
    choice_layout_mode_->add("Hierarchical Treemap");
    if (options_.layout_type == LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP) {
        choice_layout_mode_->value(2);
    } else if (options_.layout_type == LayoutEngine::LayoutType::TREEMAP) {
        choice_layout_mode_->value(1);
    } else {
        choice_layout_mode_->value(0);
    }
    choice_layout_mode_->callback(on_control_changed, this);
    ly += 34;

    // Paper Preset
    choice_paper_size_ = new Fl_Choice(lx + 120, ly, lw - 120, 26, "Paper Size:");
    choice_paper_size_->labelcolor(FL_WHITE);
    choice_paper_size_->add("US Letter (8.5 x 11 in)");
    choice_paper_size_->add("US Legal (8.5 x 14 in)");
    choice_paper_size_->add("US Tabloid (11 x 17 in)");
    choice_paper_size_->add("ISO A4 (210 x 297 mm)");
    choice_paper_size_->add("ISO A3 (297 x 420 mm)");
    choice_paper_size_->add("ISO A2 (420 x 594 mm)");
    choice_paper_size_->add("ISO A1 (594 x 841 mm)");
    choice_paper_size_->value(0);
    choice_paper_size_->callback(on_control_changed, this);
    ly += 34;

    // Orientation
    choice_orientation_ = new Fl_Choice(lx + 120, ly, lw - 120, 26, "Orientation:");
    choice_orientation_->labelcolor(FL_WHITE);
    choice_orientation_->add("Portrait");
    choice_orientation_->add("Landscape");
    choice_orientation_->value(0);
    choice_orientation_->callback(on_control_changed, this);
    ly += 34;

    // Margins
    choice_margins_ = new Fl_Choice(lx + 120, ly, lw - 120, 26, "Page Margins:");
    choice_margins_->labelcolor(FL_WHITE);
    choice_margins_->add("0.25 in (Narrow)");
    choice_margins_->add("0.50 in (Standard)");
    choice_margins_->add("0.75 in (Moderate)");
    choice_margins_->add("1.00 in (Wide)");
    choice_margins_->value(1);
    choice_margins_->callback(on_control_changed, this);
    ly += 34;

    // DPI Resolution
    choice_dpi_ = new Fl_Choice(lx + 120, ly, lw - 120, 26, "Resolution:");
    choice_dpi_->labelcolor(FL_WHITE);
    choice_dpi_->add("150 DPI (Draft / Fast)");
    choice_dpi_->add("300 DPI (Standard Print)");
    choice_dpi_->add("600 DPI (High Resolution)");
    choice_dpi_->value(1);
    choice_dpi_->callback(on_control_changed, this);
    ly += 42;

    // --- Mode-Specific Groups ---
    // 1. Justified Layout Group
    group_justified_ = new Fl_Group(lx, ly, lw, 130);
    {
        Fl_Box* header_just = new Fl_Box(lx, ly, lw, 22, "Justified Grid Options");
        header_just->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        header_just->labelcolor(fl_rgb_color(220, 220, 220));
        header_just->labelfont(FL_HELVETICA_BOLD);

        slider_row_height_ = new Fl_Value_Slider(lx + 120, ly + 28, lw - 120, 24, "Row Height:");
        slider_row_height_->type(FL_HOR_SLIDER);
        slider_row_height_->bounds(60, 400);
        slider_row_height_->step(10);
        slider_row_height_->value(150);
        slider_row_height_->labelcolor(FL_WHITE);
        slider_row_height_->callback(on_control_changed, this);

        slider_spacing_ = new Fl_Value_Slider(lx + 120, ly + 62, lw - 120, 24, "Image Gap:");
        slider_spacing_->type(FL_HOR_SLIDER);
        slider_spacing_->bounds(0, 30);
        slider_spacing_->step(2);
        slider_spacing_->value(10);
        slider_spacing_->labelcolor(FL_WHITE);
        slider_spacing_->callback(on_control_changed, this);
    }
    group_justified_->end();

    // 2. Treemap Group
    group_treemap_ = new Fl_Group(lx, ly, lw, 150);
    {
        Fl_Box* header_tree = new Fl_Box(lx, ly, lw, 22, "Treemap Options");
        header_tree->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        header_tree->labelcolor(fl_rgb_color(220, 220, 220));
        header_tree->labelfont(FL_HELVETICA_BOLD);

        choice_treemap_metric_ = new Fl_Choice(lx + 120, ly + 28, lw - 120, 26, "Tile Sizing:");
        choice_treemap_metric_->labelcolor(FL_WHITE);
        choice_treemap_metric_->add("File Size");
        choice_treemap_metric_->add("Pixel Area");
        choice_treemap_metric_->add("Duplicate Count");
        choice_treemap_metric_->add("Equal Size");
        switch (options_.treemap_metric) {
            case LayoutEngine::TreemapMetric::FILE_SIZE: choice_treemap_metric_->value(0); break;
            case LayoutEngine::TreemapMetric::PIXEL_AREA: choice_treemap_metric_->value(1); break;
            case LayoutEngine::TreemapMetric::DUPLICATE_COUNT: choice_treemap_metric_->value(2); break;
            case LayoutEngine::TreemapMetric::EQUAL_SIZE: choice_treemap_metric_->value(3); break;
        }
        choice_treemap_metric_->callback(on_control_changed, this);

        choice_treemap_style_ = new Fl_Choice(lx + 120, ly + 62, lw - 120, 26, "Render Style:");
        choice_treemap_style_->labelcolor(FL_WHITE);
        choice_treemap_style_->add("All Thumbnails");
        choice_treemap_style_->add("Cushion Treemap");
        choice_treemap_style_->add("File Type Colors");
        choice_treemap_style_->value(0);
        choice_treemap_style_->callback(on_control_changed, this);

        check_container_labels_ = new Fl_Check_Button(lx + 20, ly + 98, lw - 40, 24, "Draw Directory Headers & Borders");
        check_container_labels_->labelcolor(FL_WHITE);
        check_container_labels_->value(1);
        check_container_labels_->callback(on_control_changed, this);
    }
    group_treemap_->end();
    ly += 160;

    // Document Summary Box
    label_page_summary_ = new Fl_Box(lx, ly, lw, 44, "Loading document info...");
    label_page_summary_->box(FL_FLAT_BOX);
    label_page_summary_->color(fl_rgb_color(32, 32, 32));
    label_page_summary_->labelcolor(fl_rgb_color(200, 200, 200));
    label_page_summary_->labelsize(12);
    label_page_summary_->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

    // --- Right Pane: Preview & Page Navigation ---
    int rx = 385;
    int ry = 15;
    int rw = w() - rx - 15;

    btn_prev_page_ = new Fl_Button(rx, ry, 80, 26, "@< Prev");
    btn_prev_page_->callback(on_prev_page, this);

    label_page_nav_ = new Fl_Box(rx + 90, ry, rw - 180, 26, "Page 1 of 1");
    label_page_nav_->labelcolor(FL_WHITE);
    label_page_nav_->labelfont(FL_HELVETICA_BOLD);

    btn_next_page_ = new Fl_Button(rx + rw - 80, ry, 80, 26, "Next @>");
    btn_next_page_->callback(on_next_page, this);

    preview_widget_ = new PDFPreviewWidget(rx, ry + 35, rw, h() - 125);

    // --- Bottom Bar: Progress, Status & Buttons ---
    int by = h() - 75;
    int status_w = w() - 275;

    label_status_ = new Fl_Box(lx, by, status_w, 22, "Ready to export.");
    label_status_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    label_status_->labelcolor(fl_rgb_color(220, 220, 220));
    label_status_->labelsize(13);

    progress_bar_ = new Fl_Progress(lx, by + 26, status_w, 24);
    progress_bar_->minimum(0.0f);
    progress_bar_->maximum(100.0f);
    progress_bar_->value(0.0f);
    progress_bar_->color(fl_rgb_color(36, 36, 36));
    progress_bar_->selection_color(fl_rgb_color(40, 140, 230));

    btn_export_ = new Fl_Button(w() - 250, by + 10, 130, 40, "Export PDF...");
    btn_export_->color(fl_rgb_color(40, 120, 210));
    btn_export_->labelcolor(FL_WHITE);
    btn_export_->labelfont(FL_HELVETICA_BOLD);
    btn_export_->callback(on_export_clicked, this);

    btn_close_ = new Fl_Button(w() - 105, by + 10, 90, 40, "Close");
    btn_close_->callback(on_cancel_clicked, this);

    end();
    resizable(preview_widget_);
}

void PDFExportDialog::sync_options_from_ui() {
    // 1. Layout Mode
    int layout_val = choice_layout_mode_->value();
    if (layout_val == 0) {
        options_.layout_type = LayoutEngine::LayoutType::JUSTIFIED;
        group_justified_->show();
        group_treemap_->hide();
        btn_prev_page_->show();
        btn_next_page_->show();
        label_page_nav_->show();
    } else if (layout_val == 1) {
        options_.layout_type = LayoutEngine::LayoutType::TREEMAP;
        group_justified_->hide();
        group_treemap_->show();
        btn_prev_page_->hide();
        btn_next_page_->hide();
        label_page_nav_->hide();
    } else {
        options_.layout_type = LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP;
        group_justified_->hide();
        group_treemap_->show();
        btn_prev_page_->hide();
        btn_next_page_->hide();
        label_page_nav_->hide();
    }

    // 2. Paper Size
    int paper_val = choice_paper_size_->value();
    switch (paper_val) {
        case 0: options_.set_paper_preset(PaperSize::LETTER); break;
        case 1: options_.set_paper_preset(PaperSize::LEGAL); break;
        case 2: options_.set_paper_preset(PaperSize::TABLOID); break;
        case 3: options_.set_paper_preset(PaperSize::A4); break;
        case 4: options_.set_paper_preset(PaperSize::A3); break;
        case 5: options_.set_paper_preset(PaperSize::A2); break;
        case 6: options_.set_paper_preset(PaperSize::A1); break;
    }

    // 3. Orientation
    options_.orientation = (choice_orientation_->value() == 1) ? PageOrientation::LANDSCAPE : PageOrientation::PORTRAIT;

    // 4. Margins
    int margin_val = choice_margins_->value();
    switch (margin_val) {
        case 0: options_.page_margin_inches = 0.25; break;
        case 1: options_.page_margin_inches = 0.50; break;
        case 2: options_.page_margin_inches = 0.75; break;
        case 3: options_.page_margin_inches = 1.00; break;
    }

    // 5. DPI
    int dpi_val = choice_dpi_->value();
    switch (dpi_val) {
        case 0: options_.page_dpi = 150.0; break;
        case 1: options_.page_dpi = 300.0; break;
        case 2: options_.page_dpi = 600.0; break;
    }

    // 6. Justified Parameters
    options_.row_height = static_cast<int>(slider_row_height_->value());
    options_.margin = static_cast<int>(slider_spacing_->value());

    // 7. Treemap Parameters
    int metric_val = choice_treemap_metric_->value();
    switch (metric_val) {
        case 0: options_.treemap_metric = LayoutEngine::TreemapMetric::FILE_SIZE; break;
        case 1: options_.treemap_metric = LayoutEngine::TreemapMetric::PIXEL_AREA; break;
        case 2: options_.treemap_metric = LayoutEngine::TreemapMetric::DUPLICATE_COUNT; break;
        case 3: options_.treemap_metric = LayoutEngine::TreemapMetric::EQUAL_SIZE; break;
    }

    int style_val = choice_treemap_style_->value();
    switch (style_val) {
        case 0: options_.treemap_render_style = PDFTreemapRenderStyle::ALL_THUMBNAILS; break;
        case 1: options_.treemap_render_style = PDFTreemapRenderStyle::CUSHION_TREEMAP; break;
        case 2: options_.treemap_render_style = PDFTreemapRenderStyle::FILE_TYPE_COLORS; break;
    }

    options_.show_container_labels = (check_container_labels_->value() != 0);
}

void PDFExportDialog::refresh_preview() {
    DatabaseManager* db_ptr = preview_db_.is_open() ? &preview_db_ : nullptr;
    preview_widget_->update_preview(store_, options_, current_page_index_, db_ptr);
    total_pages_ = preview_widget_->total_pages();
    current_page_index_ = preview_widget_->current_page();

    // Update Page Navigation Label
    if (options_.layout_type == LayoutEngine::LayoutType::JUSTIFIED) {
        std::string nav_str = "Page " + std::to_string(current_page_index_ + 1) + " of " + std::to_string(total_pages_);
        label_page_nav_->copy_label(nav_str.c_str());
        btn_prev_page_->activate();
        btn_next_page_->activate();
        if (current_page_index_ == 0) btn_prev_page_->deactivate();
        if (current_page_index_ + 1 >= total_pages_) btn_next_page_->deactivate();

        std::string summary_str = "Total Pages: " + std::to_string(total_pages_) +
                                  " | Total Images: " + std::to_string(store_.size());
        label_page_summary_->copy_label(summary_str.c_str());
    } else {
        std::string summary_str = "Single-Page Treemap Document | Images: " + std::to_string(store_.size());
        label_page_summary_->copy_label(summary_str.c_str());
    }
}

void PDFExportDialog::on_control_changed(Fl_Widget*, void* data) {
    auto* self = static_cast<PDFExportDialog*>(data);
    self->sync_options_from_ui();
    self->refresh_preview();
}

void PDFExportDialog::on_prev_page(Fl_Widget*, void* data) {
    auto* self = static_cast<PDFExportDialog*>(data);
    if (self->current_page_index_ > 0) {
        self->current_page_index_--;
        self->refresh_preview();
    }
}

void PDFExportDialog::on_next_page(Fl_Widget*, void* data) {
    auto* self = static_cast<PDFExportDialog*>(data);
    if (self->current_page_index_ + 1 < self->total_pages_) {
        self->current_page_index_++;
        self->refresh_preview();
    }
}

void PDFExportDialog::on_export_clicked(Fl_Widget*, void* data) {
    auto* self = static_cast<PDFExportDialog*>(data);
    if (self->export_running_) return;

    const char* save_path = fl_file_chooser("Save PDF Export", "*.pdf", "picexplore_export.pdf");
    if (!save_path || strlen(save_path) == 0) return;

    self->start_export(save_path);
}

void PDFExportDialog::on_cancel_clicked(Fl_Widget*, void* data) {
    auto* self = static_cast<PDFExportDialog*>(data);
    if (self->export_running_) {
        self->cancel_export();
    }
    self->hide();
}

void PDFExportDialog::timer_cb(void* data) {
    auto* self = static_cast<PDFExportDialog*>(data);
    self->poll_export_progress();
    Fl::repeat_timeout(0.03, timer_cb, data);
}

void PDFExportDialog::poll_export_progress() {
    if (!export_running_) return;

    int curr = progress_.current.load();
    int tot = progress_.total.load();
    std::string msg;
    {
        std::lock_guard<std::mutex> lock(progress_.msg_mutex);
        msg = progress_.message;
    }

    float pct = (tot > 0) ? (static_cast<float>(curr) / tot * 100.0f) : 0.0f;
    progress_bar_->value(pct);
    if (!msg.empty()) {
        label_status_->copy_label(msg.c_str());
    }
    progress_bar_->redraw();
    label_status_->redraw();

    if (progress_.done.load()) {
        if (export_thread_.joinable()) {
            export_thread_.join();
        }
        export_running_ = false;
        btn_export_->activate();
        btn_close_->label("Close");

        if (progress_.success.load()) {
            progress_bar_->value(100.0f);
            label_status_->copy_label("PDF Export complete!");
        } else if (export_stop_requested_.load()) {
            label_status_->copy_label("PDF Export cancelled.");
        } else {
            label_status_->copy_label("PDF Export failed.");
        }
        progress_bar_->redraw();
        label_status_->redraw();
    }
}

void PDFExportDialog::start_export(const std::string& output_path) {
    if (export_running_) return;

    export_running_ = true;
    export_stop_requested_ = false;
    progress_.current.store(0);
    progress_.total.store(0);
    {
        std::lock_guard<std::mutex> lock(progress_.msg_mutex);
        progress_.message = "Starting PDF export...";
    }
    progress_.done.store(false);
    progress_.success.store(false);

    progress_bar_->value(0.0f);
    progress_bar_->redraw();
    label_status_->copy_label("Starting PDF export...");
    label_status_->redraw();
    btn_export_->deactivate();
    btn_close_->label("Cancel");
    Fl::flush();

    std::string db_p = db_path_;

    export_thread_ = std::thread([this, output_path, db_p]() {
        DatabaseManager export_db;
        DatabaseManager* db_ptr = nullptr;
        if (!db_p.empty() && export_db.open(db_p)) {
            db_ptr = &export_db;
        }

        PDFGenerator generator;
        bool ok = generator.generate_from_store(
            store_, output_path, options_, db_ptr,
            [this](int curr, int total, const std::string& msg) {
                progress_.current.store(curr);
                progress_.total.store(total);
                {
                    std::lock_guard<std::mutex> lock(progress_.msg_mutex);
                    progress_.message = msg;
                }
            },
            &export_stop_requested_);

        progress_.success.store(ok);
        progress_.done.store(true);
    });
}

void PDFExportDialog::cancel_export() {
    if (export_running_) {
        export_stop_requested_ = true;
        label_status_->copy_label("Cancelling PDF export...");
        label_status_->redraw();
        Fl::flush();
        if (export_thread_.joinable()) {
            export_thread_.join();
        }
        export_running_ = false;
    }
}

void PDFExportDialog::show_dialog(const ImageStore& store,
                                  const std::string& db_path,
                                  LayoutEngine::LayoutType initial_layout,
                                  LayoutEngine::TreemapMetric initial_metric,
                                  const std::string& root_dir,
                                  const std::string& dir_filter) {
    PDFExportDialog* dlg = new PDFExportDialog(920, 640, "Export as PDF",
                                               store, db_path,
                                               initial_layout, initial_metric,
                                               root_dir, dir_filter);
    dlg->set_modal();
    dlg->show();
    while (dlg->shown()) {
        dlg->poll_export_progress();
        Fl::wait(0.03);
    }
    delete dlg;
}
