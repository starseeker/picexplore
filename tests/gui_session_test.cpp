#include "gui/main_window.h"
#include "utils.h"
#include <FL/Fl.H>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>

struct MainWindowTestAccess {
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
    std::filesystem::remove_all(base);
}
