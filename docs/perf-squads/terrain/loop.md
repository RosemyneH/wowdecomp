# Campaign 3 — Terrain loop

**NEXT SLICE**

Cycle 1. No writer slice has landed. The three budget lines are still open in `MapRenderChunk.cpp`.

## Budget

| Line | Result | Code |
| --- | --- | --- |
| Per-chunk capture is matrices plus draw args | Open | `TerrainCapture` is still the fat record |
| Flush scratch stops allocating after warmup | Open | `used` and `draws` are built inside every flush |
| Batch count matches the baseline | Unchanged, not yet a log match | Pairing is still the nested scan |

### Capture

`TerrainCapture` (`WoW/Source/WorldClient/MapRenderChunk.cpp:2443-2458`) is one chunk pointer, three `C44Matrix` values (192 bytes), four `float[4][4]` light blocks (256 bytes), a fog tuple (16 bytes), and the draw args `indexCount` / `firstIndex` / `baseVertex` (12 bytes). On LP64 that is 484 bytes plus 4 bytes of tail padding, **488 bytes** per chunk. The plan’s “about 468” is that record without the fog tuple.

`QueueMultiPass` fills the light blocks on every chunk (`CaptureLights` at `2548`, body `2463-2490`) and snapshots fog from the device at `2549-2552`. `RenderSetup` has already rewritten both, per chunk, via `SelectLights` / `SetupGxLights` / `SetupGxFog` (`923-927`) before the queue at `World.cpp:3711`.

`FillTerrainDraw` (`2500-2513`) copies those same four light blocks into `GxTerrainDraw` again. The GPU record stays: `GpuDrawItem` is pinned at 448 bytes (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:20`) and `GxTerrainDraw` (`Engine/Source/Gx/CGxDevice/CGxDevice.h:72-83`) is that record plus the 12-byte draw args (460 bytes). This slice does not touch either layout.

Target per chunk, from the campaign layout: pointer, `world`, `tex0`, `tex1`, index range, base vertex. That is **212 bytes** (216 with tail padding). The 256-byte light blocks and the 16-byte fog tuple are the gap.

### Flush scratch

`FlushMultiPass` (`2559`) still allocates scratch on every call, including after `s_terrainBatch` has reached a steady high-water mark:

- `std::vector<uint8_t> used(s_terrainBatch.size())` at `2572` is a new vector each flush.
- `std::vector<GxTerrainDraw> draws` at `2573` is a new vector each flush. `draws.clear()` at `2585` only reuses capacity inside that one call; `draws.push_back` at `2603` allocates again on the next flush.

`s_terrainBatch.clear()` at `2655` keeps capacity, and `push_back` at `2556` will not grow once the chunk count stays under that peak. There is no `reserve` on `used` or `draws`. They are not file-scope.

### Batch count

Grouping is still layers × chunks × chunks (`2576-2604`). A batch is chunks on the same layer whose `m_texture`, `m_layerTexture`, and fog tuple match (`2596`). Alpha order is the layer loop: opaque on layer 0, alpha after (`2619`). The shadow pass still runs after the batches (`2637-2653`). Chunks with `special`, a missing buffer, or the wrong pool never enter the batch (`2529-2540`).

No texture sort or hash has replaced the nested scan, so the batch count is the current predicate, not a new grouping to check against a baseline log. There is no Campaign 0 terrain-batch sample in tree to diff. Leave the predicate alone in the next slice.

## Next writer task

**Slim capture + shared lights.** One concern. Do not persist `used` / `draws`, and do not replace the nested pair scan.

1. Take the four light arrays off `TerrainCapture`. Keep one flush-scoped light block, written by a single `CaptureLights` per `FlushMultiPass`, then `memcpy` that block into each `GxTerrainDraw` from `FillTerrainDraw`. `DrawTerrainBatch` (`CGxDeviceVulkan.cpp:1502-1539`) still reads lights from the per-draw record.
2. Per-chunk `TerrainCapture` becomes `chunk`, `world`, `tex0`, `tex1`, `indexCount`, `firstIndex`, `baseVertex`.
3. Fog is the same kind of per-chunk device snapshot (`2549-2552`) and it is part of the pair key (`2596`, `2629`) and `ApplyChunkFog` (`2493-2497`). Hoist it to one flush-scoped tuple in this same slim capture. The pair test and the shadow pass read that tuple. If two chunks in one `s_terrainBatch` disagree on lights or fog, do not land the shared records — `RenderSetup` (`923-927`) can write both per chunk, and a shared block would change draws. That is a Faith reject of this approach, not a partial per-chunk leftover.
4. Leave pool identity, the `special` early-out (`2537-2539`), layer alpha order, and the shadow pass behavior as they are. `GxTerrainDraw` and `GpuDrawItem` field order stay put.

After that lands, the next loop remeasures capture size (212-byte chunk record, lights and fog once per flush), then the following writer slice is persistent `used` and `draws` (`clear` + `reserve`).
