/*
 * picexplore_gui.cpp - Interactive FLTK GUI for image exploration
 *
 * Copyright (c) 2025 Clifford Yapp
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

#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <iostream>
#include <filesystem>
#include <memory>
#include <thread>

#include "stb_image.h"
#include "fl_justified_gallery.h"
#include "thumbnail_cache.h"

namespace fs = std::filesystem;

class PicExploreWindow : public Fl_Window {
public:
    PicExploreWindow(int W, int H, const char* title);
    virtual ~PicExploreWindow();

    void set_directory(const std::string& dir);

private:
    static void browse_callback(Fl_Widget* w, void* data);
    static void scan_thread_func(PicExploreWindow* win, std::string directory);
    
    void perform_fast_scan(const std::string& directory);
    void update_status(const std::string& msg);

    Fl_Button* browse_btn_;
    Fl_Box* status_box_;
    Fl_Justified_Gallery* gallery_;
    
    std::shared_ptr<ThumbnailCache> cache_;
    std::thread scan_thread_;
    bool scanning_;
};

PicExploreWindow::PicExploreWindow(int W, int H, const char* title)
    : Fl_Window(W, H, title)
    , scanning_(false)
{
    // Create UI elements
    browse_btn_ = new Fl_Button(10, 10, 150, 30, "Browse Directory...");
    browse_btn_->callback(browse_callback, this);
    
    status_box_ = new Fl_Box(170, 10, W - 180, 30, "Select a directory to begin");
    status_box_->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    status_box_->box(FL_FLAT_BOX);
    
    gallery_ = new Fl_Justified_Gallery(10, 50, W - 20, H - 60);
    
    end();
    resizable(gallery_);
    
    // Initialize cache
    cache_ = std::make_shared<ThumbnailCache>();
}

PicExploreWindow::~PicExploreWindow()
{
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
    cache_->shutdown();
}

void PicExploreWindow::browse_callback(Fl_Widget* w, void* data)
{
    PicExploreWindow* win = static_cast<PicExploreWindow*>(data);
    
    Fl_Native_File_Chooser chooser;
    chooser.title("Select Image Directory");
    chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
    
    if (chooser.show() == 0) {
        std::string directory = chooser.filename();
        win->set_directory(directory);
    }
}

void PicExploreWindow::set_directory(const std::string& dir)
{
    if (scanning_) {
        update_status("Already scanning...");
        return;
    }
    
    scanning_ = true;
    update_status("Scanning directory: " + dir);
    
    // Start scan in background thread
    if (scan_thread_.joinable()) {
        scan_thread_.join();
    }
    
    scan_thread_ = std::thread(scan_thread_func, this, dir);
}

void PicExploreWindow::scan_thread_func(PicExploreWindow* win, std::string directory)
{
    win->perform_fast_scan(directory);
}

void PicExploreWindow::perform_fast_scan(const std::string& directory)
{
    std::vector<ImageMetadata> images;
    
    try {
        // Fast pass: just collect basic metadata
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file())
                continue;
            
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && 
                ext != ".bmp" && ext != ".tga")
                continue;
            
            // Load image to get dimensions
            int width, height, channels;
            if (stbi_info(entry.path().string().c_str(), &width, &height, &channels)) {
                ImageMetadata meta;
                meta.filepath = entry.path().string();
                meta.width = width;
                meta.height = height;
                meta.aspect_ratio = (double)width / height;
                
                images.push_back(meta);
            }
            
            // Update UI periodically
            if (images.size() % 50 == 0) {
                Fl::awake([](void* data) {
                    auto* win = static_cast<PicExploreWindow*>(data);
                    win->update_status("Found " + std::to_string(
                        win->cache_->image_count()) + " images...");
                }, this);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error scanning directory: " << e.what() << std::endl;
    }
    
    // Initialize cache with images
    Fl::awake([](void* data) {
        auto* pair = static_cast<std::pair<PicExploreWindow*, std::vector<ImageMetadata>*>*>(data);
        PicExploreWindow* win = pair->first;
        std::vector<ImageMetadata>* imgs = pair->second;
        
        win->cache_->initialize(*imgs, 4);
        win->gallery_->set_cache(win->cache_);
        win->update_status("Found " + std::to_string(imgs->size()) + " images - Loading thumbnails...");
        win->scanning_ = false;
        
        delete imgs;
        delete pair;
    }, new std::pair<PicExploreWindow*, std::vector<ImageMetadata>*>(this, new std::vector<ImageMetadata>(images)));
}

void PicExploreWindow::update_status(const std::string& msg)
{
    status_box_->copy_label(msg.c_str());
    status_box_->redraw();
}

int main(int argc, char** argv)
{
    PicExploreWindow window(1200, 800, "PicExplore - Interactive Image Gallery");
    window.show(argc, argv);
    
    // If directory provided as argument, load it
    if (argc > 1) {
        window.set_directory(argv[1]);
    }
    
    return Fl::run();
}

// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8 cino=N-s
