#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Watches the library folders for on-disk changes and flags when a rescan is
// warranted, so the library stays in sync without a manual refresh. On Linux
// this is inotify on a worker thread (zero cost until the filesystem moves);
// elsewhere it compiles to a no-op stub. Events are debounced so a bulk copy
// triggers a single rescan rather than one per file.
//
// The worker pokes the GLFW event loop (glfwPostEmptyEvent) when it fires, so
// the idle, event-waiting main loop wakes up and notices via Poll().
class FolderWatcher {
public:
    ~FolderWatcher();

    void Start();
    void Stop();
    // (Re)establish watches for this exact set of folders. Cheap to call on any
    // folder add/remove; the worker rebuilds its watch set off the main thread.
    void SetFolders(const std::vector<std::string>& folders);
    // True once per detected (debounced) change; clears the flag. Poll each frame.
    bool Poll() { return dirty_.exchange(false); }

private:
#if defined(__linux__)
    void Run();
    void RebuildWatches();
    void AddWatchRecursive(const std::string& root);

    std::thread thread_;
    int inotifyFd_ = -1;  // worker-owned: created/closed only on the worker thread
    int wakeFd_ = -1;  // eventfd: main thread -> worker (folders changed / stop)
    std::atomic<bool> stop_{false};
    std::atomic<bool> foldersDirty_{false};
    std::mutex foldersMutex_;
    std::vector<std::string> folders_;
#endif
    std::atomic<bool> dirty_{false};  // change detected, awaiting Poll()
};
