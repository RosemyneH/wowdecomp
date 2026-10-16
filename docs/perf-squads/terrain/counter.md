# Campaign 3 — Terrain counters

Counters and clocks only. No behavior change. Use the Campaign 0 compile-out flag (default off) and relaxed atomics. Keep the Campaign 0 log field names. The one-second console line prints these accumulators and then zeros them.

Clocks are `OsGetAsyncTimeClocks` (`CLOCK_MONOTONIC` on Linux). Store the delta.

## Zones

| Zone | Wraps | Site |
| --- | --- | --- |
| `terrain_queue` | whole `QueueMultiPass` | `MapRenderChunk.cpp:2526` |
| `terrain_flush` | whole `FlushMultiPass` | `MapRenderChunk.cpp:2559` |

## Batch size

`terrain_batch` is the number of `TerrainCapture` records a flush walks.

- Add `s_terrainBatch.size()` once per non-empty flush, after the empty return at `MapRenderChunk.cpp:2560`.
- Also keep the non-empty flush count and the max size in the same window. The nested scan is per flush, and `FlushMultiPass` (`MapRenderChunk.cpp:2559`) runs once per pass / layer-count bucket.
- Chunks that fail the special / pool gate draw through `RenderMultiPassAlpha` and return (`MapRenderChunk.cpp:2537`). They stay out of `s_terrainBatch` (`MapRenderChunk.cpp:2461`).
- `s_terrainBatch.clear()` at `MapRenderChunk.cpp:2655` drops the size and keeps capacity.

## Capture bytes

`terrain_capture_bytes` is the bytes appended to `s_terrainBatch` per accepted chunk.

- Add `sizeof(TerrainCapture)` at `s_terrainBatch.push_back` (`MapRenderChunk.cpp:2556`).
- The local `TerrainCapture cap` (`MapRenderChunk.cpp:2542`) is that record. `push_back` copies it, light blocks included.

`sizeof(TerrainCapture)` is **488** on LP64 / Win64 (align 8). The plan’s “about 468” is that record; the `fog` field starts at offset 468, the payload ends at 484, and 4 bytes of tail padding bring the copy to 488.

| Bytes | What is copied | Lines |
| --- | --- | --- |
| 8 | `chunk` | `MapRenderChunk.cpp:2444` |
| 192 | `world`, `tex0`, `tex1` (`C44Matrix` is 64) | `MapRenderChunk.cpp:2445`–`2447` |
| 256 | four light blocks: `lightPos`, `lightDiffuse`, `lightAmbient`, `lightAtten` | `MapRenderChunk.cpp:2448`–`2451` |
| 16 | `fogStart`, `fogEnd`, `fogColor`, `fog` | `MapRenderChunk.cpp:2452`–`2455` |
| 12 | `indexCount`, `firstIndex`, `baseVertex` | `MapRenderChunk.cpp:2456`–`2458` |
| 4 | tail padding | `MapRenderChunk.cpp:2443`–`2459` |
| **488** | one `push_back` | `MapRenderChunk.cpp:2556` |

`CaptureLights` (`MapRenderChunk.cpp:2463`) memsets the four blocks (`MapRenderChunk.cpp:2464`–`2467`) and rereads `g_theGxDevicePtr->m_lights[0..3]` into every chunk (`MapRenderChunk.cpp:2469`–`2490`). Those lights are the same for every chunk in the flush.

`FillTerrainDraw` copies the same 256 light bytes again into `GxTerrainDraw` (`MapRenderChunk.cpp:2507`–`2510`). `GxTerrainDraw` is 460 bytes: the 448-byte GPU record (three matrices + four light blocks) plus 12 bytes of draw args. That memcpy stays outside `terrain_capture_bytes`. This counter is the queue copy, so the slim-capture slice shows the per-chunk drop when the light blocks leave the record.

Budget: capture bytes per chunk fall to the pointer, three matrices, and the draw args. The 256-byte light blocks leave this counter.

## Flush allocations

`terrain_flush_allocs` counts heap allocations from `used` and `draws` inside `FlushMultiPass`. Record the event count and the new-buffer bytes.

- `std::vector<uint8_t> used(s_terrainBatch.size())` at `MapRenderChunk.cpp:2572`. One allocation of `s_terrainBatch.size()` bytes on every non-empty flush. The empty return at `MapRenderChunk.cpp:2560` runs first, so an empty flush adds nothing.
- `std::vector<GxTerrainDraw> draws` at `MapRenderChunk.cpp:2573` starts empty. Each `draws.push_back` at `MapRenderChunk.cpp:2603` that grows `capacity` is one allocation of `capacity * 460` bytes (`sizeof(GxTerrainDraw)` is 460). `draws.clear()` at `MapRenderChunk.cpp:2585` keeps that capacity for the rest of the flush. The vector ends with the function, so the next flush allocates again.
- `s_terrainBatch` stays out of this counter. It is static (`MapRenderChunk.cpp:2461`) and `clear` at `MapRenderChunk.cpp:2655` already keeps its capacity.

Budget: after persistent scratch warms up, the count stays 0.

## Nested-scan comparisons

`terrain_scan_comps` counts pair tests in the layers × chunks × chunks scan.

- Add 1 each time the predicate at `MapRenderChunk.cpp:2596` is evaluated.
- Loop: `layer` from 0 to `maxLayers` (`MapRenderChunk.cpp:2564`–`2570`, `2576`), `i` from 0 to `n - 1` (`MapRenderChunk.cpp:2579`), `j` from `i` to `n - 1` (`MapRenderChunk.cpp:2587`). `n` is `s_terrainBatch.size()`.
- The `continue`s at `MapRenderChunk.cpp:2580` and `MapRenderChunk.cpp:2588` (`used[]`, or `layer >= m_layersCount`) happen before the predicate. Comparisons are predicate evaluations only.
- The predicate reads `m_texture`, `m_layerTexture`, `fog`, `fogColor`, `fogStart`, and `fogEnd`. One evaluation is one comparison, including when `||` stops early.
- Upper bound per flush is `maxLayers * n * (n + 1) / 2`, because `j` starts at `i`.
- When `DrawTerrainBatch` returns 0 (`MapRenderChunk.cpp:2625`), the fallback loop at `MapRenderChunk.cpp:2626` evaluates the same pair test at `MapRenderChunk.cpp:2629`. Add 1 for each of those evaluations. The base device returns 0; Vulkan returns 0 when the indirect batch cannot be allocated.

The walk stays layer 0, then later layers (`MapRenderChunk.cpp:2576`).

## Must stay

Same chunks enter the batch (`special`, pool identity, `MapRenderChunk.cpp:2529`–`2539`). The shadow pass still runs after the batches (`MapRenderChunk.cpp:2637`). `GxTerrainDraw` layout, pairing, fog tests, and draw order stay as they are.
