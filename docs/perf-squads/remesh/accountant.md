# Campaign 4 — Accountant

Index pool at the radius-160 cap, plus one terrain stream refill when `AllocBuf` fails.

| Bill | Bytes |
| --- | --- |
| Index pool (`768` `uint16`s × 51,200 slots) | 78,643,200 |
| One 145-vertex refill, `GxVBF_PN` | 3,480 |
| **Pool + `GxVBF_PN` refill** | **78,646,680** |
| One 145-vertex refill, `GxVBF_PNC` | 4,060 |
| **Pool + `GxVBF_PNC` refill** | **78,647,260** |

The pool is resident for the life of `UpdatePools`. Each chunk that still has a null `m_chunkBuf` pays the refill again on the next `RenderSetup`, because `BufStream` reuses one shared stream buffer.

## Index pool

`CMapRenderChunk::UpdatePools` at `WoW/Source/WorldClient/MapRenderChunk.cpp:805`.

Radius is `1 - (int)(World::s_farClip * -0.03f)` (`WoW/Source/WorldClient/MapRenderChunk.cpp:821`), then capped:

- `WoW/Source/WorldClient/MapRenderChunk.cpp:824` — `if (radius > 160)`
- `WoW/Source/WorldClient/MapRenderChunk.cpp:825` — `radius = 160`
- `WoW/Source/WorldClient/MapRenderChunk.cpp:828` — `count = (2 * radius * radius + 1) & ~1u`

At the cap:

- `2 * 160 * 160 + 1` = 51,201
- `51,201 & ~1` = **51,200** slots

`s_pnEstimateIndex = 768 * count` (`WoW/Source/WorldClient/MapRenderChunk.cpp:831`): `768 * 51200` = **39,321,600** `uint16`s.

`PoolCreate` takes `s_pnEstimateIndex * 2` (`WoW/Source/WorldClient/MapRenderChunk.cpp:835`):

`39,321,600 * 2` = **78,643,200** bytes (75 MiB).

That 768 is the hole-free 8×8 pattern (`WoW/Source/WorldClient/MapRenderChunk.cpp:40`, written by `CMapChunk::CreateIndices` at `WoW/Source/WorldClient/MapChunk.cpp:302`).

## Stream refill when AllocBuf fails

`AllocBuf` returns null when the block free list is empty:

- Dual path (`flags & 0x3`): `WoW/Source/WorldClient/MapRenderChunk.cpp:600`–`601`
- Single path: `WoW/Source/WorldClient/MapRenderChunk.cpp:635`–`636`

`RenderPrep` stores that pointer (`WoW/Source/WorldClient/MapRenderChunk.cpp:992`–`993`). With `m_chunkBuf` still null, `RenderSetup` calls `UseStreamingBufs` (`WoW/Source/WorldClient/MapRenderChunk.cpp:930`–`934`).

`UseStreamingBufs` (`WoW/Source/WorldClient/MapRenderChunk.cpp:889`) sets `vertexCount` from `s_streamVertexCount` (`WoW/Source/WorldClient/MapRenderChunk.cpp:64`, `WoW/Source/WorldClient/MapRenderChunk.cpp:891`). `BufStream` sizes the lock at `itemCount * itemSize` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1066`–`1071`). `RenderPrepBufs` locks and fills it (`WoW/Source/WorldClient/MapRenderChunk.cpp:866`–`874`). `CreateVertices` writes the full 145 (`WoW/Source/WorldClient/MapChunk.cpp:968`).

Stride is chosen at `WoW/Source/WorldClient/MapRenderChunk.cpp:890`.

| Format | When | Stride | 145 × stride |
| --- | --- | --- | --- |
| `GxVBF_PN` | `Initialize` sets it (`WoW/Source/WorldClient/MapRenderChunk.cpp:361`). `LoadWdt` keeps it when WDT flag `0x2` is clear (`WoW/Source/WorldClient/Map.cpp:2995`–`2996`). | 24. `CGxVertexPN` is two `C3Vector`s (`WoW/Source/WorldClient/MapChunk.h:40`–`44`). `C3Vector` is three floats (`Engine/Source/Tempest/C3Vector.h:24`–`26`). | **3,480** |
| `GxVBF_PNC` | `LoadWdt` when WDT flag `0x2` is set (`WoW/Source/WorldClient/Map.cpp:2993`–`2994`). | 28. `CGxVertexPNC` adds `CImVector` (`WoW/Source/WorldClient/MapChunk.h:46`–`51`). `CImVector` is 4 bytes (`Engine/Source/Tempest/CImVector.h:11`–`14`). | **4,060** |

**78,643,200 + 3,480 = 78,646,680** (`GxVBF_PN`).

**78,643,200 + 4,060 = 78,647,260** (`GxVBF_PNC`).

A dual chunk (`m_unkFlags & 0x3`) doubles `vertexCount` (`WoW/Source/WorldClient/MapRenderChunk.cpp:894`–`897`): 290 vertices, **6,960** bytes (`PN`) or **8,120** bytes (`PNC`).

The same `UseStreamingBufs` call also streams `s_streamIndexCount` (`WoW/Source/WorldClient/MapRenderChunk.cpp:67`, `WoW/Source/WorldClient/MapRenderChunk.cpp:892`, `WoW/Source/WorldClient/MapRenderChunk.cpp:900`): `768 * 2` = **1,536** bytes. The sums above are the index pool plus the 145-vertex lock.

## Grass miss

`AcquireBuffers` leaves both pointers null when the grass free list is short (`WoW/Source/WorldClient/DetailDoodad.cpp:129`–`131`). `CDetailDoodadInstBatch::Draw` returns before any stream lock (`WoW/Source/WorldClient/DetailDoodad.cpp:888`–`889`). That miss is **0** bytes on this bill.
