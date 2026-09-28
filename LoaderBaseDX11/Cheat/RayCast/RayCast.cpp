#include "RayCast.hpp"
#include "../Cheat.hpp"
#include "../Memory/Memory.hpp"
#include "../EMemory.hpp"
#include <cstring>
#include <cmath>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>
#include <xmmintrin.h>
#include <Windows.h>

namespace PhysX
{
    namespace v7a
    {
        constexpr uint32_t PhysicsWorld              = 0xF3A100;   // NpPhysics* — libunity.so 1.132.1 (dword_F3A100; +0x18 = sceneMap)
        constexpr uint32_t SceneMap                  = 0x18;
        constexpr uint32_t MapEntries                = 0x00;
        constexpr uint32_t MapCapacity               = 0x04;
        constexpr uint32_t EntrySize                 = 12;
        constexpr uint32_t EntryTag                  = 0x00;
        constexpr uint32_t EntryValue                = 0x08;
        constexpr uint32_t PhysXSceneToPxScene       = 0x08;
        constexpr uint32_t PxSceneToQueryManager     = 0x15D0;
        constexpr uint32_t QueryManagerToStaticPruner = 0x00;
        constexpr uint32_t PrunerObjectCount         = 0x11C;
        constexpr uint32_t PrunerWorldBounds         = 0x124;
        constexpr uint32_t PrunerPayloads            = 0x128;
        constexpr uint32_t ShapeCoreGeometryType     = 0x54;
        constexpr uint32_t ShapeCoreLocalPose        = 0x30;
        constexpr uint32_t ShapeCoreHalfExtents      = 0x58;
        constexpr uint32_t RigidCoreGlobalPose       = 0x20;
        constexpr uint32_t FilterDataWord0           = 0x20;
        constexpr uint32_t PayloadByteSize           = 8;
        constexpr uint32_t PayloadElementCount       = 2;
        constexpr uint32_t BoundsFloatCount          = 6;
        constexpr uint32_t BoundsByteSize            = 24;
    }
}

// ─── Shape publication ──────────────────────────────────────────────────────
// A ShapeSet is an immutable snapshot of the world geometry the raycaster
// needs. The worker builds a fresh ShapeSet off-thread and publishes it by
// atomically swapping g_shapes under g_shapesMx. Consumers (Draw at
// Draw.cpp:323 and SilentTargetScanThread at AimModules.cpp:775) grab one
// shared_ptr under the lock in nanoseconds, drop the lock, and iterate their
// local snapshot lock-free with zero RPMs during the ray tests themselves.
namespace {
    struct ShapeSet {
        std::vector<RayCast::OBBShape> orientedBox;
        std::vector<RayCast::AABB>     nonBox;
    };
}

static std::shared_ptr<const ShapeSet> g_shapes;   // guarded by g_shapesMx
static std::mutex                      g_shapesMx;

// g_staticPruner is only touched by the worker thread (and by Reset() while
// the worker is stopped, per the Reset() contract) — no lock needed.
static uint32_t g_staticPruner = 0;

static constexpr int MaxShapeCount = 8192;

// ─── Worker lifecycle ───────────────────────────────────────────────────────
static std::atomic<bool> g_workerRunning{ false };
static std::thread       g_workerThread;
static std::mutex        g_lifecycleMx;
static uint32_t          g_workerIl2cpp = 0;
// Cadence chosen so a full geometry rebuild (which was firing every ~500 ms
// at 120 FPS on the render thread) no longer taxes the render budget. Static
// pruner geometry only mutates on map/instance transitions, so 2 s is plenty.
static constexpr DWORD   kRefreshIntervalMs = 2000;
static constexpr DWORD   kWorkerPollMs      = 50;

static inline bool BadPtr(uint32_t addr)
{
    return addr == 0 || addr < 0x10000 || addr == 0xFFFFFFFF;
}

static uint32_t FindPxScene(uint32_t LibUnity)
{
    uint32_t physicsWorld = g_Memory->Read<uint32_t>(LibUnity + PhysX::v7a::PhysicsWorld);
    if (BadPtr(physicsWorld)) return 0;

    uint32_t sceneMap = g_Memory->Read<uint32_t>(physicsWorld + PhysX::v7a::SceneMap);
    if (BadPtr(sceneMap)) return 0;

    uint32_t entries  = g_Memory->Read<uint32_t>(sceneMap + PhysX::v7a::MapEntries);
    uint32_t capacity = g_Memory->Read<uint32_t>(sceneMap + PhysX::v7a::MapCapacity);
    if (entries == 0 || capacity == 0 || capacity > 1024) return 0;

    for (uint32_t i = 0; i <= capacity; i++)
    {
        uint32_t entryAddress = entries + i * PhysX::v7a::EntrySize;
        uint32_t hashTag      = g_Memory->Read<uint32_t>(entryAddress + PhysX::v7a::EntryTag);
        if (hashTag >= 0xFFFFFFFE) continue;

        uint32_t physXScene = g_Memory->Read<uint32_t>(entryAddress + PhysX::v7a::EntryValue);
        if (BadPtr(physXScene)) continue;

        uint32_t pxScene = g_Memory->Read<uint32_t>(physXScene + PhysX::v7a::PhysXSceneToPxScene);
        if (BadPtr(pxScene)) continue;

        return pxScene;
    }
    return 0;
}

static bool RayIntersectsAABB(Vector3 origin, Vector3 inverseDirection, float maxDistance, const RayCast::AABB& bounds)
{
    __m128 o    = _mm_set_ps(0, origin.Z, origin.Y, origin.X);
    __m128 id   = _mm_set_ps(0, inverseDirection.Z, inverseDirection.Y, inverseDirection.X);
    __m128 bmin = _mm_set_ps(0, bounds.minZ, bounds.minY, bounds.minX);
    __m128 bmax = _mm_set_ps(0, bounds.maxZ, bounds.maxY, bounds.maxX);

    __m128 t1 = _mm_mul_ps(_mm_sub_ps(bmin, o), id);
    __m128 t2 = _mm_mul_ps(_mm_sub_ps(bmax, o), id);

    __m128 tlo = _mm_min_ps(t1, t2);
    __m128 thi = _mm_max_ps(t1, t2);

    alignas(16) float tloArray[4];
    alignas(16) float thiArray[4];
    _mm_store_ps(tloArray, tlo);
    _mm_store_ps(thiArray, thi);

    float tmin = fmaxf(fmaxf(tloArray[0], tloArray[1]), fmaxf(tloArray[2], 0.0f));
    float tmax = fminf(fminf(thiArray[0], thiArray[1]), fminf(thiArray[2], maxDistance));

    return tmin <= tmax;
}

static bool PointInsideAABB(Vector3 point, const RayCast::AABB& bounds)
{
    return point.X >= bounds.minX && point.X <= bounds.maxX
        && point.Y >= bounds.minY && point.Y <= bounds.maxY
        && point.Z >= bounds.minZ && point.Z <= bounds.maxZ;
}

static bool RayIntersectsOBB(Vector3 origin, Vector3 direction, float maxDistance, const RayCast::OBBShape& box)
{
    float dx = origin.X - box.worldPx;
    float dy = origin.Y - box.worldPy;
    float dz = origin.Z - box.worldPz;

    float localOrigin[3] = {
        box.r00 * dx + box.r01 * dy + box.r02 * dz,
        box.r10 * dx + box.r11 * dy + box.r12 * dz,
        box.r20 * dx + box.r21 * dy + box.r22 * dz
    };

    float localDirection[3] = {
        box.r00 * direction.X + box.r01 * direction.Y + box.r02 * direction.Z,
        box.r10 * direction.X + box.r11 * direction.Y + box.r12 * direction.Z,
        box.r20 * direction.X + box.r21 * direction.Y + box.r22 * direction.Z
    };

    float halfExtents[3] = { box.halfX, box.halfY, box.halfZ };

    constexpr float Epsilon = 1e-9f;
    float tNear = 0.0f, tFar = maxDistance;

    for (int axis = 0; axis < 3; axis++)
    {
        if (fabsf(localDirection[axis]) < Epsilon)
        {
            if (localOrigin[axis] < -halfExtents[axis] || localOrigin[axis] > halfExtents[axis])
                return false;
        }
        else
        {
            float inverseLocal = 1.0f / localDirection[axis];
            float t1 = (-halfExtents[axis] - localOrigin[axis]) * inverseLocal;
            float t2 = ( halfExtents[axis] - localOrigin[axis]) * inverseLocal;
            if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
            if (t1 > tNear) tNear = t1;
            if (t2 < tFar)  tFar  = t2;
            if (tNear > tFar) return false;
        }
    }
    return true;
}

static bool PointInsideOBB(Vector3 point, const RayCast::OBBShape& box)
{
    float dx = point.X - box.worldPx;
    float dy = point.Y - box.worldPy;
    float dz = point.Z - box.worldPz;

    float localX = box.r00 * dx + box.r01 * dy + box.r02 * dz;
    float localY = box.r10 * dx + box.r11 * dy + box.r12 * dz;
    float localZ = box.r20 * dx + box.r21 * dy + box.r22 * dz;

    return fabsf(localX) <= box.halfX
        && fabsf(localY) <= box.halfY
        && fabsf(localZ) <= box.halfZ;
}

bool RayCast::ResolvePruner()
{
    if (Cheat::LibUnityAddress == 0 || g_Memory == nullptr) return false;

    uint32_t pxScene = FindPxScene(static_cast<uint32_t>(Cheat::LibUnityAddress));
    if (pxScene == 0) return false;

    g_staticPruner = g_Memory->Read<uint32_t>(pxScene + PhysX::v7a::PxSceneToQueryManager + PhysX::v7a::QueryManagerToStaticPruner);
    if (BadPtr(g_staticPruner)) { g_staticPruner = 0; return false; }

    return true;
}

void RayCast::RefreshShapes()
{
    if (g_staticPruner == 0 || g_Memory == nullptr) return;

    uint32_t objectCount   = g_Memory->Read<uint32_t>(g_staticPruner + PhysX::v7a::PrunerObjectCount);
    uint32_t boundsPointer = g_Memory->Read<uint32_t>(g_staticPruner + PhysX::v7a::PrunerWorldBounds);
    uint32_t payloadPointer= g_Memory->Read<uint32_t>(g_staticPruner + PhysX::v7a::PrunerPayloads);

    if (objectCount == 0 || BadPtr(boundsPointer) || BadPtr(payloadPointer))
    {
        g_staticPruner = 0;
        return;
    }

    if (objectCount > MaxShapeCount) objectCount = MaxShapeCount;

    std::vector<float> rawBounds(objectCount * PhysX::v7a::BoundsFloatCount);
    if (!g_Memory->ReadBytes(boundsPointer, rawBounds.data(), objectCount * PhysX::v7a::BoundsByteSize))
        return;

    std::vector<uint32_t> rawPayload(objectCount * PhysX::v7a::PayloadElementCount);
    if (!g_Memory->ReadBytes(payloadPointer, rawPayload.data(), objectCount * PhysX::v7a::PayloadByteSize))
        return;

    // Build into LOCAL vectors — publishing only happens at the very end via
    // shared_ptr swap. On any early-return failure below we simply drop these
    // locals; the previously published snapshot stays visible to consumers,
    // which is the safe behavior (stale walls beat empty walls).
    std::vector<OBBShape> localOriented;
    std::vector<AABB>     localNon;
    localNon.reserve(objectCount / 4);
    localOriented.reserve(objectCount / 2);

    for (uint32_t i = 0; i < objectCount; i++)
    {
        uint32_t shapeCore = rawPayload[i * PhysX::v7a::PayloadElementCount];
        if (BadPtr(shapeCore)) continue;

        auto geometryType = static_cast<RayCast::PxGeometryType>(
            g_Memory->Read<uint32_t>(shapeCore + PhysX::v7a::ShapeCoreGeometryType));

        if (geometryType != RayCast::PxGeometryType::eBox
            && geometryType != RayCast::PxGeometryType::eConvexMesh
            && geometryType != RayCast::PxGeometryType::eTriangleMesh)
            continue;

        float* bounds = &rawBounds[i * PhysX::v7a::BoundsFloatCount];
        if (bounds[3] - bounds[0] < 0.01f
            && bounds[4] - bounds[1] < 0.01f
            && bounds[5] - bounds[2] < 0.01f)
            continue;

        if (geometryType == RayCast::PxGeometryType::eBox)
        {
            uint32_t rigidCore = rawPayload[i * PhysX::v7a::PayloadElementCount + 1];
            if (BadPtr(rigidCore)) continue;

            uint8_t shapeCoreBlock[52]{};
            if (!g_Memory->ReadBytes(shapeCore + PhysX::v7a::ShapeCoreLocalPose, shapeCoreBlock, 52))
                continue;

            float* halfExtents = reinterpret_cast<float*>(shapeCoreBlock + 0x28);
            if (halfExtents[0] <= 0.0f || halfExtents[1] <= 0.0f || halfExtents[2] <= 0.0f)
                continue;

            PxTransformData actorPose{};
            if (!g_Memory->ReadBytes(rigidCore + PhysX::v7a::RigidCoreGlobalPose, &actorPose, sizeof(actorPose)))
                continue;

            float qx = actorPose.qx, qy = actorPose.qy, qz = actorPose.qz, qw = actorPose.qw;
            float xx = qx * qx, yy = qy * qy, zz = qz * qz;
            float xy = qx * qy, xz = qx * qz, yz = qy * qz;
            float wx = qw * qx, wy = qw * qy, wz = qw * qz;

            localOriented.push_back({
                halfExtents[0], halfExtents[1], halfExtents[2],
                1 - 2 * (yy + zz), 2 * (xy + wz),     2 * (xz - wy),
                2 * (xy - wz),     1 - 2 * (xx + zz), 2 * (yz + wx),
                2 * (xz + wy),     2 * (yz - wx),     1 - 2 * (xx + yy),
                actorPose.px, actorPose.py, actorPose.pz
            });
        }
        else
        {
            localNon.push_back({
                bounds[0], bounds[1], bounds[2],
                bounds[3], bounds[4], bounds[5]
            });
        }
    }

    // Publish. shared_ptr swap under a mutex is the classic "producer builds
    // off-thread, consumers snapshot the pointer" handoff. Consumers hold a
    // reference for the duration of one IsBlocked() call — even if the worker
    // replaces g_shapes mid-scan, the older ShapeSet stays alive until the
    // last consumer releases its shared_ptr.
    auto next = std::make_shared<ShapeSet>();
    next->orientedBox = std::move(localOriented);
    next->nonBox      = std::move(localNon);
    {
        std::lock_guard<std::mutex> lk(g_shapesMx);
        g_shapes = std::move(next);
    }
}

void RayCast::Initialize()
{
    // no-op — RayCast state is now maintained by a dedicated worker thread;
    // see Start()/Stop(). Kept as a symbol so the existing call sites in
    // Draw.cpp and AimModules.cpp keep compiling; the periodic per-frame
    // refresh spike they used to trigger on the render thread is gone.
}

bool RayCast::IsBlocked(Vector3 from, Vector3 to)
{
    // Grab the current published ShapeSet under the lock (nanoseconds), then
    // release the lock and iterate our local snapshot with zero locking —
    // the shared_ptr keeps the ShapeSet alive for the duration of this call
    // even if the worker publishes a replacement in the middle of the loop.
    std::shared_ptr<const ShapeSet> snap;
    {
        std::lock_guard<std::mutex> lk(g_shapesMx);
        snap = g_shapes;
    }
    if (!snap || (snap->orientedBox.empty() && snap->nonBox.empty()))
        return false;

    Vector3 delta = to - from;
    float distance = Vector3::Magnitude(delta);
    if (distance < 0.01f) return false;

    Vector3 direction = Vector3::Normalized(delta);
    Vector3 inverseDirection = {
        (fabsf(direction.X) > 1e-9f) ? 1.0f / direction.X : (direction.X >= 0 ?  1e9f : -1e9f),
        (fabsf(direction.Y) > 1e-9f) ? 1.0f / direction.Y : (direction.Y >= 0 ?  1e9f : -1e9f),
        (fabsf(direction.Z) > 1e-9f) ? 1.0f / direction.Z : (direction.Z >= 0 ?  1e9f : -1e9f)
    };

    for (const OBBShape& wall : snap->orientedBox)
    {
        if (PointInsideOBB(from, wall) || PointInsideOBB(to, wall)) continue;
        if (RayIntersectsOBB(from, direction, distance, wall)) return true;
    }

    for (const AABB& wall : snap->nonBox)
    {
        if (PointInsideAABB(from, wall) || PointInsideAABB(to, wall)) continue;
        if (RayIntersectsAABB(from, inverseDirection, distance, wall)) return true;
    }

    return false;
}

bool RayCast::IsVisible(Vector3 cameraPosition, Vector3 targetPosition)
{
    return !IsBlocked(cameraPosition, targetPosition);
}

void RayCast::Reset()
{
    // Contract: Stop() must have been called first — the worker owns
    // g_staticPruner and g_shapes while running, so mutating them here would
    // race the worker. Cheat::Unload orders Stop() before Reset().
    g_staticPruner = 0;
    std::lock_guard<std::mutex> lk(g_shapesMx);
    g_shapes.reset();
}

// ─── Worker thread ──────────────────────────────────────────────────────────
static void WorkerLoop()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    // Do a refresh immediately on start so the first IsBlocked() call has
    // geometry to test against, then rebuild every kRefreshIntervalMs.
    DWORD lastRefresh = 0;

    while (g_workerRunning.load(std::memory_order_acquire))
    {
        const DWORD now = GetTickCount();
        if (lastRefresh == 0 || (now - lastRefresh) >= kRefreshIntervalMs)
        {
            try
            {
                // Invalida o page cache do EMemory antes de reler a geometria
                // do pruner — sem isso, o worker enxergaria uma foto stale
                // depois de o mapa/instância trocar.
                EMemory::BeginFrameRead();
                if (g_staticPruner == 0) RayCast::ResolvePruner();
                if (g_staticPruner != 0) RayCast::RefreshShapes();
            }
            catch (...)
            {
                // Mirror Producer::RunLoop: swallow anything the RPM layer
                // throws so a transient bad read doesn't take down the whole
                // worker. Next tick will retry.
            }
            lastRefresh = now;
        }

        // Sleep in small chunks so Stop() joins within ~kWorkerPollMs instead
        // of blocking for the full refresh interval.
        DWORD slept = 0;
        while (slept < kRefreshIntervalMs && g_workerRunning.load(std::memory_order_acquire))
        {
            Sleep(kWorkerPollMs);
            slept += kWorkerPollMs;
        }
    }
}

void RayCast::Start(uint32_t il2cpp)
{
    std::lock_guard<std::mutex> lk(g_lifecycleMx);
    if (g_workerRunning.load()) return;
    if (il2cpp == 0 || g_Memory == nullptr) return;

    g_workerIl2cpp = il2cpp;
    g_workerRunning.store(true, std::memory_order_release);
    g_workerThread = std::thread(&WorkerLoop);
}

void RayCast::Stop()
{
    std::lock_guard<std::mutex> lk(g_lifecycleMx);
    if (!g_workerRunning.load()) return;

    g_workerRunning.store(false, std::memory_order_release);
    if (g_workerThread.joinable())
        g_workerThread.join();
    g_workerIl2cpp = 0;
}
