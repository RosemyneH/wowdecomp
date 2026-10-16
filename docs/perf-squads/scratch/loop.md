# Campaign 5 — Scratch loop

**NEXT SLICE**

Cycle 1. No writer slice has landed. No frame log is in tree, so this is a code reading, not a before/after sample.

No-gain close does not apply. Faith has not rejected the arena.

The outdoor frame that misses the budget: `CGWorldFrame::RenderWorld` runs `OnWorldUpdate` then `OnWorldRender` (`WoW/Source/Ui/WorldFrame.cpp:2217-2219`). Update calls `CWorld::Update` (`WorldFrame.cpp:2184`). `prepareAll` starts true (`WoW/Source/WorldClient/World.cpp:1550`) and `CMap::PrepareUpdate` (`World.cpp:1567`) reaches `CMapArea::Update`, which enqueues every in-rect chunk (`WoW/Source/WorldClient/MapArea.cpp:258-260`). Render then calls `CMapRenderChunk::UpdatePools` (`WoW/Source/WorldClient/CWorldSceneRender.cpp:92`), which flushes at `WoW/Source/WorldClient/MapRenderChunk.cpp:806`. A steady outdoor camera still takes that path. The empty return at `WoW/Source/WorldClient/MapChunk.cpp:515` stays on a frame that just enqueued.

## Budget

Malloc after the first outdoor spike, and cancel cost.

| Line | Result | Code |
| --- | --- | --- |
| `jobs` stays allocated after the first spike | Open | Sized constructor every non-empty flush |
| `liquids` stays allocated after the first spike | Open | Sized constructor whenever `liquidTotal > 0` |
| `s_doodadCulls` stays allocated after the first spike | Holds | `clear` keeps capacity |
| Cancel is O(1) | Open | Mid-vector `erase` on both queues |

### Visibility flush

`CMapChunk::FlushQueuedVisibility` (`MapChunk.cpp:514`) still builds three heap buffers and frees them before return.

`std::vector<VisRequest> requests` (`MapChunk.cpp:519`) is automatic. `requests.swap(s_visQueue)` (`MapChunk.cpp:520`) leaves `s_visQueue` at capacity 0. The destructor frees the buffer that `EnqueueVisibility` grew. The next frame's `push_back` (`MapChunk.cpp:424`) allocates again from empty. `AddMapChunk` pushes every in-rect chunk (`MapChunk.cpp:600`). `CullAndBucketChunk` usually only sets `wantLiquids` on that tail (`MapChunk.cpp:1123-1124`). The high-water mark never sticks.

`std::vector<VisJob> jobs(requests.size())` (`MapChunk.cpp:524`) runs only after the empty return, so `requests.size()` is at least 1. That constructor allocates on every flush.

`std::vector<LiquidOut> liquids(liquidTotal)` (`MapChunk.cpp:549`) allocates when `liquidTotal > 0`. A dry flush skips it. Any queued chunk with `wantLiquids` and a liquid list takes it. The outdoor enqueue sets `wantLiquids` on the same request (`MapChunk.cpp:1123-1124`).

`Job::ParallelFor` (`MapChunk.cpp:578`) publishes into the existing pool (`Engine/Source/Base/Job/JobSystem.cpp:132-163`). It is not the malloc.

### Doodad cull

`s_doodadCulls` is already file-scope (`World.cpp:3905`). `CullDoodadsExterior` `clear`s it (`World.cpp:3925`) and `push_back`s (`World.cpp:3957`). `clear` leaves `capacity()` unchanged, and nothing calls `shrink_to_fit`. `push_back` allocates only when `size() == capacity()`.

Each call clears, and `CullChunks` calls it once per surviving chunk (`World.cpp:4065`), so the high-water mark is the largest single `m_doodadDefLinkList`, not the frame sum. After that chunk has been seen, later exterior culls do not allocate. This line already holds. The next writer leaves `World.cpp:3905-3991` alone, including the unloaded-doodad barrier (`World.cpp:3938-3943`), `FrustumCull` (`World.cpp:3912`), and the occlusion test `QueryBuffer(...) >= 2` (`World.cpp:3916`).

### Cancel

`DropQueuedVisibility` (`MapChunk.cpp:390-397`) and `CancelOwnedFill` (`MapRenderChunk.cpp:206-213`) `erase` at `MapChunk.cpp:393` and `MapRenderChunk.cpp:209`. `vector::erase` moves the tail. One hit is linear in the suffix. The scan walks the whole vector, and a repeated pointer erases again without a cursor bump. That is not O(1).

## Next writer task

**Visibility arena.** One concern. `MapChunk.cpp` only. After this lands, a steady outdoor flush does not allocate `s_visQueue`, `jobs`, or `liquids` once the queued-chunk count and `liquidTotal` have each reached their high-water mark.

1. Next to `s_visQueue` (`MapChunk.cpp:388`), add file-scope `s_visJobs` and `s_visLiquids` in the same anonymous namespace.
2. Delete the automatic `requests` vector and the swap (`MapChunk.cpp:519-520`). Capture `n = s_visQueue.size()`, copy `s_visSnap` and clear `s_visSnap.valid` as today (`MapChunk.cpp:521-522`), and read the prefix `s_visQueue[0, n)`. After the link loop, `erase` that prefix. `erase` of the prefix keeps capacity. An append during the flush stays queued, which is the membership the swap has now. `clear()` of the whole queue would drop that append.
3. Replace the sized constructors (`MapChunk.cpp:524`, `MapChunk.cpp:549`) with `s_visJobs.resize(n)` and `s_visLiquids.resize(liquidTotal)` before `ParallelFor`. `resize` at or under capacity does not allocate. `RunChunkVisibility` keeps writing through `batch.jobs` and `batch.liquids`.
4. `s_visJobs.clear()` and `s_visLiquids.clear()` before every return that follows the resize. `clear` keeps capacity and ends the `CMapChunk*` / `CChunkLiquid*` lifetimes. `PurgeChunkBufBlocks` flushes and then destroys buffers (`MapRenderChunk.cpp:671`). `Purge` drops the queue and then frees the chunk (`MapChunk.cpp:95-99`). The scratch must not hold those pointers past the flush return.
5. Leave `DropQueuedVisibility` on `erase` (`MapChunk.cpp:393`). Leave `RunChunkVisibility`, the `ParallelFor` at `MapChunk.cpp:578`, and the bucket links after it (`MapChunk.cpp:580-592`). `LinkChunkBucket` (`MapChunk.cpp:427`) and `LinkLiquidBucket` (`MapChunk.cpp:436`) stay after the parallel section. A new comment uses the TextEmoji form.

`CullAndBucketChunk`'s `back().chunk == this` test (`MapChunk.cpp:1123-1124`) still sees enqueue order. This slice does not reorder `s_visQueue`.

## Later slice

Cancels stay the following writer slice. `DropQueuedVisibility` gets a generation stamp, not swap-with-last: swap-with-last changes `back()`, so `CullAndBucketChunk` (`MapChunk.cpp:1123-1124`) would miss the coalesce and enqueue the same chunk twice. A live queue that becomes empty still clears `s_visSnap.valid` (`MapChunk.cpp:399-401`). `CancelOwnedFill` gets swap-with-last: `QueueOwnedFill` matches by pointer (`MapRenderChunk.cpp:236-239`) and `FlushOwnedFills` consumes the set (`MapRenderChunk.cpp:250-251`). That same swap may keep `s_ownedFills` capacity. It does not rewrite the lock, the overlap test, or `FillLockedBufs`.

The doodad vector is not a later slice. It already holds.
