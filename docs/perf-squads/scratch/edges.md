# Campaign 5 edges — cull scratch

Capacity stays on `s_visQueue`, the visibility `jobs` / `liquids` buffers, `s_doodadCulls`, and `s_ownedFills`. These five cases are membership and lifetime. A warm buffer with size 0 is an empty queue. A dead request that still holds a pointer is not empty.

## Empty queue

Each flush returns before `ParallelFor`, before a snapshot capture, and without releasing capacity.

| Branch | What runs |
| --- | --- |
| `WoW/Source/WorldClient/MapChunk.cpp:515` | `FlushQueuedVisibility` returns when `s_visQueue.empty()`. No swap, no `jobs` / `liquids` construction, no write to `s_visSnap`. |
| `WoW/Source/WorldClient/MapChunk.cpp:416` | The next `EnqueueVisibility` captures because the queue is empty or `s_visSnap.valid` is clear. |
| `WoW/Source/WorldClient/MapChunk.cpp:1123` | `CullAndBucketChunk` reads `back()` only when the queue is non-empty. Empty falls through to `EnqueueVisibility(this, 0, 1)` at `:1128`. |
| `WoW/Source/WorldClient/MapRenderChunk.cpp:246` | `FlushOwnedFills` returns when `s_ownedFills.empty()`. No `BufLock`, no `ParallelFor`. |
| `WoW/Source/WorldClient/World.cpp:3925` | `CullDoodadsExterior` `clear()`s `s_doodadCulls` before the link walk. `clear` keeps capacity. |
| `WoW/Source/WorldClient/World.cpp:3960` | Size 0 returns before the `>= 32` split at `:3965`. |
| `Engine/Source/Base/Job/JobSystem.cpp:133` | `ParallelFor(0)` returns before it publishes `g_pool.fn`. The empty returns above are what keep a reused result buffer from being walked again. |

`s_visQueue.empty()` is size, not capacity. After a generation stamp, size can be non-zero while every request is dead. That state takes the empty path: no `ParallelFor`, no `LinkChunkBucket` (`MapChunk.cpp:432`), no `LinkLiquidBucket` (`:449`). `RunChunkVisibility` reads `chunk->m_bbox` at `MapChunk.cpp:460` for any job with `linkChunk` set. A dead request must be dropped before that read.

`CullDoodadsExterior` clears even when this chunk's list is empty. Skipping that `clear` leaves the previous chunk's defs in `s_doodadCulls`, misses the return at `:3960`, and runs the result loop at `:3971` on them again.

## Cancel of the last item

`CMapArea::PurgeChunk` calls `Purge` and then `CMap::FreeChunk` (`WoW/Source/WorldClient/MapArea.cpp:39`–`:40`). `FreeChunk` runs the destructor and returns the slot to the heap (`WoW/Source/WorldClient/Map.cpp:3115`–`:3118`). The render-chunk destructor cancels, then `FreeRenderChunk` pushes the same address onto `s_mapRenderChunkFreeList` (`Map.cpp:3224`–`:3227`). Pointer equality is identity after that.

| Branch | What runs |
| --- | --- |
| `WoW/Source/WorldClient/MapChunk.cpp:95` | `Purge` drops visibility before `FreeRenderChunk` (`:97`), before liquids are freed (`:107`), and before `m_sortListLink.Unlink` (`:117`). |
| `WoW/Source/WorldClient/MapChunk.cpp:390` | `DropQueuedVisibility` erases every request whose `chunk` matches, including a one-element queue. The index stays put on erase, so a duplicate that slides into the hole is removed in the same call. |
| `WoW/Source/WorldClient/MapChunk.cpp:399` | When nothing remains, `s_visSnap.valid = 0`. A queue that still has some other chunk leaves the flag set. |
| `WoW/Source/WorldClient/MapChunk.cpp:1123` | Coalesce is `back().chunk == this`, then `wantLiquids = 1`. `AddMapChunk` and `CullAndBucketChunk` run back to back (`WoW/Source/WorldClient/MapArea.cpp:259`–`:260`), so the chunk just pushed is the tail. |
| `WoW/Source/WorldClient/MapRenderChunk.cpp:206` | `CancelOwnedFill` erases every matching pointer. `QueueOwnedFill` already refuses a second copy (`:236`). The last item is one `pop`. |
| `WoW/Source/WorldClient/MapRenderChunk.cpp:785` | The destructor cancels before `FreeBuf` (`:787`). The pointer is gone before the free-list link at `Map.cpp:3227`. |

Popping the tail does not reorder the survivors. Swap-with-last of any earlier index does: the old tail moves into the hole, and `back()` changes. A later `CullAndBucketChunk` on the chunk that used to be the tail then pushes a second request (`linkChunk == 0`, `wantLiquids == 1`) instead of setting `wantLiquids` on the one `AddMapChunk` already queued. A stamp that leaves order and `back()` alone avoids that.

A stamp that leaves `chunk` set does not. `FreeChunk` can recycle that address before the next flush. `CullAndBucketChunk` then treats the dead tail as the new object, sets `wantLiquids`, and returns without pushing. The same alias makes `QueueOwnedFill` skip a recycled `CMapRenderChunk*`. Last-item cancel clears the pointer (erase or null) before `Purge` returns, and a null tail fails the `back().chunk == this` test.

When the cancelled request was the last live one, `s_visSnap.valid` clears even if dead slots keep `size() > 0`. Otherwise the next `EnqueueVisibility` sees a non-empty queue and a valid flag (`MapChunk.cpp:416`) and keeps the pre-cancel frustum. `FlushQueuedVisibility` copies `s_visSnap` without reading `valid` (`:521`). The flag's job is to force the next capture, and the empty live set is what clears it.

## Cancel during parallel fill

`FlushOwnedFills` swaps `s_ownedFills` into a local vector before anyone locks (`MapRenderChunk.cpp:250`–`:251`). `CancelOwnedFill` only walks `s_ownedFills` (`:207`). After that swap the live queue is empty, and the in-flight set is `jobs`.

| Branch | What runs |
| --- | --- |
| `WoW/Source/WorldClient/MapRenderChunk.cpp:290` | `BufLock` runs on the main thread while building `jobs`, before `ParallelFor`. |
| `WoW/Source/WorldClient/MapRenderChunk.cpp:333` | `ParallelFor` runs `FillOwnedRange` (`:198`), which calls `FillLockedBufs` (`:202`). |
| `WoW/Source/WorldClient/MapRenderChunk.cpp:335` | Unlock and `m_hasData = 1` run only after `ParallelFor` returns. |
| `WoW/Source/WorldClient/MapChunk.cpp:519` | Visibility does the same split: `requests.swap(s_visQueue)` before `ParallelFor` at `:578`. `DropQueuedVisibility` scans `s_visQueue` only. |
| `Engine/Source/Base/Job/JobSystem.cpp:145` | One published `fn` / `user`. The caller runs ranges (`:156`) and waits until workers drop that publish (`:158`). |
| `WoW/Source/WorldClient/MapChunk.cpp:968` | `CreateVertices` writes the locked span. It does not purge, cancel, or flush. |

A cancel that arrives after the swap and before the unlock loop does not remove the job. The worker still fills `jobs[i].chunk`, and the unlock still runs. Pulling that job out from under `FillOwnedRange` leaves a lock without the unlock at `:335`, or writes a buffer `FreeBuf` already released.

That window is main-thread-busy: the caller is inside `RunRanges` or the wait, and `FillLockedBufs` does not re-enter `~CMapRenderChunk`. Keep it that way. `FillOwnedRange`, `FillLockedBufs`, and `RunChunkVisibility` do not call `CancelOwnedFill`, `DropQueuedVisibility`, `FlushOwnedFills`, or `FlushQueuedVisibility`. A nested `ParallelFor` from a worker replaces `g_pool.fn` while the outer wait still expects the old publish.

The in-flight batch is the membership of this flush. A stamp or swap-erase applied to `s_ownedFills` or `s_visQueue` lands on the next flush. Workers do not read that stamp, and they do not swap-erase `jobs`. The link loop at `MapChunk.cpp:580` consumes the swapped `jobs` array, not a live stamp updated after the swap.

The null-device path swaps the pending vector back (`MapRenderChunk.cpp:253`–`:255`) before any lock. That restore is the whole set. A cancel in that gap still only sees `s_ownedFills` after the swap-back.

## Doodad not loaded yet

`MAPOBJ_FLAG_PREPARED` is set from `CM2Model::IsLoaded` (`WoW/Source/WorldClient/Map.cpp:6398`–`:6401`). Until that flag is set, the exterior cull records a barrier and does not touch the scratch vector.

| Branch | What runs |
| --- | --- |
| `WoW/Source/WorldClient/World.cpp:3930` | `m_fadeLevel < fadeLevel` continues, or `break`s when `CMap::header.m_flags & 0x8` (`:3931`). That test is before the barrier. A fade reject does not call `AddBarrier`. |
| `WoW/Source/WorldClient/World.cpp:3938` | `!m_model` or missing `MAPOBJ_FLAG_PREPARED` is the not-loaded arm. |
| `WoW/Source/WorldClient/World.cpp:3940` | The box is `m_model->m_shared->m_boundingBox`, transformed by `m_mat` (`:3941`). |
| `WoW/Source/WorldClient/World.cpp:3942` | `s_barrier.AddBarrier(box, 10.0f)`. Radius 10 is the distance test in `CBarrier::AddBarrier` (`WoW/Source/WorldClient/Barrier.cpp:208`). |
| `WoW/Source/WorldClient/World.cpp:3943` | `continue`. No `m_cullPassStamp` write, no `m_culled = 1`, no `s_doodadCulls.push_back`. |
| `WoW/Source/WorldClient/World.cpp:3946` | The stamp skip applies only to defs that got past the barrier. An unloaded def does not consume `s_cullPassStamp`, so a second list still barriers it. |
| `WoW/Source/WorldClient/World.cpp:3965` | The parallel threshold is `s_doodadCulls.size()` after the filter. Unloaded defs do not count toward 32. |
| `WoW/Source/WorldClient/World.cpp:3987` | After the join, a frustum miss calls `m_model->SetAnimating`. That is the loaded arm. |
| `WoW/Source/WorldClient/World.cpp:3635` | `CullDoodads` has the same barrier and stays serial. |
| `WoW/Source/WorldClient/World.cpp:4004` | `CullDoodadsInterior` has the same barrier and stays serial. It still writes interior fog flags at `:4038`. |

The barrier runs on the main thread, before `ParallelFor` (`:3966`). `CullDoodadRange` (`:3907`) only reads spheres and the occlusion buffer. An unloaded def in `s_doodadCulls` both skips `AddBarrier` and reaches `SetAnimating`.

A chunk whose defs are all unloaded leaves the vector empty, hits `:3960`, and still has the barriers from the walk. `clear()` at `:3925` is what makes that vector empty; it does not replace the walk.

`s_cullPassStamp` increments once per render (`WoW/Source/WorldClient/CWorldSceneRender.cpp:105` or `:139`). That is the doodad once-per-pass skip. A cancel generation is a different counter.

## Two flushes in one frame

`CGWorldFrame::RenderWorld` runs update, then render (`WoW/Source/Ui/WorldFrame.cpp:2217`–`:2218`).

| Order | Call | Flush |
| --- | --- | --- |
| 1 | `OnWorldUpdate` → `CWorld::Update` (`WorldFrame.cpp:2184`, `WoW/Source/WorldClient/World.cpp:1567`) → `CMap::PrepareUpdate` | `PurgeChunkBufBlocks` → `FlushQueuedVisibility` (`WoW/Source/WorldClient/Map.cpp:2306`, `WoW/Source/WorldClient/MapRenderChunk.cpp:671`) |
| 2 | Same `PrepareUpdate`, after that flush | `AddMapChunk` + `CullAndBucketChunk` (`MapArea.cpp:259`–`:260`) fill `s_visQueue` and capture `s_visSnap` |
| 3 | `OnWorldRender` → `CWorld::Render` (`WorldFrame.cpp:1842`) → `CWorldScene::Render` | `UpdatePools` → `FlushQueuedVisibility` again (`CWorldSceneRender.cpp:92`, `MapRenderChunk.cpp:806`) |
| 4 | Same `UpdatePools` | `ClearOwnedFills` or `FlushOwnedFills` (`MapRenderChunk.cpp:808`–`:811`) |
| 5 | Later in that render, `RenderSetup` | `FlushOwnedFills` again (`MapRenderChunk.cpp:911`) |

`OnWorldRender` returns before `CWorld::Render` when the window or the NDC viewport is degenerate (`WorldFrame.cpp:1780`–`:1785`). Flush 3 does not run. The requests and the snapshot from step 2 stay until the next frame's flush 1. That flush uses the captured `s_visSnap` (`MapChunk.cpp:521`). An end-of-frame reset of the queue or the snapshot has to live inside `CWorldScene::Render`, which this path did not enter.

When both visibility flushes run, each one takes only the requests swapped out of that call (`MapChunk.cpp:519`) and sets `s_visSnap.valid = 0` (`:522`) before its own `ParallelFor`. Enqueues between them recapture (`:416`). Flush 2 does not append to flush 1's `jobs` or `liquids`, and `liquidBegin` starts from a cursor of 0 (`:550`). `LinkToTail` unlinks first (`Engine/Source/Storm/H/stpl.h:483`–`:484`), so a replayed chunk moves buckets instead of corrupting the list. Replaying is still a second test under a second snapshot. The two swaps are disjoint.

`ParallelFor` is not reentrant (`JobSystem.cpp:145`). Flush 2 publishes only after flush 1's wait returns. File-scope `jobs` and `liquids` are reusable after that wait and after the link loop. They do not keep a `CMapChunk*` or `CChunkLiquid*` after `FlushQueuedVisibility` returns. The empty flush (usually flush 1, once the previous render already drained the queue) hits `:515` and leaves capacity alone. It does not `ParallelFor` the previous flush's `jobs`.

Owned fills match that shape. `RenderPrep` queues during cull (`MapRenderChunk.cpp:999`), which is after `UpdatePools` and before `RenderSetup`. The `UpdatePools` flush consumes what the previous frame left. The `RenderSetup` flush consumes this frame's queues. A second `RenderSetup` in the same frame sees an empty `s_ownedFills` and returns at `:246` without dropping capacity. `m_hasData` / `m_valid` then make `QueueOwnedFill` (`:232`) and the job builder (`:276`) skip a chunk the first flush already finished.

A null `g_theGxDevicePtr` on the first owned flush swaps the set back (`:253`). The second flush still sees those chunks. Clearing on that path drops the fill.
