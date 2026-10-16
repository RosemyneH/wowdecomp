# Campaign 5 — Leak

**Open.** Kept `capacity` needs a ceiling, or a teleport pins the worst flush for the rest of the process. Generation stamps have one clearer each; a cancel stamp is cleared by the flush that publishes membership.

## Kept capacity

| Scratch | Declaration | What happens to `capacity` |
| --- | --- | --- |
| `s_doodadCulls` | `WoW/Source/WorldClient/World.cpp:3905` | `clear()` at `World.cpp:3925` keeps it for the process |
| `s_visQueue` | `WoW/Source/WorldClient/MapChunk.cpp:388` | `FlushQueuedVisibility` swaps it into a local at `MapChunk.cpp:519-520`; the local dies with the function |
| `jobs`, `liquids` | `MapChunk.cpp:524`, `MapChunk.cpp:549` | sized to that flush, freed on return |
| `s_ownedFills` | `WoW/Source/WorldClient/MapRenderChunk.cpp:148` | `FlushOwnedFills` swaps it into `pending` at `MapRenderChunk.cpp:250-251`; `jobs` is built at `MapRenderChunk.cpp:260` |

`CullDoodadsExterior` refills `s_doodadCulls` once per visible chunk from `CWorldScene::CullChunks` (`World.cpp:4065`), and `CullChunks` runs for every sort bucket inside `CWorldScene::CullSortTable` (`World.cpp:4218-4219`). `capacity` becomes the densest chunk since launch. A teleport through a city, then back to the outdoor camera, leaves that peak resident.

The visibility and owned-fill vectors get the same rule when they move to file scope and start keeping `capacity`. The outdoor spike may allocate. It must not stay.

**Ceiling.** One named cap per vector, sized to the steady outdoor flush (the budget scene). A frame whose `size()` is above the cap may grow. The next frame whose `size()` is at or under the cap releases the excess, down to the cap, with a swap into a buffer reserved at the cap (`shrink_to_fit` is allowed to do nothing). The steady camera then stays off `malloc`. The teleport peak does not.

Release once per frame, after that frame's last use:

- `s_doodadCulls` — end of `CWorldScene::Render` (`WoW/Source/WorldClient/CWorldSceneRender.cpp:149`), after both branches have returned from `CullSortTable` (`CWorldSceneRender.cpp:133`, `CWorldSceneRender.cpp:148`) and before `CullMapObjDefGroup` (`CWorldSceneRender.cpp:151`). A check inside `CullDoodadsExterior` runs between chunks of the same pass (`World.cpp:4218-4219`) and reallocates on the next dense chunk. The `sortTable.Clear()` path (`CWorldSceneRender.cpp:130`) still reaches this release, so an indoor frame after the teleport drops the peak too.
- `s_visQueue`, `jobs`, `liquids` — end of `CMapChunk::FlushQueuedVisibility` (`MapChunk.cpp:514`). That flush runs once per frame from `CMapRenderChunk::UpdatePools` (`MapRenderChunk.cpp:806`), called by `Render` at `CWorldSceneRender.cpp:92`.
- `s_ownedFills` and its `jobs` — end of `FlushOwnedFills` (`MapRenderChunk.cpp:245`). The device-null path swaps the queue back (`MapRenderChunk.cpp:253-254`) and uses the same cap.

## Who clears generation stamps

| Stamp | Who clears it | Who only bumps it |
| --- | --- | --- |
| `CWorldScene::s_cullPassStamp` (`World.h:199`) | `CWorldScene::Initialize` — `WoW/Source/WorldClient/WorldScene.cpp:112` (`s_cullPassStamp = 0`) | `CWorldScene::Render` — `CWorldSceneRender.cpp:105` (viewer) and `CWorldSceneRender.cpp:139` (exterior) |
| `CMapDoodadDef::m_cullPassStamp` (`MapDoodadDef.h:25`) | Nobody. Constructor, purge, and destructor leave the bytes in place | Visit sites below |
| Cancel generation on `s_visQueue` / `s_ownedFills` | The flush that publishes membership (below). No stamp exists on these vectors today | — |

`CWorldScene::Destroy` (`WorldScene.cpp:77`) leaves `s_cullPassStamp` alone. The pass counter is cleared only at `WorldScene.cpp:112`.

Per-doodad writes:

- `CWorldScene::CullDoodads` — `World.cpp:3644`
- `CWorldScene::CullDoodadsExterior` — skip when equal at `World.cpp:3946`, write at `World.cpp:3950`
- `CWorldScene::CullDoodadsInterior` — `World.cpp:4013`

`CMapDoodadDef::CMapDoodadDef` (`MapDoodadDef.cpp:143`) does not store `m_cullPassStamp`. `CMapDoodadDef::Purge` (`MapDoodadDef.cpp:213`) and `~CMapDoodadDef` (`MapDoodadDef.cpp:350`) do not either. `CMap::FreeDoodadDef` (`Map.cpp:3139`) returns the slot to the doodad heap; `CMap::AllocDoodadDef` (`Map.cpp:3131`) placement-news the next occupant onto those bytes. A recycled stamp can equal the live `s_cullPassStamp`, and `CullDoodadsExterior` then skips that doodad for the pass (`World.cpp:3946`).

**Clear `m_cullPassStamp` in `CMapDoodadDef::CMapDoodadDef`** (`MapDoodadDef.cpp:143`) to `0`, the value `Initialize` publishes at `WorldScene.cpp:112`. `Render` increments before any cull (`CWorldSceneRender.cpp:105`, `CWorldSceneRender.cpp:139`), so `0` is not a live pass.

**Cancel stamp.** `VisRequest` (`MapChunk.cpp:342`) and `s_ownedFills` (`MapRenderChunk.cpp:148`) carry no generation. Cancel erases in the middle:

- `DropQueuedVisibility` — `MapChunk.cpp:390-397`, from `CMapChunk::Purge` — `MapChunk.cpp:95`
- `CancelOwnedFill` — `MapRenderChunk.cpp:206-213`, from `CMapRenderChunk::~CMapRenderChunk` — `MapRenderChunk.cpp:786`, before `FreeBuf` at `MapRenderChunk.cpp:787`

A generation used for those cancels is cleared by the flush, in the same frame, before any use of the pointer:

- `CMapChunk::FlushQueuedVisibility` (`MapChunk.cpp:514`) drops stamped-out requests before `LinkChunkBucket` (`MapChunk.cpp:583`) and `LinkLiquidBucket` (`MapChunk.cpp:590`). `s_visSnap.valid` is cleared at `MapChunk.cpp:522`, and `DropQueuedVisibility` clears it when the queue has no entries (`MapChunk.cpp:399-401`). A stamped hole is still an entry; the snapshot clear stays tied to zero live requests. `Job::ParallelFor` at `MapChunk.cpp:578` joins before the link loop, so this clear runs on the calling thread after the workers finish.
- `FlushOwnedFills` (`MapRenderChunk.cpp:245`) and `ClearOwnedFills` (`MapRenderChunk.cpp:216-217`) drop stamped-out fills before `BufLock` (`MapRenderChunk.cpp:291`, `MapRenderChunk.cpp:301`).

`CMapChunk::~CMapChunk` (`MapChunk.cpp:1839`) is empty. Chunk retire is `CMapArea::PurgeChunk` (`MapArea.cpp:39-40`) and `CMapArea::Purge` (`MapArea.cpp:61-62`): `Purge`, then `FreeChunk`. The stamp or the erase stays in `Purge`.

`FlushOwnedFills` unlocks before it returns (`MapRenderChunk.cpp:336-344`). This scratch does not hold an in-flight GPU buffer across frames. `FreeBuf` stays after `CancelOwnedFill` (`MapRenderChunk.cpp:786-787`).

## Budget

After the first outdoor spike, the three flushes stay off `malloc` on the steady camera. A teleport may allocate above the cap; the next quiet frame releases the excess back to the cap. Cancel stays O(1). The flush clears cancel stamps in that same frame, so linked buckets match an erase.
