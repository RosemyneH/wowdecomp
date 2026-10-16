# Faith — Campaign 4 remesh

Reviewed the live grass and terrain mesh paths. No writer diff. The slices in `docs/perf-squads/remesh/integrator.md` pass when the sites below stay.

**Verdict: PASS.** Hole masks, 145-vertex topology, winding, grass seeds, fade distance, the 220-yard cap, and the debug line stream all hold. The approach is not rejected.

## Hole masks — PASS

`CMap::s_holeMask` is the sixteen ADT bits (`WoW/Source/WorldClient/Map.cpp:286`). A cell is holed when `m_header->m_holes` has that bit.

`CMapChunk::CreateIndices` skips the cell and writes no indices (`WoW/Source/WorldClient/MapChunk.cpp:308-310`). The index is `(row / 2) * 4 + (col / 2)`, the same bit as `4 * (y >> 1) + (x >> 1)` on the grass and collision walks.

| Use | Site |
| --- | --- |
| Draw indices | `MapChunk.cpp:309` |
| Grass placement | `MapChunk.cpp:1454` |
| Ray hit | `MapChunk.cpp:1682` |
| Box facets | `MapChunk.cpp:1717` |
| Frustum facets | `MapChunk.cpp:1791` |

A chunk with every bit set (`m_holes == 0xFFFF`) never reaches `RenderPrep` (`WoW/Source/WorldClient/World.cpp:4068`). A partial mask still draws, and still joins the holed occlusion list (`World.cpp:4088`).

A shared index buffer is legal only for a half whose mask is zero. `FillLockedBufs` calls `CreateIndices` on chunk 0 and again on chunk 1 (`WoW/Source/WorldClient/MapRenderChunk.cpp:189`, `MapRenderChunk.cpp:193`). Each call keeps the test at `MapChunk.cpp:309`. A non-zero mask on either half keeps that half on the private write (`MapChunk.cpp:302-337`). Sharing the hole-free pattern onto a non-zero mask fails.

## 145-vertex topology — PASS

One chunk is 9×9 corners plus 8×8 centers: 145 vertices (`WoW/Source/WorldClient/MapChunk.cpp:259-274`, `WoW/Source/WorldClient/MapChunk.h:65`). `s_maxVertexCount` is 145 (`WoW/Source/WorldClient/MapRenderChunk.cpp:37`). The hole-free index count is 768 (`MapRenderChunk.cpp:40`): 8×8 cells, 12 indices each (`MapChunk.cpp:318-331`), then `baseVertex += 17` (`MapChunk.cpp:334`).

The batch max index is `145 - 1` (`MapRenderChunk.cpp:362`). The second chunk’s index base is `m_maxIndex + 1` (`MapChunk.cpp:883`), so a full first half starts the second half at 145. `FillLockedBufs` writes that half’s vertices at offset 145 (`MapRenderChunk.cpp:177`). Pool buffers are 145 or 290 vertices (`MapRenderChunk.cpp:617`, `MapRenderChunk.cpp:653`). Stream counts stay 145 and 768 (`MapRenderChunk.cpp:64`, `MapRenderChunk.cpp:67`).

The draw’s `baseVertex` is the vertex buffer’s pool offset (`MapRenderChunk.cpp:2555`). The +145 lives in the index values, not in that field. A dual hole-free share passes when the second 768 indices are the same pattern with 145 added to every index. Reusing the 0-based 768 for that half fails.

`s_pnEstimateVertex` stays `145 * count` (`MapRenderChunk.cpp:830`). Holes drop indices. They do not shrink the 145-vertex grid.

## Winding — PASS

Each cell is four triangles, center first (`MapChunk.cpp:318-329`):

| Triangle | Order |
| --- | --- |
| 1 | C, TL, BL |
| 2 | C, TR, TL |
| 3 | C, BR, TR |
| 4 | C, BL, BR |

C is `col + 9`, TL `col`, TR `col + 1`, BL `col + 17`, BR `col + 18`.

The ray test builds the same four triples from `s_fanIndices` (`WoW/Source/WorldClient/Map.cpp:289`, `MapChunk.cpp:1688-1689`): C,TL,BL then C,TR,TL then C,BL,BR then C,BR,TR. The facet table is the same winding (`MapChunk.cpp:73`: `17,9,0` is BL,C,TL, a rotation of C,TL,BL). A reversed triple fails. The shared hole-free buffer has to be these twelve indices per cell.

## Grass seeds — PASS

`CreateDetailDoodads` is not a remesh edit. The seed is the chunk offset (`MapChunk.cpp:1365-1367`):

```1365:1367:WoW/Source/WorldClient/MapChunk.cpp
    CRndSeed seed;
    seed.SetSeed(0);
    seed.SetSeed(this->m_cOffset.x | (this->m_cOffset.y << 16));
```

Cell picks consume two `uint32_` values (`MapChunk.cpp:1387-1388`). Each placed tuft then consumes `sfloat_` for `u`, `v` (`MapChunk.cpp:1499-1500`), rotation, and scale (`MapChunk.cpp:1588-1589`). A holed cell continues before those four floats (`MapChunk.cpp:1454`), so the hole does not draw a random and does not consume one.

`FillVertices` writes the stored position, rotation, scale, and normal (`DetailDoodad.cpp:402-404`, `DetailDoodad.cpp:416-418`). It does not call `CRandom`. A short pool already leaves both buffers null (`DetailDoodad.cpp:130-131`) and returns before that fill (`DetailDoodad.cpp:670-671`, `DetailDoodad.cpp:888-889`). The miss skips the batch. It does not reseed.

## Fade distance — PASS

Both grass paths fade across the last 15% of `CWorld::detailDoodadDist`.

Shader constants (`DetailDoodad.cpp:317-322`): `fade = 1 / (dist - dist * 0.85)`, `m_fade.x = -fade`, `m_fade.y = dist * fade`. That is zero at `dist` and one at `0.85 * dist`.

Fixed function (`DetailDoodad.cpp:777-780`): scale `1 / (dist * 0.15)`, then translate by `dist * -0.85` on Z. Same band.

A chunk grows grass while `detailDoodadDist > m_distToCamera` (`MapChunk.cpp:1611`). The miss return does not change these constants. `InitializeShaderConstants` still runs from `InitializeRenderState` before the fills (`DetailDoodad.cpp:761`, `DetailDoodad.cpp:787`).

## 220-yard behavior — PASS

Extended `groundEffectDist` tops out at 220. Retail stays 140.

| Site | Behavior |
| --- | --- |
| `WoW/Source/WorldClient/WorldParam_CVarSlider.h:14` | retail max `140` |
| `WorldParam_CVarSlider.h:15` | extended max `220` |
| `WoW/Source/WorldClient/WorldParam_CVar.cpp:390-402` | callback rejects a value outside that max, then calls `SetDetailDoodadDist` |
| `WoW/Source/WorldClient/World.cpp:1316-1318` | `SetDetailDoodadDist` clamps `dist > 220` to `220` |
| `World.cpp:1320-1323` | the clamped value is stored, and `groundEffectDistValueSqr` is `dist * dist` |

`World.cpp` is not a remesh hunk. The far-clip radius cap at 160 (`MapRenderChunk.cpp:824-825`) stays where it is. A different grass cap fails.

## Debug line stream — PASS

`Enable_ShowNormals` is `0x40000000` (`WoW/Source/WorldClient/Types.h:72`). `RenderChunksSinglePass` queues those chunks (`World.cpp:3719`) and later calls `RenderSinglePass` (`World.cpp:3744`).

`RenderSinglePass` locks its own stream buffers (`MapRenderChunk.cpp:1021-1023`) and draws `GxPrim_Lines` (`MapRenderChunk.cpp:1060`). It does not call `UseStreamingBufs`.

`UseStreamingBufs` (`MapRenderChunk.cpp:889`) is the terrain miss path from `RenderSetup` when `m_chunkBuf` is null (`MapRenderChunk.cpp:930-934`). Replacing that miss with an `s_bufList` tail steal leaves `RenderSinglePass` on `BufStream`. Dropping that stream fails.

WMO normal lines are a separate stream (`WoW/Source/WorldClient/MapObj.cpp:3945-3947`, gated at `MapObj.cpp:1154`). This squad does not own that function. It keeps streaming.

## Close

Pass all seven. A later diff fails if it shares indices onto a non-zero mask, leaves a dual chunk’s second half at base 0, reverses a triangle, reseeds grass, moves the 15% fade band, changes the 220 cap, or stops `RenderSinglePass` from streaming.
