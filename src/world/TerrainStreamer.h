// TerrainStreamer.h - streams terrain chunks in and out around the player.
//
//  * The world is an endless grid of square chunks (kChunkSize metres).
//  * Each chunk gets a level of detail from its distance to the camera:
//    LOD 0 = 64x64 cells (2 m apart) up close, down to 4x4 cells far away.
//  * Meshes are built on worker threads (pure CPU work: noise + normals) and
//    uploaded to the GPU on the main thread, a few per frame, so streaming
//    never causes a hitch.
//  * Chunk positions are kept in world coordinates (int64 chunk index) and
//    converted to camera-relative floats only when drawing ("floating origin").
#pragma once

#include "IslandGenerator.h"
#include "Props.h"
#include "SeaMath.h"

#include <glad/gl.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sea {

class Shader;
class Frustum;

constexpr double kChunkSize  = 128.0;  // metres
constexpr int    kChunkCells = 128;    // cells per side at LOD 0 (1 m apart)
constexpr int    kMaxLod     = 5;      // LOD 5 = 128 >> 5 = 4 cells per side
constexpr int    kPropMaxLod = 3;      // trees and rocks only on chunks this detailed (~1.5 km)

struct ChunkKey {
    int64_t x = 0, z = 0;
    bool operator==(const ChunkKey& o) const { return x == o.x && z == o.z; }
};

struct ChunkKeyHash {
    size_t operator()(const ChunkKey& k) const
    {
        uint64_t h = static_cast<uint64_t>(k.x) * 0x9E3779B97F4A7C15ULL ^ static_cast<uint64_t>(k.z) * 0xC2B2AE3D27D4EB4FULL;
        return static_cast<size_t>(h ^ (h >> 31));
    }
};

struct ChunkVertex {
    float x, y, z;     // position relative to the chunk's corner
    float nx, ny, nz;  // normal
    float moisture;
};

struct ChunkStats {
    int loaded = 0, drawn = 0, queued = 0, building = 0;
    size_t triangles = 0;
    size_t props = 0;
};

class TerrainStreamer {
public:
    TerrainStreamer(const IslandGenerator& terrain, int viewRadiusChunks);
    ~TerrainStreamer();
    TerrainStreamer(const TerrainStreamer&) = delete;
    TerrainStreamer& operator=(const TerrainStreamer&) = delete;

    // Decide which chunks should exist at which LOD and queue the work.
    // camX/camZ are absolute world coordinates; camY is height above sea level.
    void update(double camX, double camY, double camZ);

    // Upload finished meshes, spending at most `budgetMs` of this frame.
    void uploadFinished(double budgetMs);

    // Draw all loaded chunks that touch the frustum. originX/Z is the world
    // position that local (camera-space) coordinates are measured from.
    void draw(const Shader& shader, const Frustum& frustum, double originX, double originZ, const Vec3& camLocal);

    // Collect the props of every visible nearby chunk into `out`, with
    // positions relative to the floating origin.
    void gatherProps(const Frustum& frustum, double originX, double originZ, const Vec3& camLocal,
                     float maxDistance, PropLists& out);

    // True when nothing is queued, building or waiting for upload.
    bool idle();

    const ChunkStats& stats() const { return stats_; }
    float viewDistance() const { return static_cast<float>(viewRadius_ * kChunkSize); }

private:
    struct Chunk {
        int     lod = -1;          // LOD currently on the GPU (-1 = none yet)
        int     pendingLod = -1;   // LOD queued or being built (-1 = none)
        int     desiredLod = -1;   // LOD the latest plan asked for
        bool    empty = false;     // deep ocean floor only: nothing to draw
        float   minH = 0, maxH = 0;
        GLuint  vao = 0, vbo = 0, ebo = 0;
        GLsizei indexCount = 0;
        PropLists props;           // chunk-local positions
    };

    struct Job {
        ChunkKey key;
        int      lod;
        float    priority;  // lower = sooner
    };

    struct Result {
        ChunkKey key;
        int      lod;
        bool     empty;
        float    minH, maxH;
        std::vector<ChunkVertex> verts;
        std::vector<uint16_t>    indices;
        PropLists                props;
    };

    int  lodForDistance(double metres) const;
    void workerLoop();
    Result buildChunk(const ChunkKey& key, int lod) const;
    static void freeGpu(Chunk& c);

    const IslandGenerator& terrain_;
    int viewRadius_;
    std::unordered_map<ChunkKey, Chunk, ChunkKeyHash> chunks_;

    // Planning happens only when the camera has moved far enough.
    double lastPlanX_ = 1e300, lastPlanY_ = 1e300, lastPlanZ_ = 1e300;

    // Work queue (main thread -> workers) and results (workers -> main thread).
    std::mutex              queueMutex_;
    std::condition_variable queueCv_;
    std::deque<Job>         queue_;
    std::mutex              resultMutex_;
    std::vector<Result>     results_;
    std::atomic<int>        building_{0};
    std::atomic<bool>       stop_{false};
    std::vector<std::thread> workers_;

    ChunkStats stats_;

    struct Visible {
        float  dist2;
        Vec3   offset;
        Chunk* chunk;
    };
    std::vector<Visible> visible_;  // reused every frame by draw()
};

} // namespace sea
