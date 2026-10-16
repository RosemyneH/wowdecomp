# Campaign 3 — Accountant

LP64 field sizes (pointer 8). `C44Matrix` is 16 floats, **64** bytes, alignment 4 (`Engine/Source/Tempest/C44Matrix.h:21-36`). No Campaign 0 log is applied here. **N** is chunks that enter `s_terrainBatch`. **L** is layers that chunk is drawn in.

## TerrainCapture

`WoW/Source/WorldClient/MapRenderChunk.cpp:2443-2459`

| Field | Offset | Bytes |
| --- | ---: | ---: |
| `chunk` | 0 | 8 |
| `world` | 8 | 64 |
| `tex0` | 72 | 64 |
| `tex1` | 136 | 64 |
| `lightPos` | 200 | 64 |
| `lightDiffuse` | 264 | 64 |
| `lightAmbient` | 328 | 64 |
| `lightAtten` | 392 | 64 |
| `fogStart` | 456 | 4 |
| `fogEnd` | 460 | 4 |
| `fogColor` | 464 | 4 |
| `fog` | 468 | 4 |
| `indexCount` | 472 | 4 |
| `firstIndex` | 476 | 4 |
| `baseVertex` | 480 | 4 |
| tail pad | 484 | 4 |
| **sizeof** | | **488** |

No interior padding: the pointer ends on an 8-byte boundary, and every later field has alignment 4. The struct alignment is 8, so 484 rounds to **488**.

Groups: pointer **8**, three matrices **192**, four light blocks **256**, fog key **16**, draw args **12**, tail **4**.

The plan’s “about 468” is `8 + 192 + 256 + 12`. That sum is the pointer, the matrices, the light blocks, and the draw args. The struct in the source also holds the fog key and the tail pad.

## GxTerrainDraw

`Engine/Source/Gx/CGxDevice/CGxDevice.h:72-83`

| Field | Offset | Bytes |
| --- | ---: | ---: |
| `world` | 0 | 64 |
| `tex0` | 64 | 64 |
| `tex1` | 128 | 64 |
| `lightPos` | 192 | 64 |
| `lightDiffuse` | 256 | 64 |
| `lightAmbient` | 320 | 64 |
| `lightAtten` | 384 | 64 |
| `indexCount` | 448 | 4 |
| `firstIndex` | 452 | 4 |
| `baseVertex` | 456 | 4 |
| **sizeof** | | **460** |

Alignment 4, so the 460-byte span has no tail pad. Bytes **0–447** are the GPU record: three matrices and four light blocks, **448** bytes. Bytes **448–459** are the draw args (`indexCount`, `firstIndex`, `baseVertex`).

That 448-byte prefix matches `GpuDrawItem` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:89-97`, `Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:18-26`, `static_assert` at `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:20`). `GpuDrawItem` stays **448** bytes in that field order. The shared light block is a CPU source for the prefix. The record itself stays.

## Bytes copied per chunk

`QueueMultiPass` value-inits a `TerrainCapture` and `push_back` copies it into `s_terrainBatch` (`MapRenderChunk.cpp:2542`, `MapRenderChunk.cpp:2556`). The retained copy is **488** bytes per chunk that enters the batch. `s_terrainBatch.clear()` keeps capacity (`MapRenderChunk.cpp:2655`), so a warm flush pays that copy once per chunk. Chunks rejected at `MapRenderChunk.cpp:2537-2539` take no copy.

`CaptureLights` (`MapRenderChunk.cpp:2463-2490`) fills the four light blocks from `g_theGxDevicePtr->m_lights[0..3]`. Nothing in that function reads the chunk. The four `memset`s cover **256** bytes (`MapRenderChunk.cpp:2464-2467`). Those 256 bytes are the same for every chunk in the flush, and they sit inside the 488.

Each layer then value-inits a `GxTerrainDraw` and `push_back` copies it into `draws` (`MapRenderChunk.cpp:2601-2603`): **460** bytes per chunk per layer. `FillTerrainDraw` memcpy’s the four light blocks again (`MapRenderChunk.cpp:2507-2510`): **256** bytes. `DrawTerrainBatch` copies the 448-byte prefix into `GpuDrawItem` (`CGxDeviceVulkan.cpp:1518-1524`) and writes `indexCount`, `firstIndex`, and `baseVertex` into the indirect command (`CGxDeviceVulkan.cpp:1525-1529`).

Lights stored once per flush: the per-chunk capture the budget names is the three matrices plus the draw args, `192 + 12` = **204**. The light block is **256** bytes for the flush.

| Copy | Today | Lights once per flush |
| --- | ---: | ---: |
| Into `s_terrainBatch`, per chunk | 488 | 204 |
| Light bytes on that path | 256 × N | 256 × 1 |
| Into `draws`, per chunk per layer | 460 | 460 |
| Into `GpuDrawItem`, per draw | 448 | 448 |

For N chunks the capture store goes from **488N** to **204N + 256**.

The chunk pointer (`MapRenderChunk.cpp:2444`, stored at `2544`) and the fog key (`MapRenderChunk.cpp:2452-2455`, stored at `2549-2552`, tested at `2596-2597`) are still per chunk on this flush. They sit outside the 204.

| Per-chunk fields kept | Payload | sizeof |
| --- | ---: | ---: |
| Matrices + draw args | 204 | 204 |
| Plus the chunk pointer | 212 | 216 |
| Plus the fog key | 228 | 232 |
| Today, those fields plus the light blocks | 484 | 488 |

Dropping only the light blocks leaves a **232**-byte capture (`488 − 256`). Saved capture bytes are then **256(N − 1)**. `GxTerrainDraw` stays **460**. Each draw still uploads a **448**-byte `GpuDrawItem` with its own light arrays.

## Flush scratch

`used` is constructed at N bytes every flush (`MapRenderChunk.cpp:2572`). `draws` is a new vector every flush (`MapRenderChunk.cpp:2573`). `draws.clear()` at `MapRenderChunk.cpp:2585` holds capacity only until `FlushMultiPass` returns. A group of N chunks stores **460N** bytes of `GxTerrainDraw`. `s_terrainBatch` already survives its `clear()`.

The shadow pass (`MapRenderChunk.cpp:2637-2652`) runs after the batches. It calls `Draw` per shadowed chunk and does not build a `GxTerrainDraw`.
