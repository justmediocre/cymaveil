#include "watcher.h"

#if defined(__linux__)

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <unordered_map>

#include "raylib.h"  // TraceLog

// raylib's GLFW backend; safe to call from any thread, wakes WaitEvents.
extern "C" void glfwPostEmptyEvent(void);

namespace fs = std::filesystem;

namespace {

// Collapse a burst of events (e.g. copying an album in) into one rescan.
constexpr int kDebounceMs = 900;

constexpr uint32_t kWatchMask = IN_CREATE | IN_CLOSE_WRITE | IN_DELETE | IN_MOVED_FROM |
                                IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;

bool HasAudioExt(const char* name) {
    if (name == nullptr) return false;
    const char* dot = std::strrchr(name, '.');
    if (dot == nullptr) return false;
    std::string ext(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext == ".mp3" || ext == ".flac" || ext == ".ogg" || ext == ".wav";
}

}  // namespace

FolderWatcher::~FolderWatcher() { Stop(); }

void FolderWatcher::Start() {
    if (thread_.joinable()) return;
    // The worker owns inotifyFd_ exclusively; it creates it in Run() so the main
    // thread never races on it. We only need wakeFd_ here to signal the worker.
    wakeFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (wakeFd_ == -1) {
        TraceLog(LOG_WARNING, "WATCHER: eventfd failed; file watching disabled");
        return;
    }
    stop_ = false;
    thread_ = std::thread(&FolderWatcher::Run, this);
}

void FolderWatcher::Stop() {
    // Always join a started worker regardless of whether inotify ever came up,
    // otherwise destroying a joinable std::thread calls std::terminate.
    if (thread_.joinable()) {
        stop_ = true;
        const uint64_t one = 1;
        [[maybe_unused]] ssize_t n = write(wakeFd_, &one, sizeof(one));
        thread_.join();
    }
    if (wakeFd_ != -1) {
        close(wakeFd_);
        wakeFd_ = -1;
    }
}

void FolderWatcher::SetFolders(const std::vector<std::string>& folders) {
    // Store the folders even if the worker never started or inotify is down, so
    // they're picked up if watching later comes up; don't gate on fd state.
    {
        std::lock_guard<std::mutex> lock(foldersMutex_);
        folders_ = folders;
    }
    foldersDirty_ = true;
    if (wakeFd_ != -1) {
        const uint64_t one = 1;
        [[maybe_unused]] ssize_t n = write(wakeFd_, &one, sizeof(one));
    }
}

void FolderWatcher::AddWatchRecursive(const std::string& root) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return;
    inotify_add_watch(inotifyFd_, root.c_str(), kWatchMask);
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return;
    for (const auto& entry : it) {
        if (entry.is_directory(ec)) {
            inotify_add_watch(inotifyFd_, entry.path().c_str(), kWatchMask);
        }
    }
    // inotify keys watches by inode, so it dedups re-adds for us; we don't need
    // to track watch descriptors except to know a directory tree is covered.
}

void FolderWatcher::RebuildWatches() {
    // Drop every existing watch by recreating the inotify instance — simpler and
    // race-free versus removing descriptors one by one.
    close(inotifyFd_);
    inotifyFd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd_ == -1) return;
    std::vector<std::string> folders;
    {
        std::lock_guard<std::mutex> lock(foldersMutex_);
        folders = folders_;
    }
    for (const auto& f : folders) AddWatchRecursive(f);
}

void FolderWatcher::Run() {
    using clock = std::chrono::steady_clock;
    bool pending = false;
    clock::time_point lastEvent;
    std::vector<char> buf(64 * 1024);

    // inotifyFd_ is created and owned solely by this worker thread.
    inotifyFd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd_ == -1) {
        TraceLog(LOG_WARNING, "WATCHER: inotify_init1 failed; file watching disabled");
        return;  // Stop() still joins us; the joinable thread is handled there.
    }

    while (!stop_.load()) {
        if (foldersDirty_.exchange(false)) {
            RebuildWatches();
            if (inotifyFd_ == -1) break;
        }

        int timeout = -1;  // block until something happens
        if (pending) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     clock::now() - lastEvent)
                                     .count();
            timeout = static_cast<int>(std::max<long long>(0, kDebounceMs - elapsed));
        }

        pollfd fds[2] = {{wakeFd_, POLLIN, 0}, {inotifyFd_, POLLIN, 0}};
        const int ready = poll(fds, 2, timeout);
        if (stop_.load()) break;

        if (ready > 0 && (fds[0].revents & POLLIN)) {
            uint64_t drain;
            while (read(wakeFd_, &drain, sizeof(drain)) > 0) {
            }
            continue;  // re-check stop_ / foldersDirty_ at the top
        }

        if (ready > 0 && (fds[1].revents & POLLIN)) {
            ssize_t len;
            while ((len = read(inotifyFd_, buf.data(), buf.size())) > 0) {
                for (char* p = buf.data(); p < buf.data() + len;) {
                    auto* ev = reinterpret_cast<inotify_event*>(p);
                    const bool isDir = (ev->mask & IN_ISDIR) != 0;
                    const char* name = ev->len > 0 ? ev->name : nullptr;

                    // A new/moved-in directory: start watching it (and its tree)
                    // so files dropped inside are caught too.
                    if (isDir && (ev->mask & (IN_CREATE | IN_MOVED_TO)) && name != nullptr) {
                        // The event has no full path; we can't reconstruct it
                        // from the descriptor cheaply, so flag a rescan and let
                        // a fresh full rebuild re-cover the tree.
                        foldersDirty_ = true;
                    }

                    const bool relevant = isDir || HasAudioExt(name) ||
                                          (ev->mask & (IN_DELETE_SELF | IN_MOVE_SELF)) != 0;
                    if (relevant) {
                        pending = true;
                        lastEvent = clock::now();
                    }
                    p += sizeof(inotify_event) + ev->len;
                }
            }
        }

        // Debounce window elapsed with no fresh events: fire the rescan.
        if (pending) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     clock::now() - lastEvent)
                                     .count();
            if (elapsed >= kDebounceMs) {
                pending = false;
                dirty_ = true;
                glfwPostEmptyEvent();  // wake the idle main loop
            }
        }
    }

    // The worker owns inotifyFd_, so it closes it here on exit (-1 if a rebuild
    // failed and already closed it).
    if (inotifyFd_ != -1) {
        close(inotifyFd_);
        inotifyFd_ = -1;
    }
}

#else  // not Linux: file watching is a no-op stub

FolderWatcher::~FolderWatcher() = default;
void FolderWatcher::Start() {}
void FolderWatcher::Stop() {}
void FolderWatcher::SetFolders(const std::vector<std::string>&) {}

#endif
