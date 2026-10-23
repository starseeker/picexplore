#include "gui/main_window.h"
#include "utils.h"
#include <FL/Fl.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Image_Surface.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/fl_draw.H>
#include "stb_image_write.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

struct MainWindowTestAccess {
    static void dense_treemap(const std::string& base) {
        MainWindow window(640, 480, "Dense treemap", base + "/a", base + "/cache.db");
        window.active_layout_ = LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP;
        window.viewport_->resize(0, 0, 40, 40);
        for (int i = 0; i < 100; ++i) {
            window.store_.add_image(base + "/a/folder" + std::to_string(i) + "/photo.jpg", 1, 32, 32);
        }
        window.scan_complete_ = true;
        window.recompute_layout(false);
        assert(window.layout_result_.boxes.empty());
        assert(window.layout_result_.container_boxes.size() == 100);
        Fl_Image_Surface surface(40, 40);
        surface.set_current();
        surface.draw(window.viewport_);
        const auto& folder = window.layout_result_.container_boxes.front();
        auto* pixel = fl_read_image(nullptr,
            static_cast<int>(folder.x + folder.w / 2),
            static_cast<int>(folder.y + folder.h / 2), 1, 1);
        Fl_Display_Device::display_device()->set_current();
        assert(pixel);
        // Collapsed folders must still render instead of an empty-state message.
        assert(pixel[0] == 32 && pixel[1] == 34 && pixel[2] == 38);
        delete[] pixel;
    }

    static void tile_seams(const std::string& base) {
        MainWindow window(640, 480, "Tile seams", base + "/a", base + "/cache.db");
        window.viewport_->resize(0, 0, 600, 200);
        const std::string img_path = base + "/a/tiled.png";
        std::vector<uint8_t> white_rgb(1024 * 512 * 3, 240);
        assert(stbi_write_png(img_path.c_str(), 1024, 512, 3, white_rgb.data(), 1024 * 3));
        std::string hash;
        assert(compute_file_hash(img_path, hash));

        const auto id = window.store_.add_image(img_path, 2.0, 1024, 512);
        window.store_.get(id).content_hash = hash;
        TileManager tile_mgr(window.update_queue_);
        tile_mgr.init(base);
        tile_mgr.request_tiles(id, img_path, hash);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!tile_mgr.are_tiles_ready(hash)) {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        while (!tile_mgr.get_tile(hash, 0, 0, 0) ||
               !tile_mgr.get_tile(hash, 1, 0, 0)) {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        window.viewport_->enter_single_image(id);
        window.viewport_->set_tile_manager(&tile_mgr, hash, 1024, 512);
        window.viewport_->mark_full_res_ready();
        window.viewport_->zoom_ = 1.003f;
        window.viewport_->pan_x_ = 0.6f;
        window.viewport_->pan_y_ = 0.0f;
        window.viewport_->show_minimap_ = false;

        Fl_Image_Surface surface(600, 200);
        surface.set_current();
        surface.draw(window.viewport_);
        auto* row = fl_read_image(nullptr, 510, 50, 10, 1);
        Fl_Display_Device::display_device()->set_current();
        assert(row);
        for (int x = 0; x < 10; ++x) {
            assert(row[x * 3 + 0] > 200 && row[x * 3 + 1] > 200 && row[x * 3 + 2] > 200);
        }
        delete[] row;
    }

    static void external_editor(const std::string& base) {
        auto args1 = split_command_args("gimp -n", "/tmp/my photo.jpg");
        assert(args1.size() == 3 && args1[0] == "gimp" && args1[1] == "-n" && args1[2] == "/tmp/my photo.jpg");

        auto args2 = split_command_args("my-editor --file \"%f\" --extra", "/tmp/a b.png");
        assert(args2.size() == 4 && args2[0] == "my-editor" && args2[1] == "--file" &&
               args2[2] == "/tmp/a b.png" && args2[3] == "--extra");

        auto editors = detect_external_editors("custom-img-edit");
        assert(!editors.empty() && editors.front().command == "custom-img-edit");

        std::string err;
        assert(!launch_external_editor("nonexistent_editor_binary_xyz123", "/tmp/test.jpg", &err));
        assert(!err.empty());

        const std::string marker = base + "/editor_out.txt";
        const std::string script = base + "/mock_editor.sh";
        {
            std::ofstream out(script);
            out << "#!/bin/sh\nprintf '%s' \"$1\" > \"" << marker << "\"\n";
        }
        chmod(script.c_str(), 0755);
        assert(find_executable_in_path(script));

        MainWindow window(640, 480, "External editor", base + "/a", base + "/cache.db");
        const std::string target_img = base + "/a/space name.jpg";
        window.open_in_external_editor(script, target_img);
        assert(std::string(window.statusbar_->label()).find("Opened in external editor") != std::string::npos);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!std::filesystem::exists(marker) || std::filesystem::file_size(marker) == 0) {
            assert(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::ifstream in(marker);
        std::string recorded((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(recorded == target_img);
    }

    static void watch_status(const std::string& base) {
        MainWindow window(640, 480, "Watch status", base + "/a", base + "/cache.db");
        const auto id = window.store_.add_image(base + "/a/photo.jpg", 1, 32, 32);
        window.scan_complete_ = true;
        window.layout_dirty_ = false;
        window.update_statusbar();
        const std::string gallery_status = window.statusbar_->label();
        const std::string warning = "File watch limit reached. Periodic scans remain active.";
        auto send = [&](UpdateEvent::Type type, const std::string& message) {
            window.update_queue_.enqueue(UpdateEvent::make_message(type, message));
            window.poll_events();
        };
        auto expire = [&] {
            window.watch_status_until_ = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            window.poll_events(); // Restore image information even without a new event.
        };
        send(UpdateEvent::Type::WATCH_STATUS, warning);
        assert(window.statusbar_->label() == warning);
        const auto deadline = window.watch_status_until_;
        send(UpdateEvent::Type::WATCH_STATUS, warning);
        assert(window.watch_status_until_ == deadline);
        expire();
        assert(window.statusbar_->label() == gallery_status);
        assert(window.statusbar_->tooltip() == warning);
        send(UpdateEvent::Type::WATCH_STATUS, warning);
        assert(window.statusbar_->label() == gallery_status);

        window.viewport_->enter_single_image(id);
        window.update_statusbar();
        const std::string image_status = window.statusbar_->label();
        assert(image_status.find("photo.jpg") != std::string::npos);
        send(UpdateEvent::Type::WATCH_STATUS, "Could not restart file watching");
        expire();
        assert(window.statusbar_->label() == image_status);

        send(UpdateEvent::Type::DATABASE_ERROR, "Cache unavailable");
        send(UpdateEvent::Type::WATCH_STATUS, warning);
        expire();
        assert(std::string(window.statusbar_->label()) == "Cache unavailable");
        window.viewport_->exit_single_image();
        window.switch_directory(base + "/b");
        assert(window.watch_status_.empty());
        assert(window.statusbar_->tooltip() == nullptr);
    }

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
        const auto relative_path = std::filesystem::relative(path).string() + "/./";
        MainWindow window(900, 620, "PicExplore navigation", relative_path, base + "/controls.db");
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
        until([&] { return window.store_.size() == 3 && window.pending_db_build_.empty() && !window.layout_dirty_; });
        assert(window.layout_result_.layout_type == LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP);
        assert(window.layout_result_.boxes.size() == 3);
        assert(window.layout_result_.container_boxes.size() == 1);
        assert(window.layout_result_.container_boxes.front().dir_path == path + "/album");
        assert(window.breadcrumbs_->visible() && window.layout_choice_->visible());
        window.layout_choice_->value(0); window.layout_choice_->do_callback();
        assert(window.layout_mode() == LayoutEngine::LayoutType::JUSTIFIED);
        window.apply_directory_filter(relative_path + "album");
        assert(window.layout_result_.boxes.size() == 3);
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
        // Navigation can see the previous layout briefly after a deletion.
        const auto last = window.layout_result_.boxes.back().image_index;
        if (last != viewed) window.store_.remove_image(window.store_.get(last).filepath);
        window.navigate_single_image(1);
        assert(window.viewport_->current_single_image() == viewed);
        window.exit_single_image_mode();
        window.layout_choice_->value(2); window.layout_choice_->do_callback();
        assert(window.layout_mode() == LayoutEngine::LayoutType::HIERARCHICAL_TREEMAP);
        const auto deleted = window.store_.get(viewed).filepath;
        std::filesystem::remove(deleted);
        window.rescan_and_retry();
        until([&] { return !window.store_.contains(viewed); });
        assert(window.breadcrumbs_->visible());
        window.switch_directory(relative_path);
        until([&] { return window.store_.size() == 2 && !window.layout_dirty_; });
        assert(window.layout_result_.boxes.size() == 2);
        assert(window.layout_result_.container_boxes.size() == 1);
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
    MainWindowTestAccess::watch_status(base);
    MainWindowTestAccess::run(base);
    MainWindowTestAccess::controls(base);
    MainWindowTestAccess::dense_treemap(base);
    MainWindowTestAccess::tile_seams(base);
    MainWindowTestAccess::external_editor(base);
    std::filesystem::remove_all(base);
}
