# Scratch integrator

Campaign 5. One writer at a time. Replay onto the integration branch in the order below. A hunk that edits a function this squad does not own does not land, even when it is in a file we also edit.

## Merge order

1. **Terrain** closes first. Group D: campaign 3 finishes before campaign 4 starts.
2. **Remesh** lands on that tree. `CreateIndices` and `AllocBuf` only.
3. **Visibility arena.** `FlushQueuedVisibility` keeps capacity on `s_visQueue`, `jobs`, and `liquids`.
4. **Doodad-cull arena.** `CullDoodadsExterior` keeps capacity on `s_doodadCulls`.
5. **Cancels** last. `DropQueuedVisibility` and `CancelOwnedFill` in one slice, on top of both arenas and on top of remesh's `FillLockedBufs`.

Slices 3–5 are this squad. Steps 1–2 are other squads; we rebase onto them, we do not edit them.

## Who owns which lines

| Squad | Functions | Lines |
| --- | --- | --- |
| Scratch | `FlushQueuedVisibility` | `WoW/Source/WorldClient/MapChunk.cpp:514` |
| Scratch | `DropQueuedVisibility` | `WoW/Source/WorldClient/MapChunk.cpp:390` |
| Scratch | `CullDoodadsExterior` | `WoW/Source/WorldClient/World.cpp:3924` |
| Scratch | `CancelOwnedFill` | `WoW/Source/WorldClient/MapRenderChunk.cpp:206` |
| Remesh | `CreateIndices` (byte sink and batch wrapper) | `WoW/Source/WorldClient/MapChunk.cpp:302`, `WoW/Source/WorldClient/MapChunk.cpp:882` |
| Remesh | `CreateIndices` calls inside `FillLockedBufs` | `WoW/Source/WorldClient/MapRenderChunk.cpp:189`, `WoW/Source/WorldClient/MapRenderChunk.cpp:193` |
| Remesh | `AllocBuf` | `WoW/Source/WorldClient/MapRenderChunk.cpp:596` |
| Terrain | `QueueMultiPass`, `FlushMultiPass` | `WoW/Source/WorldClient/MapRenderChunk.cpp:2526`, `WoW/Source/WorldClient/MapRenderChunk.cpp:2559` |
| Terrain | multipass call sites | `WoW/Source/WorldClient/World.cpp:3711`, `WoW/Source/WorldClient/World.cpp:3728` |

On a conflict, the owning squad's side wins for that function. Scratch keeps `MapChunk.cpp:390`–`594` and `MapChunk.cpp:1122`–`1128`, `MapRenderChunk.cpp:206`–`214` and the `s_ownedFills` capacity at `MapRenderChunk.cpp:250`, and `World.cpp:3905`–`3991`. Remesh keeps the index writes at `MapRenderChunk.cpp:189` and `:193` even though they sit in the same anonymous namespace as `CancelOwnedFill`.

## Why this order

Terrain and remesh share `MapRenderChunk.cpp`. The plan already sequences them: multipass first, remesh only after that campaign closes. Scratch is group C and can be written in parallel, but the integrator replays it after both, because the owned ranges are adjacent:

- `CreateIndices` ends at `MapChunk.cpp:338`. The visibility namespace starts at `MapChunk.cpp:340`.
- Remesh's index calls are `MapRenderChunk.cpp:189` and `:193`. `CancelOwnedFill` begins at `MapRenderChunk.cpp:206`.
- Terrain's flush is the bottom of the same file (`MapRenderChunk.cpp:2443`–`2655`) and the `RenderChunksSinglePass` calls at `World.cpp:3711` and `:3728`. `CullDoodadsExterior` is later in that file, at `World.cpp:3924`.

`FillLockedBufs` is the seam. `FlushOwnedFills` (`MapRenderChunk.cpp:333`) calls `FillOwnedRange`, which calls `FillLockedBufs` (`MapRenderChunk.cpp:202`). Remesh may stop writing hole-free indices there. Cancel only decides which chunks are still in `s_ownedFills` when that flush runs. Land the index change first, then the membership change.

Inside scratch, the arenas come before cancel:

- `FlushQueuedVisibility` swap-constructs `jobs` at `MapChunk.cpp:524` and `liquids` at `MapChunk.cpp:549`, and `requests.swap(s_visQueue)` at `MapChunk.cpp:519` drops the queue's capacity every flush. The arena replaces that. `DropQueuedVisibility` still erases in the middle until slice 5, so it keeps compiling against either queue.
- `CullDoodadsExterior` is a different vector (`s_doodadCulls` at `World.cpp:3905`, `clear` at `World.cpp:3925`). It does not read `s_visQueue`. It is second so `World.cpp` is touched after terrain's call sites are already in the file.
- Cancel is last because it changes who is still in those vectors. `Purge` drops visibility (`MapChunk.cpp:95`) and then frees the render chunk, whose destructor calls `CancelOwnedFill` (`MapRenderChunk.cpp:786`). One slice covers both erases.

## Frame the slices have to match

`CWorldScene::Render` (`WoW/Source/WorldClient/CWorldSceneRender.cpp:92`) calls `UpdatePools`, then later culls, then draws.

1. `UpdatePools` (`MapRenderChunk.cpp:805`) calls `FlushQueuedVisibility` (`MapRenderChunk.cpp:806`), then `ClearOwnedFills` or `FlushOwnedFills` (`MapRenderChunk.cpp:808`–`811`). A pool rebuild destroys chunks and runs `CancelOwnedFill`.
2. `CullSortTable` (`World.cpp:4219`) calls `CullChunks`, which calls `CullDoodadsExterior` (`World.cpp:4065`).
3. `RenderChunks` (`CWorldSceneRender.cpp:197`) reaches `QueueMultiPass` / `FlushMultiPass`. `RenderSetup` flushes owned fills again at `MapRenderChunk.cpp:911`.

`PurgeChunkBufBlocks` also flushes visibility before it destroys buffers (`MapRenderChunk.cpp:671`). Persistent `jobs` and `liquids` are capacity only. They must not hold a `CMapChunk*` or `CChunkLiquid*` past the return of `FlushQueuedVisibility`.

## Slice 5 mechanism

`DropQueuedVisibility` uses a generation stamp, not swap-with-last. `CullAndBucketChunk` coalesces liquids only when `s_visQueue.back().chunk == this` (`MapChunk.cpp:1123`–`1124`). Swap-with-last changes that tail, so the same chunk is enqueued twice and `wantLiquids` diverges before `FlushQueuedVisibility` runs. A stamp leaves order and `back()` alone. An empty live queue still clears `s_visSnap.valid` (`MapChunk.cpp:399`–`401`).

`CancelOwnedFill` uses swap-with-last. `QueueOwnedFill` matches by pointer across the whole vector (`MapRenderChunk.cpp:236`–`239`), and `FlushOwnedFills` consumes the set with `pending.swap(s_ownedFills)` (`MapRenderChunk.cpp:250`). Order is not observed. That swap still throws the capacity away; slice 5 may keep it on `s_ownedFills` without rewriting the lock, the overlap test, or `FillLockedBufs`.

Do not reuse `m_cullPassStamp` (`World.cpp:3946`). That stamp is how `CullDoodadsExterior` skips a doodad already handled this pass. `CullDoodadsInterior` (`World.cpp:3996`) stays the serial path.

## Leave these alone

- Hole masks, frustum and occlusion tests, sort-bucket linking (`MapChunk.cpp:427` and `:436`), and the unloaded-doodad barrier (`World.cpp:3938`–`3943`).
- `RenderPrep`'s `AllocBuf` call (`MapRenderChunk.cpp:993`) and the streaming miss in `RenderSetup` (`MapRenderChunk.cpp:930`–`934`). Remesh owns that miss.
- `s_terrainBatch`, `used`, and `draws` (`MapRenderChunk.cpp:2461`, `:2572`, `:2573`). Terrain owns that flush.
- `FlushOwnedFills`'s `BufLock` / `ParallelFor` body (`MapRenderChunk.cpp:264`–`345`), except the `s_ownedFills` capacity swap at `:250`.
