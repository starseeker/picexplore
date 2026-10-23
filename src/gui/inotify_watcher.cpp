#include "inotify_watcher.h"
#include "utils.h"
#include <unistd.h>
#include <poll.h>
#include <filesystem>
#include <cstring>
#include <cerrno>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
InotifyWatcher::~InotifyWatcher() { stop(); }
void InotifyWatcher::start(const std::string& directory, moodycamel::ConcurrentQueue<UpdateEvent>& queue, uint64_t session) {
    if (watch_thread_.joinable()) return;
    directory_ = normalize_path(directory);
    update_queue_ = EventPublisher(queue, session, &stop_requested_);
    stop_requested_ = false;
    inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd_ == -1) {
        degraded("Could not initialize file watching");
        return;
    }
    watch_thread_ = std::thread(&InotifyWatcher::watch_thread_func, this);
}
void InotifyWatcher::stop() {
    stop_requested_ = true;
    if (watch_thread_.joinable()) watch_thread_.join();
    if (inotify_fd_ != -1) close(inotify_fd_);
    inotify_fd_ = -1;
    wd_to_path_.clear();
    pending_renames_.clear();
}
void InotifyWatcher::changed() { changed_ = true; }
void InotifyWatcher::degraded(const std::string& reason) {
    if (!degraded_) update_queue_.enqueue(UpdateEvent::make_message(UpdateEvent::Type::WATCH_STATUS,
        reason + ". Periodic scans remain active; File > Rescan retries now."));
    degraded_ = true;
    changed();
}
void InotifyWatcher::add_watch_recursive(const std::string& path) {
    if (stop_requested_ || is_cache_or_db_path(path)) return;
    auto add = [this](const std::string& dir) {
        if (wd_to_path_.size() >= 16384) { degraded("File watch limit reached"); return; }
        int wd = inotify_add_watch(inotify_fd_, dir.c_str(), IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM |
            IN_MOVED_TO | IN_CREATE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR | IN_DONT_FOLLOW);
        if (wd < 0) { degraded("Cannot watch " + dir + ": " + std::strerror(errno)); return; }
        wd_to_path_[wd] = dir;
    };
    add(path);
    if (!recursive_) return;
    std::error_code ec;
    fs::recursive_directory_iterator it(path, fs::directory_options::none, ec), end;
    if (ec) degraded("Cannot enumerate watched directories in " + path);
    while (it != end && !stop_requested_) {
        const std::string child = it->path().string();
        std::error_code status_error;
        if (is_cache_or_db_path(child)) it.disable_recursion_pending();
        else if (it->is_directory(status_error) && !it->is_symlink(status_error)) add(child);
        it.increment(ec);
        if (ec) degraded("Some directories could not be watched");
    }
}
void InotifyWatcher::remove_subtree(const std::string& path) {
    for (auto it = wd_to_path_.begin(); it != wd_to_path_.end();) {
        if (path_is_within(it->second, path)) {
            inotify_rm_watch(inotify_fd_, it->first);
            it = wd_to_path_.erase(it);
        } else ++it;
    }
}
void InotifyWatcher::expire_renames() {
    const auto now = Clock::now();
    for (auto it = pending_renames_.begin(); it != pending_renames_.end();) {
        if (now - it->second.time >= std::chrono::milliseconds(250)) {
            if (it->second.directory) remove_subtree(it->second.path);
            it = pending_renames_.erase(it);
            changed();
        } else ++it;
    }
}
void InotifyWatcher::handle_event(const inotify_event& event) {
    // Overflow and watch lifecycle events have no filename.
    if (event.mask & IN_Q_OVERFLOW) {
        degraded("File notifications overflowed; reconciling the directory");
        rebuild_ = true;
        return;
    }
    const auto entry = wd_to_path_.find(event.wd);
    if (entry == wd_to_path_.end()) return;
    const std::string dir = entry->second;
    if (event.mask & IN_IGNORED) { wd_to_path_.erase(entry); changed(); return; }
    if (event.mask & (IN_UNMOUNT | IN_DELETE_SELF | IN_MOVE_SELF)) {
        changed();
        if (dir == directory_ || (event.mask & IN_UNMOUNT)) rebuild_ = true;
        if (event.mask & IN_UNMOUNT) degraded("A watched filesystem was unmounted");
    }
    if (event.len == 0) return;
    const std::string path = (fs::path(dir) / event.name).lexically_normal().string();
    if (is_cache_or_db_path(path)) return;
    const bool directory = event.mask & IN_ISDIR;
    // Pair all names: editors commonly rename a non-image temporary file over an image.
    if (event.mask & IN_MOVED_FROM) {
        pending_renames_[event.cookie] = {path, directory, Clock::now()};
        return;
    }
    if (event.mask & IN_MOVED_TO) {
        auto from = pending_renames_.find(event.cookie);
        if (from != pending_renames_.end()) {
            const std::string old_path = from->second.path;
            if (directory) for (auto& watch : wd_to_path_)
                if (path_is_within(watch.second, old_path)) watch.second = path + watch.second.substr(old_path.size());
            auto rename = UpdateEvent::make_image_renamed(old_path, path);
            rename.type = UpdateEvent::Type::FILESYSTEM_RENAMED;
            update_queue_.enqueue(std::move(rename));
            pending_renames_.erase(from);
        }
        if (directory && recursive_) add_watch_recursive(path);
        changed();
    } else if (event.mask & (IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_ATTRIB)) {
        if (directory && recursive_ && (event.mask & IN_CREATE)) add_watch_recursive(path);
        if (directory && (event.mask & IN_DELETE)) remove_subtree(path);
        changed();
    }
}
void InotifyWatcher::watch_thread_func() {
    auto install_initial_watches = [this]() {
        if (!watch_dirs_.empty()) {
            for (const auto& d : watch_dirs_) add_watch_recursive(normalize_path(d));
        } else {
            add_watch_recursive(directory_);
        }
    };
    install_initial_watches();
    // Rescan after watch installation closes the initial scan/registration race.
    changed();
    auto retry_at = Clock::now() + std::chrono::seconds(10);
    alignas(inotify_event) char buffer[65536];
    while (!stop_requested_) {
        pollfd fd{inotify_fd_, POLLIN, 0};
        const int ready = poll(&fd, 1, 100);
        if (ready < 0 && errno != EINTR) { degraded("File watcher read failed"); break; }
        for (;;) {
            ssize_t len = read(inotify_fd_, buffer, sizeof(buffer));
            if (len <= 0) {
                if (len < 0 && errno != EAGAIN && errno != EINTR) degraded("File watcher read failed");
                break;
            }
            for (size_t offset = 0; offset + sizeof(inotify_event) <= size_t(len);) {
                const auto& event = *reinterpret_cast<const inotify_event*>(buffer + offset);
                if (offset + sizeof(event) + event.len > size_t(len)) { rebuild_ = true; changed(); break; }
                handle_event(event);
                offset += sizeof(event) + event.len;
            }
            if (stop_requested_) break;
        }
        expire_renames();
        if (rebuild_ || (degraded_ && Clock::now() >= retry_at)) {
            rebuild_ = false;
            degraded_ = false;
            // Rebuild the descriptor set too: after overflow, a path can now name a
            // different inode while its old watch still follows a moved-out directory.
            close(inotify_fd_);
            inotify_fd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            wd_to_path_.clear();
            pending_renames_.clear();
            if (inotify_fd_ < 0) { degraded("Could not restart file watching"); break; }
            install_initial_watches();
            retry_at = Clock::now() + std::chrono::seconds(10);
            changed();
        }
        if (changed_) {
            update_queue_.enqueue(UpdateEvent::make_message(UpdateEvent::Type::FILESYSTEM_CHANGED, ""));
            changed_ = false;
        }
    }
}
