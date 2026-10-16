# Campaign 3 — Edges

Terrain multipass (`QueueMultiPass` / `FlushMultiPass`). Slim capture, persistent scratch, and texture grouping have to keep the batch gate and the per-chunk light snapshot. `GxTerrainDraw` stays the 448-byte GPU record (`CGxDeviceVulkan.cpp:20`, `lightPos` at `CGxDeviceVulkan.cpp:23`).

The layout line “one light block per flush” fails the light-enable edge below. The other five edges are holds on the current gate.

## Empty batch

`FlushMultiPass` returns when `s_terrainBatch` is empty (`MapRenderChunk.cpp:2560`). That return is before `IRsSync`, the layer walk, the shadow walk, and `clear`.

`FlushMultiPass` runs at the end of every shader pass and every layer bucket (`World.cpp:3728`), including buckets that never queued:

- Shader-on buckets take `RenderSinglePassVertexPixelShader` / `RenderSinglePassPixelShader` (`World.cpp:3704`).
- Any other `s_renderLayersFunc` draws immediately (`World.cpp:3713`).
- A bucket whose chunks all fail the gate draws them in `RenderMultiPassAlpha` and leaves the vector empty (`MapRenderChunk.cpp:2537`). Their shadows already ran inside that call (`MapRenderChunk.cpp:2724`).

An empty flush binds nothing, draws nothing, and uploads nothing. Batch size for that call is 0.

Scratch that keeps capacity (`clear` + `reserve`) still takes this return. The empty call does not `shrink_to_fit`, does not rebuild `used` / `draws`, and does not reuse `used` marks from the previous bucket. `clear` at `MapRenderChunk.cpp:2655` is what makes the next bucket empty; the early return depends on that.

A non-empty vector whose chunks have no layers is not this case. `maxLayers == 0` skips the layer loop (`MapRenderChunk.cpp:2576`) and still runs the shadow loop (`MapRenderChunk.cpp:2637`).

## One chunk

The pair walk starts at `j = i` (`MapRenderChunk.cpp:2587`), so a one-entry batch is one group of one `GxTerrainDraw`. `DrawTerrainBatch` is called with `count == 1` (`MapRenderChunk.cpp:2625`).

That count still goes through the Vulkan indirect path (`CGxDeviceVulkan.cpp:1502`). `count == 1` is a real batch: one `GpuDrawItem`, one indirect command (`CGxDeviceVulkan.cpp:1516`). A failed alloc returns 0 (`CGxDeviceVulkan.cpp:1513`) and the fallback draws that one capture with `DrawTerrainChunk` (`MapRenderChunk.cpp:2630`). D3D and any other device hit the base `DrawTerrainBatch`, which returns 0 (`CGxDevice.cpp:146`), so a one-chunk bucket always takes `DrawTerrainChunk` (`MapRenderChunk.cpp:2516`).

Keep, for that single record:

- Layer 0 stays `GxBlend_Opaque`; later layers stay `GxBlend_Alpha` (`MapRenderChunk.cpp:2619`).
- The shadow draw still runs after the layers (`MapRenderChunk.cpp:2637`).
- `indexCount`, `firstIndex`, and `baseVertex` stay the capture (`MapRenderChunk.cpp:2553`). `indexCount` is `m_batch.m_count`, which holes shrink in `CreateIndices` (`MapChunk.cpp:882`). `baseVertex` is the pool slot (`vertex->m_index / m_itemSize`), not a reset to 0.
- `used` is cleared at the start of each layer (`MapRenderChunk.cpp:2577`). A stale mark on the only slot skips the chunk.

One `TerrainCapture` can still be a merged pair. Vertices for `m_mapChunks[1]` are written at 145 (`MapRenderChunk.cpp:168`). Its indices append with `baseVertex = m_maxIndex + 1` (`MapChunk.cpp:884`), which is 145 when the first chunk used every vertex. The batch stores one index range covering both (`MapRenderChunk.cpp:192`). The slim record keeps that range; it does not assume 145 vertices or 768 indices (`MapRenderChunk.cpp:64`).

A one-chunk flush leaves scratch capacity where the previous outdoor flush put it.

## Flags 0x40 and 0x80

Any layer with `m_flags & (0x40 | 0x80)` sets `special` and the whole chunk takes `RenderMultiPassAlpha` (`MapRenderChunk.cpp:2531`). The chunk is not split: layers without those bits on that chunk stay off the batch too. The same `special` bit is also set by `m_renderFlags & 0x4` (`MapRenderChunk.cpp:2529`), the specular path from layer flag `0x400` (`MapRenderChunk.cpp:587`).

`0x80` turns lighting off for that layer and restores it afterward (`MapRenderChunk.cpp:2684`, `MapRenderChunk.cpp:2715`). `CreateLayer` also sets `m_renderFlags |= 0x1` (`MapRenderChunk.cpp:583`), which only changes the sort variant (`World.cpp:4080`). The batch gate is the layer-flag test, not that render flag.

`0x40` is a per-layer tex0 translate. The direction is `m_flags & 7`, the rate is `s_texAnimRate[(m_flags >> 3) & 7]` (`MapRenderChunk.cpp:2688`, rates at `MapRenderChunk.cpp:70`), and tex0 is restored after the draw (`MapRenderChunk.cpp:2719`). The capture holds one `tex0` / `tex1` per chunk (`MapRenderChunk.cpp:2547`). An animated layer cannot share that pair.

Those chunks already draw their own shadow (`MapRenderChunk.cpp:2724`). They must not appear again in the flush shadow loop.

## Missing vertex pool

A chunk enters the batch only with both buffers on `s_gxVertexPool` and `s_gxIndexPool`, a non-zero vertex `m_itemSize`, an aligned `m_index`, and index `m_itemSize == 2` (`MapRenderChunk.cpp:2537`).

The miss path:

- `AllocBuf` returns null when `s_chunkBufBlockFreeList` is empty (`MapRenderChunk.cpp:601`).
- `RenderPrep` leaves `m_chunkBuf` null (`MapRenderChunk.cpp:992`).
- `RenderSetup` then calls `UseStreamingBufs` (`MapRenderChunk.cpp:930`), which locks `BufStream` buffers (`MapRenderChunk.cpp:889`, `CGxDevice.cpp:1062`). Those are `m_streamBufs`, not `s_gxVertexPool`. `UseStreamingBufs` does not assign `m_chunkBuf`.
- `QueueMultiPass` sees a null vertex pointer (`MapRenderChunk.cpp:2527`) and calls `RenderMultiPassAlpha` while the stream bind from `RenderSetup` is still current.

Flush reads `cap.chunk->m_chunkBuf->m_vertexBuf` with no null check (`MapRenderChunk.cpp:2622`, `MapRenderChunk.cpp:2646`). That is safe only while the gate holds. A streamed chunk in the vector crashes. A stream pool that fails the identity check stays on the immediate path even if some later edit stores it on `m_chunkBuf`.

`vertex->m_pool != s_gxVertexPool` is true when the terrain pool exists and the buf’s pool does not. It is false when both pointers are null. A null pool is still a miss: reject it on the way in. `DrawTerrainBatch` already bails on a null index pool (`CGxDeviceVulkan.cpp:1503`) and the fallback `Draw` bails the same way (`CGxDeviceVulkan.cpp:1451`), so a null-pool capture draws nothing.

## Light enable changes mid-flush

Each chunk calls `SetupGxLights` inside `RenderSetup` before it is queued (`MapRenderChunk.cpp:926`). That enables slot 0 (`M2Light.cpp:329`), then the point lights for that chunk’s sphere (`M2Light.cpp:352`), and disables the rest (`M2Light.cpp:357`). Point lights are chosen per sphere (`M2Scene.cpp:868`, `M2Light.cpp:216`) and capped at four (`M2Light.h:58`). Two chunks in one bucket can enable different slots.

`CaptureLights` runs at that moment (`MapRenderChunk.cpp:2548`). Disabled slots stay zero because of the `memset` and the `m_enable` skip (`MapRenderChunk.cpp:2464`, `MapRenderChunk.cpp:2472`). Enabled slots are written with `lightDiffuse[i][3] = 1`. The shader skips a slot whose diffuse `w` is 0 (`Ffp.vert:98`) and, for `gpuMode == 2`, uses the per-draw record rather than the uniform block (`Ffp.vert:70`). `DrawTerrainBatch` copies those slots into `GpuDrawItem` (`CGxDeviceVulkan.cpp:1521`).

`FlushMultiPass` never calls `LightEnable`. By the time it runs, `m_lights` is whatever the last `SetupGxLights` in the bucket wrote (`CGxDevice.cpp:798`). `LightEnable(0)` clears `m_enable` and leaves the color payload in the slot (`CGxDevice.cpp:803`). A flush-time read of `m_lights` paints the last chunk’s point lights onto every draw, and a copy that skips the enable test plus the memset keeps a disabled slot alive.

The per-chunk snapshot stays. A single block for the flush is valid only when every capture’s four slots compare equal. The fallback `Draw` path rebuilds uniforms from the live device (`CGxDeviceVulkan.cpp:1002`) and does not see the snapshot; that is the D3D / no-indirect path. The Vulkan batch path keeps the queue-time slots.

`0x80` chunks toggle `GxRs_Lighting` on the immediate path (`MapRenderChunk.cpp:2684`) and restore it before the next queue (`MapRenderChunk.cpp:2715`). `flags.x` in the uniform is that render state at `Draw` time (`CGxDeviceVulkan.cpp:1002`), shared by every `DrawTerrainBatch` in the flush. Leaving `0x80` chunks on the immediate path keeps that toggle out of the flush.

## More than four layers

`m_layers` is four entries (`MapRenderChunk.h:140`). The render loop buckets layers `1..4` (`World.cpp:3691`) into `m_renderChunkList[24]` (`SortTable.h:21`). `CanMergeChunkLayers` refuses a merged count above 4 (`MapChunk.cpp:1656`). Shader tables match that cap (`MapRenderChunk.h:81`).

`m_nLayers` on disk is a `uint32_t` (`MapChunk.h:166`). `CreateLayer` increments `m_layersCount` with no clamp (`MapRenderChunk.cpp:568`, `MapRenderChunk.cpp:591`). Past 4, that write lands on `m_terrainBlendTexture`. `FlushMultiPass` then sets `maxLayers` from `m_layersCount` and indexes `m_layers[layer]` (`MapRenderChunk.cpp:2566`, `MapRenderChunk.cpp:2584`).

The layer walk stops at 4, and at `m_layersCount` when that is smaller (`MapRenderChunk.cpp:2580`). A chunk with fewer layers than `maxLayers` stays out of that pass. Grouping does not invent a fifth pass, and it does not grow `lightPos[4]` to mean a fifth texture layer. Those four slots are lights (`CGxDevice.h:76`).

Alpha order stays layer 0, then later layers (`MapRenderChunk.cpp:2619`).

## Verdict

| Edge | Result |
| --- | --- |
| Empty batch | Hold the early return. No device work, no scratch shrink. |
| One chunk | Hold one-draw groups, the D3D fallback, holes, and the dual-chunk index range. |
| `0x40` / `0x80` | Hold the whole-chunk eject onto `RenderMultiPassAlpha`. |
| Missing vertex pool | Hold the pool-identity gate. Null pool stays out. |
| Light enable mid-flush | Block a flush-scoped light read. Keep the queue-time snapshot. |
| More than four layers | Hold the cap at `m_layers[4]`. |
