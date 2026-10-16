# Watch squad — writer

**Do not apply.** Campaign 0 has not ranked these. The three hunks below are one future slice each. They stay in this file until the frame log places that slice above campaigns 1–6.

`SMem.cpp` stays as it is. `SMemReAlloc` already `realloc`s when `SMEM_FLAG_NOREALLOC` is clear. The trivial-array slice only stops passing that flag from `TSFixedArray::ReallocData`.

## 1. Trivial-type `TSFixedArray` realloc

**Gate.** The one-second line shows `ReallocData` allocs and copy-constructs above the campaign 1–6 counters. Every container in the client grows through this function. Original Storm behavior is the `NOREALLOC` path.

**Unsafe while this holds:** a `T` that `std::is_trivially_copyable` and `std::is_trivially_destructible` accept still has a live address into the block across `ReallocData`, or its bytes are not a valid `T` after a move. That includes an element that stores a pointer to itself or to a neighbor, and any caller that treats a changed `m_data` as the signal that the copy constructor ran. `realloc` may return the same address. With `SMEM_FLAG_NOREALLOC`, `SMemReAlloc` returns null, and `ReallocData` copy-constructs into a fresh block and destroys the old one.

A zero `count` is the other break. `SMemReAlloc` of size 0 can free `olddata` and return null (`SMem.cpp` returns null when `realloc` yields null and the rounded size is 0). The existing null fallback would then copy-construct from that freed pointer. The hunk leaves `count == 0` on the `NOREALLOC` path.

```diff
diff --git a/Engine/Source/Storm/H/stpl.h b/Engine/Source/Storm/H/stpl.h
--- a/Engine/Source/Storm/H/stpl.h
+++ b/Engine/Source/Storm/H/stpl.h
@@ -3,6 +3,7 @@
 #include <cstddef>
 #include <cstdint>
 #include <cstring>
+#include <type_traits>
 #include <new>
 #include <typeinfo>
 #include "Engine/Source/Base/OsCrt.h"
@@ -754,7 +755,32 @@ void TSFixedArray<T>::ReallocData(uint32_t count) {
     }
 
     this->m_alloc = count;
-    this->m_data = static_cast<T*>(SMemReAlloc(olddata, sizeof(T) * count, this->MemFileName(), this->MemLineNo(), SMEM_FLAG_NOREALLOC));
+
+    // -- ʕ •ᴥ•ʔ✿ Byte move only. Count 0 stays on the NOREALLOC path so realloc cannot free olddata and return null. ✿ ʕ •ᴥ•ʔ
+    if constexpr (std::is_trivially_copyable<T>::value && std::is_trivially_destructible<T>::value) {
+        if (count != 0) {
+            void* moved = SMemReAlloc(olddata, sizeof(T) * static_cast<size_t>(count), this->MemFileName(), this->MemLineNo(), 0);
+
+            if (moved) {
+                this->m_data = static_cast<T*>(moved);
+
+                if (olddata && moved != olddata) {
+                    uint32_t live = count < this->m_count ? count : this->m_count;
+
+                    for (uint32_t i = 0; i < live; i++) {
+                        T copy;
+                        memcpy(&copy, static_cast<char*>(moved) + i * sizeof(T), sizeof(T));
+                        new (&this->m_data[i]) T(copy);
+                    }
+                }
+
+                return;
+            }
+        }
+    }
+
+    this->m_data = static_cast<T*>(SMemReAlloc(olddata, sizeof(T) * count, this->MemFileName(), this->MemLineNo(), SMEM_FLAG_NOREALLOC));
 
     if (!this->m_data) {
         this->m_data = static_cast<T*>(SMemAlloc(sizeof(T) * count, this->MemFileName(), this->MemLineNo(), 0));
```

The tail destructor loop above this hunk stays. It runs before the branch. `ReallocAndClearData` is a different contract (destroy every element, then `SMemReAlloc` with flags 0) and is not part of this slice.

Non-trivial `T` still takes `SMEM_FLAG_NOREALLOC`, then copy-constructs. `TSLink` and anything with a user-provided copy or destructor stay on that path. `C3Vector` has a user-provided default constructor and an implicit copy, so the trait accepts it.

## 2. Lighter `Job::ParallelFor` barrier

**Gate.** Campaign 1 still spends time inside small doodad jobs. `FillDoodadSkin` publishes only when `instances >= 8`. The current wait is `remaining == 0 && finished == workerCount`, so every parked worker must wake, take `g_pool.mu`, and check in, including workers that stole no chunk.

**Unsafe while this holds:** a worker copies `fn` and `user` after `remaining` hits zero. `remaining == 0` means every published chunk has returned from `fn`. A worker notified for this epoch can still be before `RunRanges`, and can still be about to read `g_pool.fn`. The next `ParallelFor` stores `next = 0` and a new `user`. That worker then `fetch_add`s the new counter and runs the old callback.

Callers pass stack objects and use the results as soon as `ParallelFor` returns:

- `DoodadSkin skin` in `CM2SceneRender::DrawBatchDoodad`, then `BufUnlock`
- `VisBatch batch` in `CMapChunk::FlushQueuedVisibility`
- `M2AnimateBatch batch` in `CM2Scene::Animate`
- `jobs.data()` in `FlushOwnedFills`, then `BufUnlock`
- `s_fills` in `PrepareGrassFills`, then `BufUnlock`

The hunk is also unsafe if `ParallelFor` is not main-thread-only, or if `fn` calls `Job::` again. One pool, one epoch, and the join waits on `g_pool.mu`. The header already requires both. The `closed` test and the copy of `fn` / `user` / `count` / `chunks` happen under that same lock. A worker that reads them outside it races the next publish.

```diff
diff --git a/Engine/Source/Base/Job/JobSystem.h b/Engine/Source/Base/Job/JobSystem.h
--- a/Engine/Source/Base/Job/JobSystem.h
+++ b/Engine/Source/Base/Job/JobSystem.h
@@ -16,9 +16,10 @@ using RangeFn = void (*)(Range range, void* user);
 void Init();
 void Shutdown();
 uint32_t WorkerCount();
 
-// Main thread only. Splits [0, count) across the pool and the calling thread.
-// Returns when every range has finished. fn must not call Job:: or anything on g_theGxDevicePtr.
+// Main thread only. Splits [0, count) across the pool and the calling thread.
+// Returns when every range has finished and every worker that copied this epoch has left RunRanges.
+// fn must not call Job:: or anything on g_theGxDevicePtr.
 void ParallelFor(uint32_t count, RangeFn fn, void* user);
 
 }
diff --git a/Engine/Source/Base/Job/JobSystem.cpp b/Engine/Source/Base/Job/JobSystem.cpp
--- a/Engine/Source/Base/Job/JobSystem.cpp
+++ b/Engine/Source/Base/Job/JobSystem.cpp
@@ -17,8 +17,10 @@ struct Pool {
     std::vector<std::thread> threads;
     uint32_t workerCount = 0;
     uint32_t epoch = 0;
-    uint32_t finished = 0;
+    uint32_t entered = 0;
+    uint32_t left = 0;
     bool shutdown = false;
+    bool closed = false;
     Job::RangeFn fn = nullptr;
     void* user = nullptr;
     uint32_t count = 0;
@@ -72,16 +74,23 @@ void WorkerMain() {
         if (g_pool.shutdown) {
             return;
         }
 
+        // -- ʕ •ᴥ•ʔ✿ Asleep workers must observe closed under this lock before they read fn. ✿ ʕ •ᴥ•ʔ
+        if (g_pool.closed) {
+            seen = g_pool.epoch;
+            continue;
+        }
+
         seen = g_pool.epoch;
         Job::RangeFn fn = g_pool.fn;
         void* user = g_pool.user;
         uint32_t count = g_pool.count;
         uint32_t chunks = g_pool.chunks;
+        g_pool.entered += 1;
         lock.unlock();
         RunRanges(fn, user, count, chunks);
         lock.lock();
-        g_pool.finished += 1;
+        g_pool.left += 1;
         g_pool.cv.notify_all();
     }
 }
@@ -144,20 +155,26 @@ void ParallelFor(uint32_t count, RangeFn fn, void* user) {
         std::lock_guard<std::mutex> lock(g_pool.mu);
         g_pool.fn = fn;
         g_pool.user = user;
         g_pool.count = count;
         g_pool.chunks = chunks;
         g_pool.next.store(0);
         g_pool.remaining.store(chunks);
-        g_pool.finished = 0;
+        g_pool.entered = 0;
+        g_pool.left = 0;
+        // -- ʕ •ᴥ•ʔ✿ closed clears before epoch moves, under this lock, so a late worker cannot skip the new epoch. ✿ ʕ •ᴥ•ʔ
+        g_pool.closed = false;
         g_pool.epoch += 1;
         g_pool.cv.notify_all();
     }
 
     RunRanges(fn, user, count, chunks);
 
     std::unique_lock<std::mutex> lock(g_pool.mu);
-    // -- ʕ •ᴥ•ʔ✿ Stay until ranges finish and workers drop fn/next, so the next publish cannot race them. ✿ ʕ •ᴥ•ʔ
     g_pool.cv.wait(lock, [] {
-        return g_pool.remaining.load() == 0 && g_pool.finished == g_pool.workerCount;
+        return g_pool.remaining.load() == 0;
     });
+    g_pool.closed = true;
+    g_pool.cv.wait(lock, [] {
+        return g_pool.left == g_pool.entered;
+    });
 }
```

The worker loop still checks `shutdown` before the `closed` test (the existing check at the top of the wakeup stays). `notify_all` still runs. The caller stops waiting for workers that have not copied this epoch. Those workers take the lock, see `closed`, and do not call `fn`.

## 3. `ClipBufferUpdate` cache

**Gate.** The outdoor or city line shows `CMapChunk::ClipBufferUpdate` above the campaign 1–6 counters. Each eligible chunk builds two 9-vertex outlines. Quadrant bit 0 selects indices `0..8` or `136..144` (`s_activeWorldView.x > camTarget.x`). Bit 1 selects the column `0,17,...` or `8,25,...` (`s_activeWorldView.y > camTarget.y`). Both go through `CWorldOcclusion::UpdateBufferChunk`.

**Unsafe while this holds:** a match on the eighteen edge heights and the quadrant is a complete key for the outlines. `UpdateBufferChunk` also projects `CMapChunk::vertexList` xy plus `m_height` and `m_topLeftCoords` through `s_clipBufferMatrix`, which `CWorldScene::Update` rebuilds from the camera. `CWorldScene::Render` then fills `s_clipBuffer` with `-1000000` before occlusion runs, so a hit that skips the write leaves a cleared horizon. Holed chunks (`m_holes != 0`) store `-1000001` on columns whose `s_clipBufferMask` bit is clear. A stored punch is wrong on a later frame whose mask differs. `CMap::FreeChunk` returns the same address from `ObjectFree`. `vertexList` is one static grid; `Intersect` writes its z, and `InitializeVertexGrid` writes its xy.

The hunk keys quadrant, holes, top-left, the eighteen heights, and the matrix. A hit replays clamped spans and re-reads the mask. It still writes, because the buffer was cleared this frame. The cache lives beside `CMapChunk`, not in it, so the `/* 011C */` layout stays. `~CMapChunk` drops the slot because `FreeChunk` calls that destructor before `ObjectFree`.

The hunk is still unsafe while `vertexList` xy changes under a live entry, or while two threads call `ClipBufferUpdate`. `UpdateOcclusion` is the only caller and it runs on the main thread. A second view would share `s_chunkClipCaches` and `s_clipBuffer`.

```diff
diff --git a/WoW/Source/WorldClient/CWorldOcclusion.h b/WoW/Source/WorldClient/CWorldOcclusion.h
--- a/WoW/Source/WorldClient/CWorldOcclusion.h
+++ b/WoW/Source/WorldClient/CWorldOcclusion.h
@@ -38,6 +38,12 @@ class CClipVolume {
     /* 0004 */ uint32_t m_planeCount;
 };
 
+struct CWorldOcclusionSpan {
+    /* 0000 */ int32_t m_x0;
+    /* 0004 */ int32_t m_x1;
+    /* 0008 */ float m_height;
+};
+
 class CWorldOcclusion {
     public:
     static TSGrowableArray<C4Plane> planeArray;
@@ -51,7 +57,8 @@ class CWorldOcclusion {
     static uint32_t QueryBuffer(CAaBox* box, uint8_t flags);
     static uint32_t QueryBuffer(CAaSphere* box, uint8_t flags);
     static uint32_t QueryBuffer(const C3Vector& center, float radius, uint8_t flags);
     static void UpdateBuffer(const C3Vector* points, int32_t count, uint32_t flags);
-    static void UpdateBufferChunk(const C3Vector* verts, const float* heights, const uint32_t* indices, int32_t count, const C3Vector* offset, int32_t holes);
+    static void UpdateBufferChunk(const C3Vector* verts, const float* heights, const uint32_t* indices, int32_t count, const C3Vector* offset, int32_t holes, CWorldOcclusionSpan* record, uint32_t* recordCount, uint32_t recordCap);
+    static void ReplayBufferChunk(const CWorldOcclusionSpan* spans, uint32_t spanCount, int32_t holes);
     static void AddVolume(const C3Vector* eye, const C3Vector* verts, int32_t count, int32_t clipToView);
 };
diff --git a/WoW/Source/WorldClient/CWorldOcclusion.cpp b/WoW/Source/WorldClient/CWorldOcclusion.cpp
--- a/WoW/Source/WorldClient/CWorldOcclusion.cpp
+++ b/WoW/Source/WorldClient/CWorldOcclusion.cpp
@@ -327,8 +327,47 @@ void CWorldOccluder::Initialize() {
 }
 
+static void EmitSpan(int32_t x0, int32_t x1, float height, int32_t holes, CWorldOcclusionSpan* record, uint32_t* recordCount, uint32_t recordCap) {
+    if (x0 < 0) {
+        x0 = 0;
+    }
+
+    if (x1 >= 384) {
+        x1 = 383;
+    }
+
+    if (x0 > x1) {
+        return;
+    }
+
+    if (holes) {
+        for (int32_t x = x0; x <= x1; x++) {
+            if (!(CWorldScene::s_clipBufferMask[x] & 1)) {
+                CWorldScene::s_clipBuffer[x] = -1000001.0f;
+            }
+        }
+    } else {
+        for (int32_t x = x0; x <= x1; x++) {
+            if (height > CWorldScene::s_clipBuffer[x]) {
+                CWorldScene::s_clipBuffer[x] = height;
+            }
+        }
+    }
+
+    if (record && recordCount && *recordCount < recordCap) {
+        CWorldOcclusionSpan& span = record[*recordCount];
+        span.m_x0 = x0;
+        span.m_x1 = x1;
+        span.m_height = height;
+        *recordCount += 1;
+    }
+}
+
 // VERIFY: 4db2afc6 manual
 // OFFSET: 0x0078F6A0
-void CWorldOcclusion::UpdateBufferChunk(const C3Vector* verts, const float* heights, const uint32_t* indices, int32_t count, const C3Vector* offset, int32_t holes) {
+void CWorldOcclusion::UpdateBufferChunk(const C3Vector* verts, const float* heights, const uint32_t* indices, int32_t count, const C3Vector* offset, int32_t holes, CWorldOcclusionSpan* record, uint32_t* recordCount, uint32_t recordCap) {
     if (!(CWorld::enables & Enable_Culling) || CWorldScene::camVec.z < -0.89999998f || CWorldScene::camVec.z > 0.89999998f) {
         return;
     }
@@ -351,62 +390,39 @@ void CWorldOcclusion::UpdateBufferChunk(const C3Vector* verts, const float* hei
     if (holes) {
         for (int32_t i = 0; i < count - 1; i++) {
             int32_t x0 = CMath::fint_(s_clipBufferVerts[i].x * 64.0f - 0.5f) + 192;
             int32_t x1 = CMath::fint_(s_clipBufferVerts[i + 1].x * 64.0f - 0.5f) + 192;
 
             if (x1 < x0) {
                 int32_t swap = x0;
                 x0 = x1;
                 x1 = swap;
             }
 
-            if (x0 < 0) {
-                x0 = 0;
-            }
-
-            if (x1 >= 384) {
-                x1 = 383;
-            }
-
-            for (; x0 <= x1; x0++) {
-                if (!(CWorldScene::s_clipBufferMask[x0] & 1)) {
-                    CWorldScene::s_clipBuffer[x0] = -1000001.0f;
-                }
-            }
+            EmitSpan(x0, x1, -1000001.0f, holes, record, recordCount, recordCap);
         }
 
         return;
     }
 
     for (int32_t i = 0; i < count - 1; i++) {
         const C3Vector& p0 = s_clipBufferVerts[i];
         const C3Vector& p1 = s_clipBufferVerts[i + 1];
 
         if (p0.z < 0.027777778f || p1.z < 0.027777778f) {
             continue;
         }
 
         float height = p1.y <= p0.y ? p1.y : p0.y;
         int32_t x0 = CMath::fint_(p0.x * 64.0f - 0.5f) + 192;
         int32_t x1 = CMath::fint_(p1.x * 64.0f - 0.5f) + 192;
 
         if (x1 < x0) {
             int32_t swap = x0;
             x0 = x1;
             x1 = swap;
         }
 
-        if (x0 < 0) {
-            x0 = 0;
-        }
-
-        if (x1 >= 384) {
-            x1 = 383;
-        }
-
-        for (; x0 <= x1; x0++) {
-            if (height > CWorldScene::s_clipBuffer[x0]) {
-                CWorldScene::s_clipBuffer[x0] = height;
-            }
-        }
+        EmitSpan(x0, x1, height, holes, record, recordCount, recordCap);
     }
 }
 
+void CWorldOcclusion::ReplayBufferChunk(const CWorldOcclusionSpan* spans, uint32_t spanCount, int32_t holes) {
+    if (!(CWorld::enables & Enable_Culling) || CWorldScene::camVec.z < -0.89999998f || CWorldScene::camVec.z > 0.89999998f) {
+        return;
+    }
+
+    for (uint32_t i = 0; i < spanCount; i++) {
+        EmitSpan(spans[i].m_x0, spans[i].m_x1, spans[i].m_height, holes, nullptr, nullptr, 0);
+    }
+}
+
 // VERIFY: 5b9f55f1 manual
 // OFFSET: 0x0078F900
 void CWorldOcclusion::UpdateBuffer(const C3Vector* points, int32_t count, uint32_t flags) {
diff --git a/WoW/Source/WorldClient/MapChunk.cpp b/WoW/Source/WorldClient/MapChunk.cpp
--- a/WoW/Source/WorldClient/MapChunk.cpp
+++ b/WoW/Source/WorldClient/MapChunk.cpp
@@ -5,6 +5,8 @@
 #include "Engine/Source/Tempest/C3Ray.h"
 #include "Engine/Source/Tempest/NTempest.h"
 #include <cmath>
+#include <cstring>
+#include <unordered_map>
 #include "Engine/Source/Base/CDataAllocator.h"
@@ -1297,10 +1299,93 @@ void CMapChunk::UpdateLights() {
     }
 }
 
+struct ChunkClipCache {
+    uint32_t quadrant;
+    int32_t holes;
+    C3Vector topLeft;
+    C44Matrix matrix;
+    float samples[18];
+    CWorldOcclusionSpan spans[16];
+    uint32_t spanCount;
+    uint8_t valid;
+};
+
+static std::unordered_map<CMapChunk*, ChunkClipCache> s_chunkClipCaches;
+
+static bool ClipOcclusionSuppressed() {
+    return !(CWorld::enables & Enable_Culling) || CWorldScene::camVec.z < -0.89999998f || CWorldScene::camVec.z > 0.89999998f;
+}
+
+static uint32_t ClipQuadrant() {
+    uint32_t quadrant = 0;
+
+    if (CWorldScene::s_activeWorldView.x > CWorldScene::camTarget.x) {
+        quadrant |= 1u;
+    }
+
+    if (CWorldScene::s_activeWorldView.y > CWorldScene::camTarget.y) {
+        quadrant |= 2u;
+    }
+
+    return quadrant;
+}
+
+static void LoadClipSamples(const float* height, uint32_t quadrant, float samples[18]) {
+    uint32_t edgeA = (quadrant & 1u) ? 136u : 0u;
+    uint32_t edgeB = (quadrant & 2u) ? 8u : 0u;
+
+    for (uint32_t i = 0; i < 9; i++) {
+        samples[i] = height[edgeA + i];
+        samples[9 + i] = height[edgeB + 17u * i];
+    }
+}
+
+static bool ClipCacheHit(const ChunkClipCache& slot, uint32_t quadrant, int32_t holes, const C3Vector& topLeft, const float samples[18]) {
+    return slot.valid
+        && slot.quadrant == quadrant
+        && slot.holes == holes
+        && memcmp(&slot.topLeft, &topLeft, sizeof(slot.topLeft)) == 0
+        && memcmp(slot.samples, samples, sizeof(float) * 18) == 0
+        && memcmp(&slot.matrix, &CWorldScene::s_clipBufferMatrix, sizeof(slot.matrix)) == 0;
+}
+
 // VERIFY: c9630247 manual
 // OFFSET: 0x007CFB10
 void CMapChunk::ClipBufferUpdate() {
+    if (ClipOcclusionSuppressed()) {
+        return;
+    }
+
+    uint32_t quadrant = ClipQuadrant();
+    float samples[18];
+    LoadClipSamples(this->m_height, quadrant, samples);
+    int32_t holes = this->m_header->m_holes;
+    auto found = s_chunkClipCaches.find(this);
+
+    if (found != s_chunkClipCaches.end() && ClipCacheHit(found->second, quadrant, holes, this->m_topLeftCoords, samples)) {
+        // -- ʕ •ᴥ•ʔ✿ Render cleared s_clipBuffer this frame. Replay the spans. The mask is read again. ✿ ʕ •ᴥ•ʔ
+        CWorldOcclusion::ReplayBufferChunk(found->second.spans, found->second.spanCount, holes);
+        return;
+    }
+
     uint32_t indices[9];
-    uint32_t base = 0;
-
-    if (CWorldScene::s_activeWorldView.x > CWorldScene::camTarget.x) {
-        base = 136;
-    }
+    CWorldOcclusionSpan spans[16];
+    uint32_t spanCount = 0;
+    uint32_t base = (quadrant & 1u) ? 136u : 0u;
 
     for (uint32_t i = 0; i < 9; i++) {
         indices[i] = i + base;
     }
 
-    CWorldOcclusion::UpdateBufferChunk(CMapChunk::vertexList, this->m_height, indices, 9, &this->m_topLeftCoords, this->m_header->m_holes);
+    CWorldOcclusion::UpdateBufferChunk(CMapChunk::vertexList, this->m_height, indices, 9, &this->m_topLeftCoords, holes, spans, &spanCount, 16);
 
-    base = 0;
-
-    if (CWorldScene::s_activeWorldView.y > CWorldScene::camTarget.y) {
-        base = 8;
-    }
+    base = (quadrant & 2u) ? 8u : 0u;
 
     for (uint32_t i = 0; i < 9; i++) {
         indices[i] = base;
         base += 17;
     }
 
-    CWorldOcclusion::UpdateBufferChunk(CMapChunk::vertexList, this->m_height, indices, 9, &this->m_topLeftCoords, this->m_header->m_holes);
+    CWorldOcclusion::UpdateBufferChunk(CMapChunk::vertexList, this->m_height, indices, 9, &this->m_topLeftCoords, holes, spans, &spanCount, 16);
+
+    ChunkClipCache& slot = s_chunkClipCaches[this];
+    slot.quadrant = quadrant;
+    slot.holes = holes;
+    slot.topLeft = this->m_topLeftCoords;
+    slot.matrix = CWorldScene::s_clipBufferMatrix;
+    memcpy(slot.samples, samples, sizeof(samples));
+    memcpy(slot.spans, spans, sizeof(CWorldOcclusionSpan) * spanCount);
+    slot.spanCount = spanCount;
+    slot.valid = 1;
 }
@@ -1836,6 +1919,9 @@ void CMapChunk::CreateVerticesWorld(CGxVertexPNC* buf, const C3Vector& offset)
 // VERIFY: 15f51c4a manual
 // OFFSET: [0x007C5E50, 0x007C6480]
 CMapChunk::~CMapChunk() {
+    s_chunkClipCaches.erase(this);
 }
```

Miss-path `operator[]` allocates a map node the first time a chunk address is seen. The hit path is `find` plus the span walk. Eighteen height loads still happen on a hit, so the key can see an edited edge. `CWorldOccluder::ClipBufferUpdate` is a two-point segment and is not this cache.
