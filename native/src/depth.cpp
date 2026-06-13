#include "depth.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include <onnxruntime_cxx_api.h>

#include "raylib.h"

#include "maskpipe.h"
#include "netfetch.h"
#include "paths.h"

namespace {

// The web app runs the model at 256; Depth Anything wants multiples of 14.
constexpr int kModelInput = 252;
constexpr int kMaskSize = 256;
const char* kModelUrl =
    "https://huggingface.co/onnx-community/depth-anything-v2-small/resolve/main/onnx/"
    "model_quantized.onnx";

std::string ModelPath() {
    const std::string dir = paths::DataDir() + "/models";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir + "/depth-anything-v2-small-q8.onnx";
}

std::string MaskDir() {
    const std::string dir = paths::DataDir() + "/masks";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

// Loads art as RGBA stretched to size x size (the web pipeline stretches too;
// album art is square in practice).
bool LoadArtRgba(const std::string& path, int size, std::vector<unsigned char>& out) {
    Image img = LoadImage(path.c_str());
    if (img.data == nullptr) return false;
    ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    ImageResize(&img, size, size);
    const auto* px = static_cast<const unsigned char*>(img.data);
    out.assign(px, px + static_cast<size_t>(size) * size * 4);
    UnloadImage(img);
    return true;
}

}  // namespace

struct DepthEngine::OrtState {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "cymaveil"};
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::string outputName;
};

DepthEngine::DepthEngine() = default;

DepthEngine::~DepthEngine() {
    quit_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

std::string DepthEngine::MaskPath(const std::string& albumId) {
    return MaskDir() + "/" + albumId + ".png";
}

bool DepthEngine::MaskExists(const std::string& albumId) {
    std::error_code ec;
    return std::filesystem::exists(MaskPath(albumId), ec);
}

const char* DepthEngine::StatusText() const {
    switch (status_.load()) {
        case Status::Idle: return "idle";
        case Status::DownloadingModel: return "downloading model";
        case Status::LoadingModel: return "loading model";
        case Status::Processing: return "processing";
        case Status::Unavailable: return "unavailable";
    }
    return "?";
}

bool DepthEngine::Busy() const {
    if (status_.load() != Status::Idle && status_.load() != Status::Unavailable) return true;
    std::lock_guard<std::mutex> lock(mu_);
    return !jobs_.empty() || !results_.empty();
}

void DepthEngine::Request(const std::string& albumId, const std::string& artPath) {
    if (status_.load() == Status::Unavailable) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (attempted_.contains(albumId)) return;
        attempted_.insert(albumId);
        jobs_.push_back(Job{albumId, artPath});
    }
    EnsureWorker();
    cv_.notify_one();
}

bool DepthEngine::PollResult(Result* out) {
    std::lock_guard<std::mutex> lock(mu_);
    if (results_.empty()) return false;
    *out = results_.back();
    results_.pop_back();
    return true;
}

void DepthEngine::EnsureWorker() {
    if (!thread_.joinable()) thread_ = std::thread(&DepthEngine::Worker, this);
}

void DepthEngine::Worker() {
    while (!quit_) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) break;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        if (!EnsureModel()) {
            status_ = Status::Unavailable;
            std::lock_guard<std::mutex> lock(mu_);
            jobs_.clear();
            continue;
        }

        status_ = Status::Processing;
        const bool ok = GenerateMask(job);
        {
            std::lock_guard<std::mutex> lock(mu_);
            results_.push_back(Result{job.albumId, ok});
            if (jobs_.empty()) {
                // Free ORT memory between batches, like the web app's
                // terminate-worker-per-batch strategy.
                ort_.reset();
                status_ = Status::Idle;
            }
        }
    }
}

bool DepthEngine::EnsureModel() {
    const std::string path = ModelPath();
    std::error_code ec;

    if (!std::filesystem::exists(path, ec)) {
        status_ = Status::DownloadingModel;
        const std::string tmp = path + ".part";
        if (!netfetch::Download(kModelUrl, tmp)) {
            TraceLog(LOG_WARNING, "DEPTH: model download failed");
            std::filesystem::remove(tmp, ec);
            return false;
        }
        std::filesystem::rename(tmp, path, ec);
        if (ec) return false;
        TraceLog(LOG_INFO, "DEPTH: model downloaded to %s", path.c_str());
    }

    if (ort_ == nullptr || ort_->session == nullptr) {
        status_ = Status::LoadingModel;
        try {
            auto state = std::make_unique<OrtState>();
            Ort::SessionOptions opts;
            opts.SetIntraOpNumThreads(2);
            opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            state->session = std::make_unique<Ort::Session>(state->env, path.c_str(), opts);
            Ort::AllocatorWithDefaultOptions alloc;
            state->inputName = state->session->GetInputNameAllocated(0, alloc).get();
            state->outputName = state->session->GetOutputNameAllocated(0, alloc).get();
            ort_ = std::move(state);
        } catch (const Ort::Exception& e) {
            TraceLog(LOG_WARNING, "DEPTH: failed to load model: %s", e.what());
            // A truncated/corrupt download would fail here forever; clear it.
            std::filesystem::remove(path, ec);
            return false;
        }
    }
    return true;
}

bool DepthEngine::GenerateMask(const Job& job) {
    // ── Preprocess: resize, ImageNet-normalize, HWC -> CHW ──
    std::vector<unsigned char> rgba;
    if (!LoadArtRgba(job.artPath, kModelInput, rgba)) return false;

    constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
    constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};
    constexpr int n = kModelInput * kModelInput;
    std::vector<float> input(3 * static_cast<size_t>(n));
    for (int i = 0; i < n; i++) {
        for (int c = 0; c < 3; c++) {
            input[c * n + i] = (rgba[i * 4 + c] / 255.0f - kMean[c]) / kStd[c];
        }
    }

    // ── Inference ──
    std::vector<float> depthRaw;
    int dw = 0, dh = 0;
    try {
        const std::array<int64_t, 4> shape{1, 3, kModelInput, kModelInput};
        auto mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value in = Ort::Value::CreateTensor<float>(mem, input.data(), input.size(),
                                                        shape.data(), shape.size());
        const char* inNames[] = {ort_->inputName.c_str()};
        const char* outNames[] = {ort_->outputName.c_str()};
        auto out = ort_->session->Run(Ort::RunOptions{nullptr}, inNames, &in, 1, outNames, 1);
        const auto dims = out[0].GetTensorTypeAndShapeInfo().GetShape();
        if (dims.size() < 2) return false;
        dh = static_cast<int>(dims[dims.size() - 2]);
        dw = static_cast<int>(dims[dims.size() - 1]);
        const float* data = out[0].GetTensorData<float>();
        depthRaw.assign(data, data + static_cast<size_t>(dw) * dh);
    } catch (const Ort::Exception& e) {
        TraceLog(LOG_WARNING, "DEPTH: inference failed: %s", e.what());
        return false;
    }

    // ── Normalize to 0..255 and resize to the mask resolution ──
    float lo = depthRaw[0], hi = depthRaw[0];
    for (float v : depthRaw) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    const float range = hi - lo > 1e-6f ? hi - lo : 1.0f;
    std::vector<unsigned char> depth8(depthRaw.size());
    for (size_t i = 0; i < depthRaw.size(); i++) {
        depth8[i] = static_cast<unsigned char>(255.0f * (depthRaw[i] - lo) / range);
    }
    Image depthImg{depth8.data(), dw, dh, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
    Image depthCopy = ImageCopy(depthImg);  // ImageResize reallocs; keep our vector intact
    ImageResize(&depthCopy, kMaskSize, kMaskSize);
    const auto* dp = static_cast<const unsigned char*>(depthCopy.data);
    std::vector<unsigned char> depth(dp, dp + kMaskSize * kMaskSize);
    UnloadImage(depthCopy);

    // ── Post-process into the foreground alpha and persist ──
    std::vector<unsigned char> artPx;
    if (!LoadArtRgba(job.artPath, kMaskSize, artPx)) return false;
    std::vector<unsigned char> alpha =
        maskpipe::DepthToMask(depth, artPx.data(), kMaskSize, kMaskSize, true);

    Image maskImg{alpha.data(), kMaskSize, kMaskSize, 1, PIXELFORMAT_UNCOMPRESSED_GRAYSCALE};
    return ExportImage(maskImg, MaskPath(job.albumId).c_str());
}
