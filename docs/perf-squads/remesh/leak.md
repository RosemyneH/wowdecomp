# Campaign 4 leak review

**Block** the shared hole-free index buffer and the `s_bufList` LRU steal until both follow the retire rules below.

Neither slice is in the tree. `AllocBuf` still returns null when both free lists are empty (`WoW/Source/WorldClient/MapRenderChunk.cpp:600`, `WoW/Source/WorldClient/MapRenderChunk.cpp:635`), and every slot still gets a private index `CGxBuf` (`WoW/Source/WorldClient/MapRenderChunk.cpp:618`, `WoW/Source/WorldClient/MapRenderChunk.cpp:654`).

A shared index and a stolen slot must not free a buffer the GPU is still reading, and the old per-chunk index buffers must be released exactly once.

## What actually frees GPU memory

`BufDestroy` deletes the CPU `CGxBuf` and unlinks it from the pool. It does not free the `VkBuffer` or the D3D buffer.

- `Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1157` `BufDestroy` unlinks and `delete`s the `CGxBuf`.
- `Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1667` `BufCreate` is what puts that object on `pool->m_bufList`.
- `Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1684` `PoolDestroy` unlinks the pool and `delete`s it. `CGxPool::~CGxPool` (`Engine/Source/Gx/CGxPool.cpp:10`) does not walk `m_bufList`. A `CGxBuf` that was not `BufDestroy`'d is leaked, and its `m_pool` dangles.

GPU memory dies only in the device `PoolDestroy`:

| Path | Where | When the GPU object dies |
| --- | --- | --- |
| Vulkan | `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1581` calls `IReleasePool` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1556`) | `RetireBuffer` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1165`) queues the `VkBuffer` on the current frame. `PrepareFrame` collects it only after that frame's fence (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:707`). `FramesInFlight` is 2 (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:131`). |
| D3D9 | `Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:341` | `IReleaseD3dPool` `Release()`s immediately (`Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:290`). |
| D3D9Ex | `Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.cpp:305` | Same immediate `Release` (`Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.cpp:254`). |

Draws bind the pool's GPU buffer, not the `CGxBuf`. Vulkan records `vkCmdBindIndexBuffer` on `pool->m_apiSpecific` and `vkCmdDrawIndexed` at `m_index >> 1` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1455`). That command buffer stays live until its frame fence.

Terrain pools are `GxPoolUsage_Dynamic` (`WoW/Source/WorldClient/MapRenderChunk.cpp:835`). A lock is a write into memory the GPU may still be reading:

- Vulkan returns `mapped + m_index` with no rename (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1649`).
- D3D9 and D3D9Ex lock dynamic pools with `D3DLOCK_NOOVERWRITE` (`Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:154`, `Engine/Source/Gx/CGxDeviceD3d9Ex/CGxDeviceD3d9Ex.cpp:152`).

`RetireBuffer` does not make that write safe. Overwriting a live range is a use of memory the GPU is still reading. A `borrowed` alloc skips retire entirely (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1166`, `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1560`). The shared index must be a real pool alloc (`borrowed == 0`) so `PoolDestroy` retires it. It must not be a borrowed view of the stream ring.

## Shared hole-free indices

One static index buffer for `m_header->m_holes == 0`, including the dual-chunk base of +145 (`WoW/Source/WorldClient/MapChunk.cpp:883`). Private indices stay only for a non-zero hole mask. One owner. One `BufDestroy`. One `PoolDestroy`.

`DestroyBufs` frees whatever pointer sits in the slot, and the stream buffer is the only pointer it skips:

- `WoW/Source/WorldClient/MapRenderChunk.cpp:429` walks every `m_indexBuf` in `s_chunkBlockArray`.
- `WoW/Source/WorldClient/MapRenderChunk.cpp:432` skips only `BufStream`.
- `WoW/Source/WorldClient/MapRenderChunk.cpp:688` `PurgeChunkBufBlocks` does the same.
- `WoW/Source/WorldClient/MapRenderChunk.cpp:642` the dual-to-single split `GxBufDestroy`s slot 0's index buf. `GxBufDestroy` (`Engine/Source/Gx/Gx.cpp:991`) also skips only the stream buffer.

Storing the shared `CGxBuf*` in every hole-free slot makes the first of those calls delete it and the next call free it again. `BufDestroy` nulls only the reference it was passed (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1168`). Other slots keep the dangling pointer. Draws already recorded against that pool are still in flight.

Required:

- Hole-free slots hold a non-owning view (draw `firstIndex` / base vertex into the one shared buffer). `DestroyBufs`, `PurgeChunkBufBlocks`, and the split at `WoW/Source/WorldClient/MapRenderChunk.cpp:641` skip that pointer the same way they skip `BufStream`.
- The single owner is `BufDestroy`'d from `CMapRenderChunk::Destroy` (`WoW/Source/WorldClient/MapRenderChunk.cpp:378`) before the pool `PoolDestroy`.
- If the shared buffer is its own pool, `Destroy` `PoolDestroy`s it once, beside `s_gxIndexPool` (`WoW/Source/WorldClient/MapRenderChunk.cpp:389`). A pool that is not on that path leaks on every `s_bPoolsDirty` rebuild. Recreating it inside `UpdatePools` without that `PoolDestroy` leaks a `VkBuffer` / D3D index buffer per rebuild.
- If it is a subrange of `s_gxIndexPool`, it dies with that pool's one `PoolDestroy`. It still needs exactly one `BufDestroy` so the CPU object is not left on `m_bufList`.
- Fill it once. The free-list reuse path clears `m_hasData` on the slot's index pointer (`WoW/Source/WorldClient/MapRenderChunk.cpp:612`, `WoW/Source/WorldClient/MapRenderChunk.cpp:629`), and `RenderPrepBufs` then locks and rewrites (`WoW/Source/WorldClient/MapRenderChunk.cpp:870`). That clear must not run on the shared buffer. A rewrite lands in the same mapped range the other in-flight frame has bound.
- Do not `PoolDestroy`, `Release`, or `vkDestroyBuffer` it from `AllocBuf`, from a steal, or per chunk. Vulkan's safe point is the existing `PoolDestroy` → `RetireBuffer` path. D3D `Release` is immediate, so a shared-buffer destroy on the hot path frees a buffer the previous frame is still drawing. A fence timeout skips `Collect` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:709`); do not destroy on that timeout.
- Staging for a one-time upload, if any, goes through `RetireBuffer` too. Campaign 6 owns device-local static upload. This slice must not `vkDestroyBuffer` the staging copy while the upload command buffer is in flight.
- `RenderSinglePass` stays on `BufStream` (`WoW/Source/WorldClient/MapRenderChunk.cpp:1021`). That is the debug line path.

A mixed dual chunk (one half holed) keeps a private index buffer. Moving it onto the shared buffer `BufDestroy`s that private buffer once first. Moving it off the shared buffer `BufCreate`s a private buffer and does not destroy the shared one.

## Old per-chunk index buffers

Private indices created today:

- Dual slot: 1536 `uint16`s at byte offset `1536 * m_vertexCount` (`WoW/Source/WorldClient/MapRenderChunk.cpp:618`).
- Single slot: 768 `uint16`s at byte offset `1536 * (m_vertexCount + i)` (`WoW/Source/WorldClient/MapRenderChunk.cpp:654`).

Those objects are reachable only from the slot pointer and from `pool->m_bufList`. The reclaim sites are `DestroyBufs` (`WoW/Source/WorldClient/MapRenderChunk.cpp:429`), `PurgeChunkBufBlocks` (`WoW/Source/WorldClient/MapRenderChunk.cpp:688`), and the split `GxBufDestroy` (`WoW/Source/WorldClient/MapRenderChunk.cpp:643`).

Nulling `m_indexBuf` to swing the slot at the shared buffer, without `BufDestroy` of the previous pointer, orphans that `CGxBuf`. `CGxDevice::PoolDestroy` will not free it. After the pool is deleted the orphan's `m_pool` is dangling. Each `s_bPoolsDirty` rebuild leaks one `CGxBuf` per hole-free slot that was dropped this way.

Required on the switch, and on every later `Destroy`:

1. `BufDestroy` each private index `CGxBuf` once while it is still the slot's pointer.
2. Then point the slot at the non-owning shared view.
3. Holed slots stay in `s_chunkBlockArray` so `DestroyBufs` still sees them.
4. Shrink `s_pnEstimateIndex` (`WoW/Source/WorldClient/MapRenderChunk.cpp:831`, today `768 * count` for every chunk) by leaving hole-free indices out of that count. The drop happens by the existing rebuild: `ClearAllRenderChunks` (`WoW/Source/WorldClient/Map.cpp:469`) then `Destroy` (`DestroyBufs`, then `PoolDestroy` of the old pool, then `PoolCreate` of the smaller one). `ClearAllRenderChunks` only `FreeBuf`s; it does not destroy. The `PoolDestroy` is what retires the old GPU allocation. Skipping it keeps the old pool alive.
5. Do not `SetDirty` every frame to chase the hole-free share. `UpdatePools` returns immediately when `s_bPoolsDirty` is clear (`WoW/Source/WorldClient/MapRenderChunk.cpp:814`). A per-frame dirty flag `PoolDestroy`s the vertex pool too, and on D3D that `Release`s buffers the GPU is still reading.

## LRU steal

`s_bufList` is already MRU-at-head. `UpdatePools` links every block at the tail (`WoW/Source/WorldClient/MapRenderChunk.cpp:845`). `RenderPrep` moves the used block to the head (`WoW/Source/WorldClient/MapRenderChunk.cpp:997`). Nothing reads the tail. On an empty free list the miss falls through to `UseStreamingBufs` (`WoW/Source/WorldClient/MapRenderChunk.cpp:930`).

Free lists the steal must not confuse with the LRU:

- `s_chunkBufBlockFreeList` (`WoW/Source/WorldClient/MapRenderChunk.cpp:85`). Dual alloc pops the head (`WoW/Source/WorldClient/MapRenderChunk.cpp:598`). Single alloc pops the tail (`WoW/Source/WorldClient/MapRenderChunk.cpp:633`). `FreeBuf` pushes a dual block at the head (`WoW/Source/WorldClient/MapRenderChunk.cpp:769`). `UpdatePools` pushes every block at the tail (`WoW/Source/WorldClient/MapRenderChunk.cpp:844`).
- `s_renderChunkBufFreeList` (`WoW/Source/WorldClient/MapRenderChunk.cpp:91`). Single alloc pops the tail (`WoW/Source/WorldClient/MapRenderChunk.cpp:622`). `FreeBuf` pushes at the head (`WoW/Source/WorldClient/MapRenderChunk.cpp:771`).

Steal only when both of those are empty. The tail of `s_bufList` is then an owned block, not a free one. It was not `RenderPrep`'d this frame (those blocks were moved to the head), so it will not draw this frame. It may have been drawn last frame, and that frame is still in flight (`FramesInFlight` is 2).

`FreeBuf` (`WoW/Source/WorldClient/MapRenderChunk.cpp:761`) is the detach to copy. It returns the slot to a free list and clears `m_renderChunk`. It does not `BufDestroy` or `PoolDestroy`. Callers that already do this are the 2-second idle drop (`WoW/Source/WorldClient/Map.cpp:1933`) and map unload. A refill after that idle is safe. A refill of last frame's tail is not.

The split at `WoW/Source/WorldClient/MapRenderChunk.cpp:641` is the wrong pattern. It `GxBufDestroy`s the block's vertex and index buffers. On a stolen live block that either double-frees the shared index (if the slot held that pointer) or deletes a `CGxBuf` whose range the previous frame is still drawing. The following `BufCreate` at the same pool offset (`WoW/Source/WorldClient/MapRenderChunk.cpp:653`) then locks and overwrites it.

Required:

- Detach the victim with the `FreeBuf` protocol: clear the old owner's `m_chunkBuf`, unlink ownership, leave the `CGxBuf`s allocated.
- Do not `BufDestroy`, `GxBufDestroy`, `PoolDestroy`, or `vkDestroyBuffer` on the steal.
- Do not `BufLock` the stolen vertex range, or a holed private index range, until that block has aged out of both in-flight frames. A frame stamp on the block is enough. Hole-free indices are the shared buffer; the steal must not lock or replace them.
- Clearing `m_hasData` is what schedules the refill (`WoW/Source/WorldClient/MapRenderChunk.cpp:610`, `WoW/Source/WorldClient/MapRenderChunk.cpp:628`). Do that only after the age-out, and only on buffers this block owns.
- `DestroyBufs` unlinks `m_bufLink` only for blocks still on `s_chunkBufBlockFreeList` (`WoW/Source/WorldClient/MapRenderChunk.cpp:406`). `~CMapChunkBufBlock` does not unlink (`WoW/Source/WorldClient/MapRenderChunk.cpp:780`). `UpdatePools` is safe because `ClearAllRenderChunks` `FreeBuf`s every owner before `SetCount(0)` (`WoW/Source/WorldClient/MapRenderChunk.cpp:441`). A stolen block that is still owned at that `SetCount` leaves `s_bufList` pointing into freed array storage. The victim must be `FreeBuf`'d before any destroy.

## Grass miss

Same free-list rule, smaller surface. A miss that skips the batch must not drop a pool slot.

- `AcquireBuffers` writes null on a miss (`WoW/Source/WorldClient/DetailDoodad.cpp:130`).
- `FreeBuffers` returns a pool pair onto `s_freeGxBufs` only while `m_vertexBuf->m_pool == s_gxVertexPool`, then nulls the batch pointers (`WoW/Source/WorldClient/DetailDoodad.h:133`).
- `FreeGxBufMat` `BufDestroy`s only what is on `s_freeGxBufs`, then `PoolDestroy`s both grass pools (`WoW/Source/WorldClient/DetailDoodad.cpp:510`). `UpdatePools` calls `ClearChunkDetailDoodads` first (`WoW/Source/WorldClient/DetailDoodad.cpp:547`, `WoW/Source/WorldClient/Map.cpp:458`) so live batches return to that free list before the pools die.

Nulling a live pool pointer without `FreeBuffers` leaks the `CGxBuf` and leaves it off the list that `FreeGxBufMat` drains. The following `PoolDestroy` retires the GPU memory under a dangling `CGxBuf`. A stream pointer may be dropped; `FreeGxBufMat` already refuses `BufStream` (`WoW/Source/WorldClient/DetailDoodad.cpp:514`). A pool pointer stays owned by the batch, or goes back onto `s_freeGxBufs`.
