# Campaign 5 — Accountant

LP64 (pointer 8). No Campaign 0 log is applied here. Sizes are the `SMemAlloc` argument: `::operator new` is `SMemAlloc` (`Engine/Source/Storm/Source/SMem.cpp:115`, `:41`). `SMemAlloc` rounds the request up to a multiple of 8 (`SMem.cpp:42`). Every sizeof below is already a multiple of 8, so the rounded size is the request.

**Q** is `s_visQueue.size()` at the start of a non-empty `FlushQueuedVisibility`. That is `requests.size()` after the swap, and the length `jobs` is constructed with.

**L** is `liquidTotal`: the sum of `CChunkLiquid` nodes on jobs with `wantLiquids` set.

**D** is the number of `s_doodadCulls.push_back` calls in one `CullDoodadsExterior`. That is one chunk’s submitted doodads. The vector is cleared at the start of every chunk, so the retained capacity is the fattest chunk since launch, not the sum across the frame.

## VisJob

`WoW/Source/WorldClient/MapChunk.cpp:359-368`. `C3Vector` is three floats, 12 bytes, alignment 4 (`Engine/Source/Tempest/C3Vector.h:24-26`).

| Field | Offset | Bytes |
| --- | ---: | ---: |
| `chunk` | 0 | 8 |
| `linkChunk` | 8 | 4 |
| `wantLiquids` | 12 | 4 |
| `rejected` | 16 | 4 |
| `dist` | 20 | 4 |
| `corner` | 24 | 12 |
| `liquidBegin` | 36 | 4 |
| `liquidCount` | 40 | 4 |
| tail pad | 44 | 4 |
| **sizeof** | | **48** |

The pointer forces alignment 8. `liquidCount` ends at 44, so the tail pad is 4. One job is **48** bytes.

## LiquidOut

`WoW/Source/WorldClient/MapChunk.cpp:370-374`

| Field | Offset | Bytes |
| --- | ---: | ---: |
| `liquid` | 0 | 8 |
| `visible` | 8 | 4 |
| `corner` | 12 | 12 |
| **sizeof** | | **24** |

`corner` ends on the alignment boundary. No tail pad. One liquid result is **24** bytes.

## DoodadCull

`WoW/Source/WorldClient/World.cpp:3899-3903`

| Field | Offset | Bytes |
| --- | ---: | ---: |
| `def` | 0 | 8 |
| `inFrustum` | 8 | 4 |
| `occluded` | 12 | 4 |
| **sizeof** | | **16** |

## Bytes allocated every visibility flush

`CMapChunk::FlushQueuedVisibility` (`MapChunk.cpp:514`) returns before any of these vectors exist when `s_visQueue` is empty (`MapChunk.cpp:515-516`). A flush that continues does this:

| Vector | Construction | `SMemAlloc` argument | Lifetime |
| --- | --- | ---: | --- |
| `jobs` | `std::vector<VisJob> jobs(requests.size())` at `MapChunk.cpp:524` | **48Q** | Freed when `FlushQueuedVisibility` returns |
| `liquids` | `std::vector<LiquidOut> liquids(liquidTotal)` at `MapChunk.cpp:549` | **24L** when L > 0 | Freed when `FlushQueuedVisibility` returns |

`std::vector<T>(n)` requests `n * sizeof(T)` and sets `capacity()` to `n`. `n == 0` does not call `operator new`. Q is at least 1 on this path, so `jobs` is one malloc of **48Q** on every non-empty flush. `liquids` is a second malloc of **24L** only when the setup loop counted at least one liquid (`MapChunk.cpp:542-546`). A dry flush has L = 0 and skips that malloc.

The legacy MCLQ walk adds at most four `CChunkLiquid` nodes per chunk (`MapChunk.cpp:1040-1071`, `i < 4`). MH2O adds `chunkDesc->m_layerCount` more (`MapChunk.cpp:1085-1109`). L is that sum over the Q jobs whose `wantLiquids` is set. The vector is sized before `RunChunkVisibility`, so a chunk the worker rejects still occupies its `VisJob` and its liquid slots.

Q counts chunks, one `VisJob` each. `AddMapChunk` pushes a request with `wantLiquids == 0` (`MapChunk.cpp:598-601`, `EnqueueVisibility` at `MapChunk.cpp:415-424`). `CullAndBucketChunk` sees that same chunk at the back of the queue and sets `wantLiquids = 1` (`MapChunk.cpp:1122-1125`). The outdoor pair is `MapArea.cpp:259-260`, so each enqueued chunk is one 48-byte job with liquids requested. `InsideFrustumRect` is tested on the depth-2 rect (`Map.cpp:1756-1757`, `MapArea.cpp:83-84`, `:258`), a 4×4. Every in-`gbChunkRect` chunk of an overlapping 4×4 is queued, and each one is in the **48Q** allocation.

The allocating flush on a steady frame is `CMapRenderChunk::UpdatePools` (`MapRenderChunk.cpp:806`), called from `CWorldScene::Render` (`CWorldSceneRender.cpp:92`). `PurgeChunkBufBlocks` also flushes (`MapRenderChunk.cpp:671`) at the start of `CMap::PrepareUpdate` (`Map.cpp:2306`), before `PreUpdateAreas` enqueues. That call sees an empty queue and returns at `MapChunk.cpp:515-516`.

Both buffers die with the function. The next frame with the same Q and L calls `SMemAlloc(48Q)` and, when L > 0, `SMemAlloc(24L)` again.

## s_doodadCulls

`s_doodadCulls` is file-scope (`World.cpp:3905`). `CullDoodadsExterior` starts with `clear()` (`World.cpp:3925`) and `push_back`s one `DoodadCull` per submitted doodad (`World.cpp:3953-3957`). Callers: once per surviving chunk from `CullChunks` (`World.cpp:4065`).

`clear()` erases elements and leaves `capacity()` as it was. `push_back` calls `operator new` only when `size() == capacity()`. On the libstdc++ this tree links, that growth is `size + max(size, 1)` elements: an empty vector’s first `push_back` allocates **16** bytes, and a full vector doubles. The `SMemAlloc` argument is `new_capacity * 16`. The previous buffer is freed. The buffer that stays is the last one.

For a chunk that submits D doodads onto a smaller capacity, the climb is the power-of-two ladder. The capacity that remains is the smallest power of two greater than or equal to D:

| D that crosses a full buffer | New capacity | `SMemAlloc` bytes |
| ---: | ---: | ---: |
| 1 | 1 | 16 |
| 2 | 2 | 32 |
| 3 | 4 | 64 |
| 5 | 8 | 128 |
| 9 | 16 | 256 |
| 17 | 32 | 512 |
| 33 | 64 | 1024 |
| 2^k + 1 | 2^(k+1) | 16 × 2^(k+1) |

Resident bytes after that chunk are **16 × 2^ceil(log2(D))** (for D = 1, 16). A later chunk with a submitted count at or under that capacity pays the `clear()` and the `push_back`s inside the existing buffer. `operator new` is not called. A fatter chunk doubles once, and that new size is the capacity the following chunks reuse.

D is the filtered count, not `SMChunk::m_nDoodadRefs` (`MapChunk.h:167`). The loop skips a doodad below the fade band (`World.cpp:3930`), stops the list when that skip hits a map whose header has `0x8` (`World.cpp:3931-3932`), sends an unprepared doodad to the barrier (`World.cpp:3938-3943`), and skips a doodad already stamped this pass (`World.cpp:3946-3948`). The `push_back` at `World.cpp:3957` is the only growth site. It finishes before `ParallelFor` (`World.cpp:3965-3966`), so the buffer pointer the workers read is stable for the rest of the call.

## Why clear-and-keep-capacity removes the malloc after the first spike

`clear()` destroys `[begin, end)` and sets the size to 0. It does not change the end-of-storage pointer, so `capacity()` is the same value on the next statement. The next fill calls `operator new` only when the new length is greater than that capacity.

`s_doodadCulls` already has that shape: file-scope vector, `clear()` at `World.cpp:3925`, `push_back` at `World.cpp:3957`. The first time a chunk’s submitted count climbs the ladder above, each doubling is one `SMemAlloc` of `new_capacity * 16`. The last of those is the spike. Every later `CullDoodadsExterior` whose D fits in that capacity does not call `malloc`. A steady outdoor camera revisits chunks at or under that peak, so `scratch_doodad_cull_mallocs` stays 0 while `scratch_doodad_cull_len` stays non-zero.

`jobs` and `liquids` allocate on every non-empty flush because the vector object does not survive the function. `jobs(Q)` is one `SMemAlloc(48Q)` (`MapChunk.cpp:524`). `liquids(L)` is one `SMemAlloc(24L)` when L > 0 (`MapChunk.cpp:549`). Both destructors run at `MapChunk.cpp:594` and `SMemFree` the buffers. The following frame starts from capacity 0 and requests the same byte counts again. Same Q, same L, another pair of mallocs. That is the traffic that stays on a steady camera.

Give each of them the `s_doodadCulls` lifetime: file-scope (or thread-local; the main thread owns the flush), `clear()`, then `resize` to this flush’s count.

- The first flush whose Q exceeds capacity does one `SMemAlloc(48Q)` and keeps it. `clear()` has set `size()` to 0, and a `resize(Q)` from a zero size requests Q elements, so the spike is **48Q** bytes, not a doubled buffer. The same rule makes the liquids spike **24L**.
- A later flush with a count at or under that capacity `clear()`s and `resize`s inside the existing storage. `operator new` is not called. The steady outdoor camera is this case.
- A later flush one element past the peak is a new spike of exactly the new count (`48 * (Q+1)` or `24 * (L+1)`), and the previous buffer is freed. The capacity that remains is that new exact count. Doubling happens when `resize` grows a vector whose `size()` is still the old length. `clear()` before the `resize` is what keeps the spike at `n * sizeof(T)`.

`resize` on a full vector, without `clear()`, grows by `size + max(size, added)`. A 200-element `VisJob` buffer (`capacity == 200`, 9600 bytes) resized to 201 without `clear()` allocates 400 elements, **19200** bytes. `clear()` then `resize(201)` allocates 201 elements, **9648** bytes.

Resident after the outdoor spike, until some later flush exceeds it:

| Scratch | Resident bytes |
| --- | ---: |
| `jobs` | **48 × Qpeak** |
| `liquids` | **24 × Lpeak** |
| `s_doodadCulls` | **16 × 2^ceil(log2(Dpeak))** |

The swap at `MapChunk.cpp:519-520` is the opposite lifetime for the queue. `VisRequest` (`MapChunk.cpp:342-346`) is a pointer plus two `int32_t`s, **16** bytes, alignment 8. `requests` is an automatic vector. The swap hands it the buffer `s_visQueue` grew during `EnqueueVisibility` (`MapChunk.cpp:424`), and the destructor at the end of the flush frees that buffer. `s_visQueue` is left at capacity 0. The next frame’s `push_back`s climb the same power-of-two ladder, `16 × 2^k` per step, until capacity covers Q. A file-scope queue that `clear()`s, and is not swapped into a temporary that dies, keeps that **16 × 2^ceil(log2(Qpeak))** buffer. Enqueue then fills it with no `malloc` while Q stays at or under the peak.
