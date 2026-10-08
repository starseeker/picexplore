#include "gui/main_window.h"
#include "utils.h"
#include <FL/Fl.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_draw.H>
#include "stb_image_write.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>

struct MainWindowTestAccess {
    static void controls(const std::string& base) {
        const auto path = base + "/controls";
        std::filesystem::create_directories(path + "/album");
        for (int i = 0; i < 3; ++i) {
            auto rgb = std::vector<uint8_t>(180 * 120 * 3);
            for (int y = 0; y < 120; ++y) for (int x = 0; x < 180; ++x) {
                rgb[(y * 180 + x) * 3 + (i % 3)] = 80 + x / 2;
                rgb[(y * 180 + x) * 3 + ((i + 1) % 3)] = y + 60;
            }
            const auto jpeg = encode_jpeg(rgb.data(), 180, 120);
            std::ofstream file(path + "/album/" + std::to_string(i) + ".jpg", std::ios::binary);
            file.write(reinterpret_cast<const char*>(jpeg.data()), jpeg.size());
        }
        MainWindow window(900, 620, "PicExplore navigation", path, base + "/controls.db");
        window.show(); window.start();
        auto until = [&](auto condition) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            do {
                window.poll_events(); Fl::check();
                if (condition()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            } while (std::chrono::steady_clock::now() < deadline);
            assert(false && "GUI control update timed out");
        };
        until([&] { return window.store_.size() == 3 && window.pending_db_build_.empty(); });
        assert(window.breadcrumbs_->visible() && window.layout_choice_->visible());
        window.layout_choice_->value(0); window.layout_choice_->do_callback();
        assert(window.layout_mode() == LayoutEngine::LayoutType::JUSTIFIED);
        window.apply_directory_filter(path + "/album");
        assert(window.breadcrumb_paths_.size() == 2);
        assert(window.viewport_->gallery_key(FL_Home));
        const auto first = window.viewport_->get_selected_image();
        assert(window.store_.contains(first));
        assert(window.viewport_->gallery_key(FL_Right));
        assert(window.viewport_->get_selected_image() != first);
        Fl::check(); Fl::flush(); window.make_current();
        auto* screenshot = fl_read_image(nullptr, 0, 0, window.w(), window.h());
        if (screenshot) {
            stbi_write_png("/tmp/px-ui-gallery.png", window.w(), window.h(), 3, screenshot, window.w() * 3);
            delete[] screenshot;
        }
        assert(window.viewport_->gallery_key(FL_Enter));
        until([&] { return window.viewport_->full_res_ready_; });
        const auto viewed = window.viewport_->current_single_image();
        window.rescan_and_retry();
        until([&] { return window.viewport_->full_res_ready_ && window.scan_complete_; });
        assert(window.viewport_->current_single_image() == viewed);
        window.exit_single_image_mode();
        window.layout_choice_->value(2); window.layout_choice_->do_callback();
        assert(window.layout_mode() == LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP);
        const auto deleted = window.store_.get(viewed).filepath;
        std::filesystem::remove(deleted);
        window.rescan_and_retry();
        until([&] { return !window.store_.contains(viewed); });
        assert(window.breadcrumbs_->visible());
        window.hide();
    }
    static void run(const std::string& base) {
        MainWindow window(640, 480, "Session test", base + "/a", base + "/cache.db");
        window.show();
        window.start();
        const auto first_session = window.session_id_;
        window.start();
        assert(window.session_id_ == first_session);
        const auto id = window.store_.add_image(base + "/a/photo.jpg", 1, 32, 32);
        auto old = UpdateEvent::make_thumb_rgb_ready(id, base + "/a/photo.jpg", "old",
            ThumbQuality::SMALL, std::vector<uint8_t>(3, 99), 1, 1, 0);
        old.session = first_session;
        old.job = {first_session, id, 1, 0};

        window.switch_directory(base + "/b");
        assert(window.session_id_ != first_session);
        assert(window.db_path_ == base + "/cache.db");
        window.update_queue_.enqueue(old); // A late completion from the closed session.
        const auto fresh = window.store_.add_image(base + "/b/photo.jpg", 1, 32, 32);
        assert(fresh != id);
        window.poll_events();
        assert(window.store_.get(fresh).scaled.rgb_data.empty());

        auto stale_revision = UpdateEvent::make_thumb_rgb_ready(fresh, base + "/b/photo.jpg", "old",
            ThumbQuality::SMALL, std::vector<uint8_t>(3, 99), 1, 1, 0);
        stale_revision.session = window.session_id_;
        stale_revision.job = {window.session_id_, fresh, 1, 0};
        window.store_.get(fresh).revision = 2;
        window.update_queue_.enqueue(stale_revision);
        window.poll_events();
        assert(window.store_.get(fresh).scaled.rgb_data.empty());

        window.viewport_->enter_single_image(fresh);
        window.viewer_request_ = 7;
        auto old_view = UpdateEvent::make_full_res_ready(fresh, base + "/b/photo.jpg", {}, 0, 0);
        old_view.session = window.session_id_;
        old_view.job = {window.session_id_, fresh, 2, 6};
        window.statusbar_->copy_label("new request");
        window.update_queue_.enqueue(old_view);
        window.poll_events();
        assert(!window.viewport_->full_res_ready_);
        window.hide();
    }
};

int main() {
    Fl::lock();
    char pattern[] = "/tmp/px-session-XXXXXX";
    const char* tmp = mkdtemp(pattern);
    assert(tmp);
    const std::string base(tmp);
    std::filesystem::create_directories(base + "/a");
    std::filesystem::create_directories(base + "/b");
    setenv("PICEXPLORE_CACHE_DIR", (base + "/settings").c_str(), 1);
    MainWindowTestAccess::run(base);
    MainWindowTestAccess::controls(base);
    std::filesystem::remove_all(base);
}
