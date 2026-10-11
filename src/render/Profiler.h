// Profiler.h - per-frame CPU and GPU timings, for the F3 overlay.
//
// CPU: named sections timed with std::chrono, in the order they happen.
// GPU: OpenGL timestamp queries at the start of each render pass. The GPU
// runs a frame or two behind the CPU, so results are read back a few frames
// later (never waiting on them, which would stall the pipeline).
//
// Every section keeps the last 120 frames, so the overlay can show both the
// average cost and the worst spike: stutter shows up as a high maximum even
// when the average looks fine.
#pragma once

#include <glad/gl.h>

#include <array>
#include <chrono>
#include <string>
#include <vector>

namespace sea {

class Profiler {
public:
    static constexpr int kHistory = 120;   // frames kept per section
    static constexpr int kGraph   = 240;   // frames in the frame-time graph

    struct Section {
        std::string name;
        std::array<float, kHistory> ms{};  // ring buffer, one entry per frame
        int lastFrame = -1;                // a section can run more than once a frame
        float avg() const;
        float max() const;
    };

    Profiler();
    ~Profiler();
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // Call once at the very start of each frame.
    void beginFrame();

    // CPU sections run back to back: begin() closes the previous one.
    void cpu(const char* name);
    void cpuEnd();

    // GPU sections likewise: a timestamp marks where each pass starts.
    void gpu(const char* name);
    void gpuEnd();

    const std::vector<Section>& cpuSections() const { return cpu_; }
    const std::vector<Section>& gpuSections() const { return gpu_; }
    float cpuTotal() const { return cpuTotal_; }
    float gpuTotal() const { return gpuTotal_; }
    bool  gpuSupported() const { return gpuOk_; }

    // Whole frame (vsync wait included), oldest first.
    const std::array<float, kGraph>& frameTimes() const { return frames_; }
    int   frameIndex() const { return graphPos_; }
    float frameAvg() const { return frameAvg_; }
    float frameMax() const;

private:
    using Clock = std::chrono::steady_clock;
    static void record(std::vector<Section>& list, const std::string& name, float ms, int frame);
    void harvestGpu();

    // CPU
    std::vector<Section> cpu_;
    std::string cpuOpen_;
    Clock::time_point cpuStart_, frameStart_;
    bool started_ = false;
    float cpuTotal_ = 0.0f, cpuSum_ = 0.0f;
    int frame_ = 0;

    // GPU: kSlots frames in flight, each with up to kMarks timestamps.
    static constexpr int kSlots = 4, kMarks = 16;
    struct Slot {
        GLuint queries[kMarks] = {};
        std::string names[kMarks];
        int count = 0;
        bool pending = false;
    };
    Slot slots_[kSlots];
    std::vector<Section> gpu_;
    float gpuTotal_ = 0.0f;
    bool gpuOk_ = true;

    std::array<float, kGraph> frames_{};
    int graphPos_ = 0;
    float frameAvg_ = 0.0f;
};

} // namespace sea
