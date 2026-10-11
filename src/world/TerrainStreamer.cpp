#include "TerrainStreamer.h"

#include "Frustum.h"
#include "Shader.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

namespace sea {

namespace {

// Chunks whose highest point is this far under water are not drawn at all:
// the water shader already renders deep water as opaque.
constexpr float kDeepCutoff = -20.0f;

int cellsForLod(int lod) { return kChunkCells >> lod; }

} // namespace

TerrainStreamer::TerrainStreamer(const IslandGenerator& terrain, int viewRadiusChunks)
    : terrain_(terrain), viewRadius_(viewRadiusChunks)
{
    // Leave a core for the main (render) thread.
    unsigned hw = std::thread::hardware_concurrency();
    unsigned n = hw > 2 ? hw - 1 : 2;
    if (n > 8) n = 8;
    for (unsigned i = 0; i < n; ++i) workers_.emplace_back(&TerrainStreamer::workerLoop, this);
}

TerrainStreamer::~TerrainStreamer()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stop_ = true;
        queue_.clear();
    }
    queueCv_.notify_all();
    for (auto& t : workers_) t.join();
    for (auto& kv : chunks_) freeGpu(kv.second);
}

int TerrainStreamer::lodForDistance(double d) const
{
    // Ring boundaries in chunks. Each ring halves the vertex density.
    const double c = d / kChunkSize;
    if (c < 1.6)  return 0;   //   1 m cells right around the boat
    if (c < 3.5)  return 1;   //   2 m
    if (c < 7.0)  return 2;   //   4 m
    if (c < 12.0) return 3;   //   8 m
    if (c < 20.0) return 4;   //  16 m
    return 5;                 //  32 m out to the fog
}

// ------------------------------------------------------------- planning ---

void TerrainStreamer::update(double camX, double camY, double camZ)
{
    // Re-plan only after moving a quarter chunk (or climbing/descending a
    // lot); the LOD rings don't need per-frame precision.
    double moved = std::fabs(camX - lastPlanX_) + std::fabs(camZ - lastPlanZ_) + std::fabs(camY - lastPlanY_);
    if (moved < kChunkSize * 0.25) return;
    lastPlanX_ = camX; lastPlanY_ = camY; lastPlanZ_ = camZ;

    const double alt = std::max(0.0, camY);
    const int64_t ccx = static_cast<int64_t>(std::floor(camX / kChunkSize));
    const int64_t ccz = static_cast<int64_t>(std::floor(camZ / kChunkSize));
    const double maxDist = viewRadius_ * kChunkSize;

    std::vector<Job> jobs;
    {
        // Throw away queued (not yet started) work: the camera has moved, so
        // priorities changed and some of it is no longer needed at all.
        std::lock_guard<std::mutex> lock(queueMutex_);
        for (const Job& j : queue_) {
            auto it = chunks_.find(j.key);
            if (it != chunks_.end() && it->second.pendingLod == j.lod) it->second.pendingLod = -1;
        }
        queue_.clear();
    }

    for (auto& kv : chunks_) kv.second.desiredLod = -1;

    for (int64_t dz = -viewRadius_; dz <= viewRadius_; ++dz)
        for (int64_t dx = -viewRadius_; dx <= viewRadius_; ++dx) {
            ChunkKey key{ccx + dx, ccz + dz};
            double centreX = (static_cast<double>(key.x) + 0.5) * kChunkSize;
            double centreZ = (static_cast<double>(key.z) + 0.5) * kChunkSize;
            double horiz = std::hypot(centreX - camX, centreZ - camZ);
            if (horiz > maxDist) continue; // circular view area

            // Altitude counts toward distance: from high up, nothing is close.
            double dist = std::hypot(horiz, alt);
            int lod = lodForDistance(dist);

            Chunk& c = chunks_[key];
            c.desiredLod = lod;
            if (c.lod == lod || c.pendingLod == lod) continue;

            // Holes first (chunks with nothing on screen), then nearest first.
            float priority = static_cast<float>(horiz) - (c.lod < 0 ? 1e7f : 0.0f);
            jobs.push_back({key, lod, priority});
            c.pendingLod = lod;
        }

    std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) { return a.priority < b.priority; });
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.assign(jobs.begin(), jobs.end());
    }
    queueCv_.notify_all();

    // Unload chunks well outside the view (a margin avoids load/unload
    // flicker when hovering at the boundary).
    const double unloadDist = maxDist + 2.0 * kChunkSize;
    for (auto it = chunks_.begin(); it != chunks_.end();) {
        double centreX = (static_cast<double>(it->first.x) + 0.5) * kChunkSize;
        double centreZ = (static_cast<double>(it->first.z) + 0.5) * kChunkSize;
        if (it->second.desiredLod < 0 && std::hypot(centreX - camX, centreZ - camZ) > unloadDist) {
            freeGpu(it->second);
            it = chunks_.erase(it);
        } else {
            ++it;
        }
    }
}

// --------------------------------------------------------------- workers --

void TerrainStreamer::workerLoop()
{
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            queueCv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = queue_.front();
            queue_.pop_front();
            ++building_; // inside the lock, so idle() never sees a gap
        }
        Result r = buildChunk(job.key, job.lod);
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            results_.push_back(std::move(r));
        }
        --building_;
    }
}

TerrainStreamer::Result TerrainStreamer::buildChunk(const ChunkKey& key, int lod) const
{
    const int    n    = cellsForLod(lod);
    const float  step = static_cast<float>(kChunkSize / n);
    const double wx0  = static_cast<double>(key.x) * kChunkSize;
    const double wz0  = static_cast<double>(key.z) * kChunkSize;

    // Heights on an (n+1)^2 vertex grid plus a one-sample border, so normals
    // at the chunk edge use real neighbours (the same ones the adjacent chunk
    // uses) and lighting is continuous across chunk seams.
    const int g = n + 3;
    std::vector<float> h(static_cast<size_t>(g) * g), moist(static_cast<size_t>(g) * g);
    for (int j = 0; j < g; ++j)
        for (int i = 0; i < g; ++i) {
            TerrainSample s = terrain_.sample(wx0 + (i - 1) * static_cast<double>(step),
                                              wz0 + (j - 1) * static_cast<double>(step));
            h[j * g + i] = s.height;
            moist[j * g + i] = s.moisture;
        }
    auto H = [&](int i, int j) { return h[(j + 1) * g + (i + 1)]; }; // i, j in [-1, n+1]

    Result r;
    r.key = key;
    r.lod = lod;
    r.minH = 1e9f;
    r.maxH = -1e9f;
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            r.minH = std::min(r.minH, H(i, j));
            r.maxH = std::max(r.maxH, H(i, j));
        }
    r.empty = r.maxH < kDeepCutoff;
    if (r.empty) return r;

    // Trees and rocks, placed on this mesh's own triangles.
    if (lod <= kPropMaxLod) {
        HeightGridView view{h.data(), moist.data(), g, n, step};
        scatterProps(view, wx0, wz0, kChunkSize, terrain_.seed(), r.props);
    }

    // Vertices: central differences give the slope, the normal is
    // (-dh/dx, 1, -dh/dz) normalised.
    const int vn = n + 1;
    r.verts.reserve(static_cast<size_t>(vn) * vn + 4 * vn);
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i) {
            float dhdx = (H(i + 1, j) - H(i - 1, j)) / (2.0f * step);
            float dhdz = (H(i, j + 1) - H(i, j - 1)) / (2.0f * step);
            Vec3 nrm = normalize(Vec3{-dhdx, 1.0f, -dhdz});
            r.verts.push_back({i * step, H(i, j), j * step, nrm.x, nrm.y, nrm.z, moist[(j + 1) * g + (i + 1)]});
        }

    // Two triangles per cell, counter-clockwise seen from above.
    auto idx = [&](int i, int j) { return static_cast<uint16_t>(j * vn + i); };
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            uint16_t v00 = idx(i, j), v10 = idx(i + 1, j), v01 = idx(i, j + 1), v11 = idx(i + 1, j + 1);
            r.indices.insert(r.indices.end(), {v00, v01, v10, v10, v01, v11});
        }

    // Skirts: a short wall hanging down from every edge. Where a chunk meets
    // a neighbour at a different LOD their edges don't match exactly; the
    // skirt fills the crack so you never see through the ground.
    const float skirt = step * 2.0f + 2.0f;
    auto addSkirt = [&](int i0, int j0, int di, int dj) {
        uint16_t prevTop = 0, prevBottom = 0;
        for (int k = 0; k <= n; ++k) {
            int i = i0 + di * k, j = j0 + dj * k;
            uint16_t top = idx(i, j);
            ChunkVertex v = r.verts[top];
            v.y -= skirt;
            uint16_t bottom = static_cast<uint16_t>(r.verts.size());
            r.verts.push_back(v);
            if (k > 0) r.indices.insert(r.indices.end(), {prevTop, prevBottom, top, top, prevBottom, bottom});
            prevTop = top;
            prevBottom = bottom;
        }
    };
    addSkirt(0, 0, 1, 0);  // z = 0 edge
    addSkirt(0, n, 1, 0);  // z = max edge
    addSkirt(0, 0, 0, 1);  // x = 0 edge
    addSkirt(n, 0, 0, 1);  // x = max edge
    r.minH -= skirt;
    return r;
}

// ------------------------------------------------------------ main thread --

void TerrainStreamer::freeGpu(Chunk& c)
{
    if (c.vao) {
        glDeleteVertexArrays(1, &c.vao);
        glDeleteBuffers(1, &c.vbo);
        glDeleteBuffers(1, &c.ebo);
    }
    c.vao = c.vbo = c.ebo = 0;
    c.indexCount = 0;
}

void TerrainStreamer::uploadFinished(double budgetMs)
{
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();

    std::vector<Result> batch;
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        batch.swap(results_);
    }

    size_t i = 0;
    for (; i < batch.size(); ++i) {
        if (std::chrono::duration<double, std::milli>(clock::now() - start).count() > budgetMs) break;

        Result& r = batch[i];
        auto it = chunks_.find(r.key);
        if (it == chunks_.end()) continue;  // unloaded while it was being built
        Chunk& c = it->second;
        if (c.pendingLod == r.lod) c.pendingLod = -1;

        // Keep it if it's what we want now, or if the chunk has nothing at
        // all on screen yet (any LOD beats a hole).
        if (r.lod != c.desiredLod && c.lod >= 0) continue;

        c.lod   = r.lod;
        c.empty = r.empty;
        c.minH  = r.minH;
        c.maxH  = r.maxH;
        for (int t = 0; t < kPropTypes; ++t) c.props[t] = std::move(r.props[t]);
        if (r.empty) {
            freeGpu(c);
            continue;
        }

        if (!c.vao) {
            glGenVertexArrays(1, &c.vao);
            glGenBuffers(1, &c.vbo);
            glGenBuffers(1, &c.ebo);
        }
        glBindVertexArray(c.vao);
        glBindBuffer(GL_ARRAY_BUFFER, c.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(r.verts.size() * sizeof(ChunkVertex)), r.verts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, c.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(r.indices.size() * sizeof(uint16_t)), r.indices.data(), GL_STATIC_DRAW);
        const GLsizei stride = sizeof(ChunkVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, x)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, nx)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(ChunkVertex, moisture)));
        glBindVertexArray(0);
        c.indexCount = static_cast<GLsizei>(r.indices.size());
    }

    // Out of time: put the rest back for next frame.
    if (i < batch.size()) {
        std::lock_guard<std::mutex> lock(resultMutex_);
        results_.insert(results_.end(), std::make_move_iterator(batch.begin() + static_cast<std::ptrdiff_t>(i)),
                        std::make_move_iterator(batch.end()));
    }
}

void TerrainStreamer::draw(const Shader& shader, const Frustum& frustum, double originX, double originZ)
{
    stats_ = {};
    for (auto& kv : chunks_) {
        Chunk& c = kv.second;
        if (c.lod >= 0) ++stats_.loaded;
        if (!c.vao || c.empty) continue;

        // World -> camera-relative, done in double before dropping to float.
        Vec3 offset{static_cast<float>(static_cast<double>(kv.first.x) * kChunkSize - originX), 0.0f,
                    static_cast<float>(static_cast<double>(kv.first.z) * kChunkSize - originZ)};
        Vec3 mn{offset.x, c.minH, offset.z};
        Vec3 mx{offset.x + static_cast<float>(kChunkSize), c.maxH, offset.z + static_cast<float>(kChunkSize)};
        if (!frustum.intersectsBox(mn, mx)) continue;

        shader.set("uChunkOffset", offset);
        glBindVertexArray(c.vao);
        glDrawElements(GL_TRIANGLES, c.indexCount, GL_UNSIGNED_SHORT, nullptr);
        ++stats_.drawn;
        stats_.triangles += static_cast<size_t>(c.indexCount) / 3;
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stats_.queued = static_cast<int>(queue_.size());
    }
    stats_.building = building_.load();
}

void TerrainStreamer::gatherProps(const Frustum& frustum, double originX, double originZ, const Vec3& camLocal,
                                  float maxDistance, PropLists& out)
{
    for (auto& list : out) list.clear();
    size_t total = 0;
    const float half = static_cast<float>(kChunkSize) * 0.5f;
    for (auto& kv : chunks_) {
        Chunk& c = kv.second;
        if (c.empty || c.lod < 0 || c.lod > kPropMaxLod) continue;
        bool any = false;
        for (const auto& list : c.props) any = any || !list.empty();
        if (!any) continue;

        Vec3 offset{static_cast<float>(static_cast<double>(kv.first.x) * kChunkSize - originX), 0.0f,
                    static_cast<float>(static_cast<double>(kv.first.z) * kChunkSize - originZ)};
        float dx = offset.x + half - camLocal.x, dz = offset.z + half - camLocal.z;
        if (std::sqrt(dx * dx + dz * dz) > maxDistance + half * 1.42f) continue;
        // Trees stand up to ~25 m above the ground they grow on.
        Vec3 mn{offset.x, c.minH, offset.z};
        Vec3 mx{offset.x + static_cast<float>(kChunkSize), c.maxH + 25.0f, offset.z + static_cast<float>(kChunkSize)};
        if (!frustum.intersectsBox(mn, mx)) continue;

        for (int t = 0; t < kPropTypes; ++t)
            for (PropInstance p : c.props[t]) {
                p.x += offset.x;
                p.z += offset.z;
                out[t].push_back(p);
                ++total;
            }
    }
    stats_.props = total;
}

bool TerrainStreamer::idle()
{
    std::lock_guard<std::mutex> lq(queueMutex_);
    std::lock_guard<std::mutex> lr(resultMutex_);
    return queue_.empty() && results_.empty() && building_.load() == 0;
}

} // namespace sea
