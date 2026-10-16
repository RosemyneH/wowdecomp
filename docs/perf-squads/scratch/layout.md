# Campaign 5 — Cull and visibility scratch — Layout

File-scope vectors. `clear()` keeps capacity. Workers write slots by index; they do not own scratch. Cancel is O(1). The flush still visits the same set it visits today.

`Job::ParallelFor` runs the caller inline and waits until every worker drops the batch (`Engine/Source/Base/Job/JobSystem.cpp:156`). A thread-local vector would split one batch across workers and race on `push_back`. Do not use `thread_local` for these arenas.

Do not append fields to `CMapChunk`. `CMap::chunkHeap` is `ObjectAllocAddHeap(sizeof(CMapChunk), …)` (`WoW/Source/WorldClient/Map.cpp:776`). Pinned offsets run through `m_fishableBits` (`WoW/Source/WorldClient/MapChunk.h:142`). The visibility stamp uses the existing handle `CMapBaseObj::m_objectIndex` (`WoW/Source/WorldClient/MapBaseObj.h:15`), stored by `CMap::AllocMapChunk` (`WoW/Source/WorldClient/Map.cpp:3305`). Handles are recycled from the index stack (`WoW/Source/ObjectAlloc/ObjectAlloc.cpp:203`, `WoW/Source/ObjectAlloc/ObjectAlloc.cpp:318`) and are not reset when the slot is reused.

## Capacity rule

Every scratch vector below lives for the process, in the anonymous namespace of the file that already owns the call.

- `clear()` then `resize` or `push_back`. `clear()` does not free the buffer.
- `resize(n)` and `push_back` allocate only when `n` exceeds `capacity()`.
- No `shrink_to_fit`. No `std::vector<T>(n)` temporary. No `swap` with a function-local vector. That swap is what drops capacity today: `requests.swap(s_visQueue)` (`WoW/Source/WorldClient/MapChunk.cpp:519`) leaves `s_visQueue` at capacity 0, and the local buffer is freed when `FlushQueuedVisibility` returns. The same leak is `pending.swap(s_ownedFills)` (`WoW/Source/WorldClient/MapRenderChunk.cpp:250`) and the local `jobs` (`WoW/Source/WorldClient/MapRenderChunk.cpp:260`).
- No fixed `reserve`. High-water is whatever the outdoor scene pushed on its first spike. Later frames stay inside that capacity.
- After a flush, `size()` is 0 and `capacity()` is the high-water.

Three flushes, after that spike, do not call `malloc`: `CMapChunk::FlushQueuedVisibility`, `CWorldScene::CullDoodadsExterior`, `FlushOwnedFills`.

## 1. Visibility arena

Owner: anonymous namespace of `WoW/Source/WorldClient/MapChunk.cpp`, next to `s_visQueue` (`WoW/Source/WorldClient/MapChunk.cpp:388`). Main thread only. Callers are `CMapRenderChunk::PurgeChunkBufBlocks` (`WoW/Source/WorldClient/MapRenderChunk.cpp:671`) and `CMapRenderChunk::UpdatePools` (`WoW/Source/WorldClient/MapRenderChunk.cpp:806`).

`VisJob`, `LiquidOut`, `VisBatch`, and `VisSnapshot` keep their fields (`WoW/Source/WorldClient/MapChunk.cpp:348`). Parallel results stay per slot. Linking stays on the main thread after `ParallelFor` (`WoW/Source/WorldClient/MapChunk.cpp:577`).

### Fields

`VisRequest` (`WoW/Source/WorldClient/MapChunk.cpp:342`) gains two copies taken at enqueue:

| Field | Type | Meaning |
| --- | --- | --- |
| `chunk` | `CMapChunk*` | Live only while the stamp matches. |
| `linkChunk` | `int32_t` | Unchanged. |
| `wantLiquids` | `int32_t` | Unchanged. |
| `handle` | `uint32_t` | `chunk->m_objectIndex` at enqueue. |
| `gen` | `uint32_t` | `s_visSlots[handle].gen` at enqueue. |

New file-scope vectors:

| Name | Element | Lifetime | Capacity |
| --- | --- | --- | --- |
| `s_visQueue` | `VisRequest` | Process. `clear()` at the end of every flush, including a flush whose live count is 0. | High-water of requests queued in one frame. |
| `s_visJobs` | `VisJob` | Process. `clear()` then `resize(live)` once per flush. | High-water of live requests in one flush. |
| `s_visLiquids` | `LiquidOut` | Process. `clear()` then `resize(liquidTotal)`. | High-water of `liquidTotal`. |
| `s_visSlots` | `VisSlot { uint32_t gen; uint8_t queued; }` | Process. Indexed by handle. `resize` upward only. Never `clear()` — a clear would resurrect a cancelled request after the handle is recycled. | `maxHandle + 1` seen so far. |

`s_visLive` is a `uint32_t` count of slots with `queued == 1`.

`EnqueueVisibility` (`WoW/Source/WorldClient/MapChunk.cpp:415`) grows `s_visSlots` to `handle + 1` when the handle is new, then writes `req.gen` and sets `queued` from 0 to 1 (`s_visLive++`). A second push for a chunk that is already queued does not happen: `CMapArea::Update` calls `AddMapChunk` and then `CullAndBucketChunk` back to back (`WoW/Source/WorldClient/MapArea.cpp:259`). `CullAndBucketChunk` (`WoW/Source/WorldClient/MapChunk.cpp:1123`) upgrades `wantLiquids` on the back request only when that request is `this` and `s_visSlots[handle].gen == back.gen`. A stale back falls through to a new `EnqueueVisibility`.

Capture the snapshot when `s_visLive == 0` or `s_visSnap.valid` is clear (`WoW/Source/WorldClient/MapChunk.cpp:416`). `DropQueuedVisibility` already clears `s_visSnap.valid` when nothing remains (`WoW/Source/WorldClient/MapChunk.cpp:399`). That condition becomes `s_visLive == 0`.

### Flush

`FlushQueuedVisibility` (`WoW/Source/WorldClient/MapChunk.cpp:514`):

1. If `s_visLive == 0`, `s_visQueue.clear()`, set `s_visSnap.valid = 0`, return. Holes must not accumulate into the next frame.
2. Copy `s_visSnap`, then set `valid = 0` (`WoW/Source/WorldClient/MapChunk.cpp:521`).
3. Walk `s_visQueue` in order. A request is live when `s_visSlots[req.handle].gen == req.gen`. Do not read `req.chunk` on a miss — `Purge` drops the queue and `FreeChunk` recycles the object (`WoW/Source/WorldClient/MapChunk.cpp:95`, `WoW/Source/WorldClient/Map.cpp:3115`).
4. For each live request, in that same order, write one `VisJob` into `s_visJobs` and count liquids by walking `m_liquidChunkLinkList` as today (`WoW/Source/WorldClient/MapChunk.cpp:542`). Set `s_visSlots[handle].queued = 0`.
5. `s_visLiquids.clear()` then `resize(liquidTotal)`. Fill liquid pointers in job order (`WoW/Source/WorldClient/MapChunk.cpp:552`).
6. `ParallelFor` over `s_visJobs.size()` only (`WoW/Source/WorldClient/MapChunk.cpp:578`). `RunChunkVisibility` is unchanged (`WoW/Source/WorldClient/MapChunk.cpp:452`).
7. Link chunks and liquids in job order (`WoW/Source/WorldClient/MapChunk.cpp:580`).
8. `s_visQueue.clear()`, `s_visJobs.clear()`, `s_visLiquids.clear()`, `s_visLive = 0`.

The set of jobs that reach `LinkChunkBucket` / `LinkLiquidBucket` is the set that remains after today's erase loop (`WoW/Source/WorldClient/MapChunk.cpp:390`). Order of that set is queue order, so `LinkToTail` into a distance bucket stays in enqueue order.

## 2. Doodad-cull arena

Owner: `s_doodadCulls` in `WoW/Source/WorldClient/World.cpp:3905`. Already file-scope. Already `clear()` then `push_back` (`WoW/Source/WorldClient/World.cpp:3925`, `WoW/Source/WorldClient/World.cpp:3957`). `clear()` keeps capacity. Leave it that way.

| Name | Element | Lifetime | Capacity |
| --- | --- | --- | --- |
| `s_doodadCulls` | `DoodadCull` (`WoW/Source/WorldClient/World.cpp:3899`) | Process. One `clear()` per `CullDoodadsExterior` call. | High-water of one exterior chunk's doodad list, not the sum of the frame. `CullChunks` calls this once per chunk (`WoW/Source/WorldClient/World.cpp:4065`). |

`DoodadCull` fields stay `def`, `inFrustum`, `occluded`. The walk on the main thread fills them. `CullDoodadRange` (`WoW/Source/WorldClient/World.cpp:3907`) only writes `inFrustum` and `occluded` through the pointer passed to `ParallelFor` (`WoW/Source/WorldClient/World.cpp:3966`). No `push_back` on a worker. No second vector. No `shrink_to_fit`.

Unloaded doodads stay off this vector. They still take the barrier and `continue` (`WoW/Source/WorldClient/World.cpp:3938`).

## 3. Cancels

Same membership means the same set of survivors the flush already acts on. Visibility keeps survivor order (sort buckets). Owned fills may reorder; the set of chunks that pass the existing filters (`WoW/Source/WorldClient/MapRenderChunk.cpp:264`) stays the same.

### Visibility — generation stamp

`DropQueuedVisibility` (`WoW/Source/WorldClient/MapChunk.cpp:390`) does not scan and does not `erase`.

While the chunk is still alive (`CMapChunk::Purge` calls this first, `WoW/Source/WorldClient/MapChunk.cpp:95`):

- If `s_visSlots[handle].queued` is 0, return.
- `s_visSlots[handle].gen++`
- `s_visSlots[handle].queued = 0`
- `s_visLive--`
- If `s_visLive == 0`, `s_visSnap.valid = 0`

That is O(1). A chunk with one queued request (the `AddMapChunk` / `CullAndBucketChunk` pair) dies in one increment. The slot stays in `s_visQueue` until the flush skips it. Do not read the chunk pointer after `Purge` returns.

Do not reuse `CMapDoodadDef::m_cullPassStamp` / `CWorldScene::s_cullPassStamp` (`WoW/Source/WorldClient/MapDoodadDef.h:25`, `WoW/Source/WorldClient/World.h:199`). That stamp is the doodad pass id, incremented in `CWorldSceneRender.cpp:105` and `CWorldSceneRender.cpp:139`.

Swap-with-last is the wrong cancel here. It reorders survivors, and `LinkChunkBucket` / `LinkLiquidBucket` `LinkToTail` in walk order (`WoW/Source/WorldClient/MapChunk.cpp:427`, `WoW/Source/WorldClient/MapChunk.cpp:436`).

### Owned fills — swap-with-last

`s_ownedFills` (`WoW/Source/WorldClient/MapRenderChunk.cpp:148`) stays a file-scope `std::vector<CMapRenderChunk*>`. A chunk is queued at most once (`QueueOwnedFill` bails on a match, `WoW/Source/WorldClient/MapRenderChunk.cpp:236`).

Append one scratch field after `m_batch` (`WoW/Source/WorldClient/MapRenderChunk.h:144`). Do not insert it above `/* 0090 */`.

| Field | Type | Meaning |
| --- | --- | --- |
| `m_fillSlot` | `uint32_t` | Index in `s_ownedFills`, or `npos` (`~uint32_t{0}`) when the chunk is not queued. |

`CMapRenderChunk::CMapRenderChunk` sets `m_fillSlot = npos` (`WoW/Source/WorldClient/MapRenderChunk.cpp:706`). The object is a free-list node (`WoW/Source/WorldClient/Map.cpp:3207`), not a `CMapChunk` heap slot. Existing offset comments through `m_batch` stay.

`QueueOwnedFill`: if `m_fillSlot != npos`, return. Otherwise `push_back` and store `size() - 1`. The linear scan (`WoW/Source/WorldClient/MapRenderChunk.cpp:236`) goes away; the set stays one entry per chunk.

`CancelOwnedFill` (`WoW/Source/WorldClient/MapRenderChunk.cpp:206`), called from the destructor while the object is alive (`WoW/Source/WorldClient/MapRenderChunk.cpp:785`):

- If `m_fillSlot == npos`, return.
- Let `slot = m_fillSlot` and `last = size() - 1`.
- Move `s_ownedFills[last]` into `slot`. If that pointer is a different chunk, set its `m_fillSlot = slot`.
- `pop_back()`.
- `chunk->m_fillSlot = npos`.

O(1). The pointer is gone before `FreeRenderChunk` placement-news the node (`WoW/Source/WorldClient/Map.cpp:3217`). The multiset of remaining pointers equals the multiset after today's mid-vector `erase`.

`ClearOwnedFills` (`WoW/Source/WorldClient/MapRenderChunk.cpp:216`) walks the vector, sets each `m_fillSlot = npos`, then `clear()`. `UpdatePools` uses this when pools are dirty (`WoW/Source/WorldClient/MapRenderChunk.cpp:809`). Leaving a stale index would make the next destructor swap the wrong slot.

`FlushOwnedFills` (`WoW/Source/WorldClient/MapRenderChunk.cpp:245`):

- Empty queue: return.
- No device: return without clearing (`WoW/Source/WorldClient/MapRenderChunk.cpp:253` swaps back today; in-place return keeps the same set and the same capacity).
- `s_ownedJobs` is a file-scope `std::vector<OwnedFill>`. `OwnedFill` fields stay (`WoW/Source/WorldClient/MapRenderChunk.cpp:140`). `clear()` then `push_back` the same filtered jobs as today.
- `ParallelFor` / overlap fallback are unchanged (`WoW/Source/WorldClient/MapRenderChunk.cpp:317`, `WoW/Source/WorldClient/MapRenderChunk.cpp:333`). Overlap is still pairwise (`RangesOverlap`, `WoW/Source/WorldClient/MapRenderChunk.cpp:150`). Swap-with-last can change the order of that fallback; the set of chunks locked and filled does not change.
- After the jobs run, set every remaining `m_fillSlot = npos` and `s_ownedFills.clear()`. `s_ownedJobs.clear()`.

| Name | Element | Lifetime | Capacity |
| --- | --- | --- | --- |
| `s_ownedFills` | `CMapRenderChunk*` | Process. `clear()` after a real flush. `ClearOwnedFills` also `clear()`s. | High-water of chunks queued between flushes. |
| `s_ownedJobs` | `OwnedFill` | Process. `clear()` each flush. | High-water of jobs that passed the buffer filter. |

## Thresholds that stay

Scratch does not feed these. Do not edit the predicates, the scales, or the pass stamp.

| Predicate | Site |
| --- | --- |
| Chunk frustum accept is `Cull(bbox) != 0`; `rejected` starts at 1 | `WoW/Source/WorldClient/MapChunk.cpp:458` |
| Nearest-corner distance into `m_distToCamera` | `WoW/Source/WorldClient/MapChunk.cpp:460` |
| Liquids skipped when `!wantLiquids` or `liquidCount == 0` | `WoW/Source/WorldClient/MapChunk.cpp:474` |
| Liquid group rejected when bounds are inverted or `Cull(liquidBounds) == 0` | `WoW/Source/WorldClient/MapChunk.cpp:481` |
| Per-liquid `Cull(box) == 0` skips; corner z clamps to `[bottom, top]` around `camZ` | `WoW/Source/WorldClient/MapChunk.cpp:494` |
| Chunk bucket scale `0.029999999f`, link only if `bucket < 64` | `WoW/Source/WorldClient/MapChunk.cpp:431` |
| Liquid bucket scale `0.03f`, `bucket >= 64` returns | `WoW/Source/WorldClient/MapChunk.cpp:441` |
| Link only when `linkChunk && !rejected` | `WoW/Source/WorldClient/MapChunk.cpp:581` |
| Fade: `m_fadeLevel < fadeLevel`, then `header.m_flags & 0x8` breaks, else `continue` | `WoW/Source/WorldClient/World.cpp:3930` |
| Unloaded doodad: `!m_model` or missing `MAPOBJ_FLAG_PREPARED` → `AddBarrier(box, 10.0f)` and `continue` | `WoW/Source/WorldClient/World.cpp:3938` |
| `m_cullPassStamp == s_cullPassStamp` skips; otherwise stamp and set `m_culled = 1` | `WoW/Source/WorldClient/World.cpp:3946` |
| Sphere test: `FrustumCull(center, radius)` (`Cull(sphere) == 0` means outside) | `WoW/Source/WorldClient/World.cpp:3912`, `WoW/Source/WorldClient/World.cpp:2047` |
| Occluded when `QueryBuffer(center, radius, 0x10) >= 2` | `WoW/Source/WorldClient/World.cpp:3916` |
| Parallel doodads only at `size() >= 32` | `WoW/Source/WorldClient/World.cpp:3965` |
| `inFrustum` clears `m_culled`; `inFrustum && !occluded` adds the model | `WoW/Source/WorldClient/World.cpp:3974` |
| Animate when `m_staticFlags & 0x400` or distance squared `< 100` | `WoW/Source/WorldClient/World.cpp:3986` |
| Exterior chunk gate: frustum, `!QueryVolumes(&sphere)`, `!QueryBuffer(&bbox, 0)` | `WoW/Source/WorldClient/World.cpp:4064` |
| Occluder list only when `Enable_Culling` and `index < 63`; hole bucket scale `0.029999999f` | `WoW/Source/WorldClient/World.cpp:4056`, `WoW/Source/WorldClient/World.cpp:4096` |

## Slices

1. **Visibility arena.** File-scope `s_visJobs`, `s_visLiquids`, `s_visSlots`. `FlushQueuedVisibility` stops swapping with a local and stops constructing `jobs(n)` / `liquids(n)` (`WoW/Source/WorldClient/MapChunk.cpp:519`, `WoW/Source/WorldClient/MapChunk.cpp:524`, `WoW/Source/WorldClient/MapChunk.cpp:549`).
2. **Doodad-cull arena.** Leave `s_doodadCulls` file-scope and `clear()`-only. No new vector.
3. **Cancels.** Generation stamp in `DropQueuedVisibility`. Swap-with-last in `CancelOwnedFill` via `m_fillSlot`. `FlushOwnedFills` uses file-scope `s_ownedJobs` and `clear()` on `s_ownedFills`.

## Budget check

On a steady outdoor camera, after the first spike of each vector:

- `FlushQueuedVisibility`, `CullDoodadsExterior`, and `FlushOwnedFills` do not allocate.
- `DropQueuedVisibility` and `CancelOwnedFill` are O(1).
- Linked chunks, linked liquids, doodads that enter the model scene, and owned fills that get buffers are the same sets as before the arenas.
