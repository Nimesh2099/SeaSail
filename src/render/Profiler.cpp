#include "Profiler.h"

#include <algorithm>

namespace sea {

float Profiler::Section::max() const { return *std::max_element(ms.begin(), ms.end()); }

float Profiler::Section::avg() const
{
    float sum = 0.0f;
    for (float v : ms) sum += v;
    return sum / static_cast<float>(kHistory);
}

Profiler::Profiler()
{
    for (Slot& s : slots_) glGenQueries(kMarks, s.queries);
    // GL_TIMESTAMP is core in 3.3, but a driver may report 0 counter bits.
    GLint bits = 0;
    glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
    gpuOk_ = bits > 0;
}

Profiler::~Profiler()
{
    for (Slot& s : slots_) glDeleteQueries(kMarks, s.queries);
}

void Profiler::record(std::vector<Section>& list, const std::string& name, float ms, int frame)
{
    auto it = std::find_if(list.begin(), list.end(), [&](const Section& s) { return s.name == name; });
    if (it == list.end()) {
        list.push_back(Section{name, {}, -1});
        it = list.end() - 1;
    }
    float& slot = it->ms[static_cast<size_t>(frame % kHistory)];
    slot = it->lastFrame == frame ? slot + ms : ms;  // add up repeats within a frame
    it->lastFrame = frame;
}

float Profiler::frameMax() const { return *std::max_element(frames_.begin(), frames_.end()); }

void Profiler::beginFrame()
{
    const auto now = Clock::now();
    if (started_) {
        float ms = std::chrono::duration<float, std::milli>(now - frameStart_).count();
        frames_[static_cast<size_t>(graphPos_)] = ms;
        graphPos_ = (graphPos_ + 1) % kGraph;
        frameAvg_ += (ms - frameAvg_) * 0.05f;
        cpuTotal_ += (cpuSum_ - cpuTotal_) * 0.05f;
    }
    started_ = true;
    frameStart_ = now;
    cpuSum_ = 0.0f;
    cpuOpen_.clear();
    ++frame_;
    if (gpuOk_) harvestGpu();
}

void Profiler::cpu(const char* name)
{
    cpuEnd();
    cpuOpen_ = name;
    cpuStart_ = Clock::now();
}

void Profiler::cpuEnd()
{
    if (cpuOpen_.empty()) return;
    float ms = std::chrono::duration<float, std::milli>(Clock::now() - cpuStart_).count();
    record(cpu_, cpuOpen_, ms, frame_);
    cpuSum_ += ms;
    cpuOpen_.clear();
}

void Profiler::gpu(const char* name)
{
    if (!gpuOk_) return;
    Slot& s = slots_[frame_ % kSlots];
    if (s.count >= kMarks - 1) return;  // keep one for gpuEnd
    s.names[s.count] = name;
    glQueryCounter(s.queries[s.count++], GL_TIMESTAMP);
}

void Profiler::gpuEnd()
{
    if (!gpuOk_) return;
    Slot& s = slots_[frame_ % kSlots];
    if (s.count == 0) return;
    s.names[s.count] = "";
    glQueryCounter(s.queries[s.count++], GL_TIMESTAMP);
    s.pending = true;
}

void Profiler::harvestGpu()
{
    // The slot we're about to reuse holds the frame from kSlots frames ago.
    Slot& s = slots_[frame_ % kSlots];
    if (s.pending && s.count >= 2) {
        GLint ready = 0;
        glGetQueryObjectiv(s.queries[s.count - 1], GL_QUERY_RESULT_AVAILABLE, &ready);
        if (ready) {
            GLuint64 prev = 0, first = 0, t = 0;
            for (int i = 0; i < s.count; ++i) {
                glGetQueryObjectui64v(s.queries[i], GL_QUERY_RESULT, &t);
                if (i == 0) first = t;
                else record(gpu_, s.names[i - 1], static_cast<float>(t - prev) / 1e6f, frame_);
                prev = t;
            }
            gpuTotal_ += (static_cast<float>(prev - first) / 1e6f - gpuTotal_) * 0.05f;
        }
        // Not ready after kSlots frames: skip it rather than stall.
    }
    s.count = 0;
    s.pending = false;
}

} // namespace sea
