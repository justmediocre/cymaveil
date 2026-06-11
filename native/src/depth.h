#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

// Generates foreground masks for album art with Depth Anything v2 small
// (q8 ONNX, same model the web app uses) + the maskpipe post-processing.
// Everything heavy runs on one worker thread: model download (first use,
// ~25 MB via curl), ONNX Runtime inference, and mask post-processing.
// Finished masks land on disk as 256x256 grayscale PNGs and are reported
// through PollResult() for the UI to pick up.
class DepthEngine {
public:
    enum class Status { Idle, DownloadingModel, LoadingModel, Processing, Unavailable };

    struct Result {
        std::string albumId;
        bool ok = false;
    };

    DepthEngine();  // out-of-line: OrtState is incomplete here (pimpl)
    ~DepthEngine();

    static std::string MaskPath(const std::string& albumId);
    static bool MaskExists(const std::string& albumId);

    // Queues mask generation (deduped). Safe to call every frame.
    void Request(const std::string& albumId, const std::string& artPath);
    bool PollResult(Result* out);
    Status GetStatus() const { return status_.load(); }
    const char* StatusText() const;
    bool Busy() const;

private:
    struct Job {
        std::string albumId;
        std::string artPath;
    };

    void EnsureWorker();
    void Worker();
    bool EnsureModel();
    bool GenerateMask(const Job& job);

    std::thread thread_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    std::vector<Result> results_;
    std::unordered_set<std::string> attempted_;  // don't retry failures this session
    std::atomic<Status> status_{Status::Idle};
    std::atomic<bool> quit_{false};

    struct OrtState;
    std::unique_ptr<OrtState> ort_;  // worker-thread only
};
