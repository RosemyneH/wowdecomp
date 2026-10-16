# Campaign 5 — Counter

Scratch squad. **Counters only.** No behavior change. No `reserve`, no swap-erase, no generation stamp.

Measurements sit behind Campaign 0’s compile-out flag (default off). When the flag is off, these four functions stay as they are: no clock reads, no atomics. When it is on, add the fields below to that one-second console line and zero them with it. Field names stay as written. Leave Campaign 0’s visibility-flush sample alone.

Timers are `OsGetAsyncTimeClocks` (`CLOCK_MONOTONIC` on Linux, `Engine/Source/Os/Linux/TimeManager.cpp:59`). Each zone starts at the opening brace and stops on every return. Clocks are a sum of deltas. Atomics are relaxed. The main thread owns all four functions; the worker callbacks do not allocate this scratch.

`std::vector` heap traffic is `::operator new` → `SMemAlloc` (`Engine/Source/Storm/Source/SMem.cpp:115`). Count that request. A sized constructor `std::vector<T>(n)` is one malloc when `n > 0`, of `n * sizeof(T)` bytes. A `push_back` is one malloc when `capacity()` changes, of `new_capacity * sizeof(T)` bytes (the new buffer, not the delta). `clear`, `swap`, and `erase` do not allocate. Do not count `operator delete` / `SMemFree`.

## Zones

| Field | Function | Span |
| --- | --- | --- |
| `scratch_vis_flush_calls`, `scratch_vis_flush_clocks` | `CMapChunk::FlushQueuedVisibility` | `MapChunk.cpp:514` through the empty return at `MapChunk.cpp:516` and the end at `MapChunk.cpp:594` |
| `scratch_doodad_cull_calls`, `scratch_doodad_cull_clocks` | `CWorldScene::CullDoodadsExterior` | `World.cpp:3924` through the empty return at `World.cpp:3961` and the end at `World.cpp:3992` |
| `scratch_cancel_fill_calls`, `scratch_cancel_fill_clocks` | `CancelOwnedFill` | `MapRenderChunk.cpp:206`–`214` |
| `scratch_drop_vis_calls`, `scratch_drop_vis_clocks` | `DropQueuedVisibility` | `MapChunk.cpp:390`–`402` |

`Job::ParallelFor` at `MapChunk.cpp:578` and `World.cpp:3966` stays inside those two zones. No counters inside `RunChunkVisibility` or `CullDoodadRange`.

## Mallocs

| Field | What increments it |
| --- | --- |
| `scratch_vis_flush_mallocs`, `scratch_vis_flush_malloc_bytes` | `std::vector<VisJob> jobs(requests.size())` at `MapChunk.cpp:524`. One malloc per flush that passes the empty check (`requests.size() >= 1`). Plus `std::vector<LiquidOut> liquids(liquidTotal)` at `MapChunk.cpp:549` when `liquidTotal > 0`. |
| `scratch_doodad_cull_mallocs`, `scratch_doodad_cull_malloc_bytes` | `s_doodadCulls.push_back` at `World.cpp:3957`, once per call whose `capacity()` changes. |
| `scratch_cancel_fill_mallocs`, `scratch_cancel_fill_malloc_bytes` | `s_ownedFills.erase` at `MapRenderChunk.cpp:209`, once per erase whose `capacity()` changes. |
| `scratch_drop_vis_mallocs`, `scratch_drop_vis_malloc_bytes` | `s_visQueue.erase` at `MapChunk.cpp:393`, once per erase whose `capacity()` changes. |

`requests.swap(s_visQueue)` at `MapChunk.cpp:520` is not a malloc. `s_doodadCulls.clear()` at `World.cpp:3925` is not a malloc and keeps capacity. The next `s_visQueue.push_back` is `EnqueueVisibility` at `MapChunk.cpp:424`, outside these four functions, and stays off these counters.

## Queue length

Max over the second. Sample `size()` before any erase or swap in that call.

| Field | Vector | Sample |
| --- | --- | --- |
| `scratch_vis_queue_len` | `s_visQueue` (`MapChunk.cpp:388`) | `FlushQueuedVisibility` entry, `MapChunk.cpp:515`, before the swap. This is the length `jobs` is constructed with at `MapChunk.cpp:524`. |
| `scratch_doodad_cull_len` | `s_doodadCulls` (`World.cpp:3905`) | After the refill loop, `World.cpp:3960`. |
| `scratch_owned_fill_len` | `s_ownedFills` (`MapRenderChunk.cpp:148`) | `CancelOwnedFill` entry, `MapRenderChunk.cpp:207`. |
| `scratch_drop_vis_queue_len` | `s_visQueue` (`MapChunk.cpp:388`) | `DropQueuedVisibility` entry, `MapChunk.cpp:391`. |

## Where they run

| Function | Callers |
| --- | --- |
| `FlushQueuedVisibility` | `CMapRenderChunk::PurgeChunkBufBlocks` `MapRenderChunk.cpp:671`, `CMapRenderChunk::UpdatePools` `MapRenderChunk.cpp:806` |
| `CullDoodadsExterior` | `CWorldScene::CullChunks` `World.cpp:4065`, once per surviving chunk |
| `CancelOwnedFill` | `CMapRenderChunk::~CMapRenderChunk` `MapRenderChunk.cpp:786` |
| `DropQueuedVisibility` | `CMapChunk::Purge` `MapChunk.cpp:95` |

## Outdoor reading

Before either arena lands:

- Each non-empty visibility flush mallocs once for `jobs`, and once more for `liquids` when `liquidTotal > 0`. That pair does not go flat while the camera is steady.
- `s_doodadCulls` mallocs while a chunk’s prepared doodad count sets a new capacity, then `scratch_doodad_cull_mallocs` stays 0 for as long as later chunks fit. `scratch_doodad_cull_len` can stay non-zero on those later calls.
- `scratch_cancel_fill_mallocs` and `scratch_drop_vis_mallocs` stay 0. Cancel and drop cost shows up as clocks against `scratch_owned_fill_len` and `scratch_drop_vis_queue_len`, because each mid-vector `erase` shifts the tail.

Campaign budget these counters close against: the flush, doodad-cull, and cancel paths do not call `malloc` after the first spike, and cancel membership updates are O(1). Same chunk membership after the flush. Parallel visibility results, sort-bucket linking, frustum and occlusion thresholds, and the barrier around doodads that are not loaded yet stay as they are.
