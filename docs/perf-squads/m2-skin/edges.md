# Campaign 1 edges — M2 skin

Holds for the three slices: static bind on the one-bone path, one CPU skin per `(model, section)` for the frame, and an in-place instance grow. A miss on any of these draws the wrong mesh or frees a buffer a draw in this frame still references.

Shaders stay on their current path (`CM2SceneRender.cpp:171`). The cache below is the `s_enableShaders == 0` skin.

## Two batches, same model, same frame

One `CM2Model` emits one element per batch (`M2Scene.cpp:1799`). Opaque doodad-compatible batches are grouped by shared, batch index, and lighting (`M2Scene.cpp:205`, `M2Scene.cpp:936`). A group of one is pushed back to a type-0 element (`M2Scene.cpp:2172`). Transparent batches stay type 0 (`M2Scene.cpp:1851`). Pass 0 then draws those type-0 elements and, on the same `CM2SceneRender`, the doodad groups (`M2Scene.cpp:2221`).

Two batches of one model in one frame are the usual city case. They are often not adjacent: `SortOpaque` / `SortDoodad` run in between (`M2Scene.cpp:2147`, `M2Scene.cpp:2186`), and the two batches have different `m_index` values so they land in different doodad groups.

`SetBatchVertices` only skips a reskin when the **previous** element matches type, model, section, and the env bit (`CM2SceneRender.cpp:179`). That skip is valid because the stream buffer is still bound. A frame cache is wider than that adjacency, and the key is not `(model, section, frame)` alone.

| Second batch differs by | Why the first skin cannot be reused |
| --- | --- |
| `m_skinSection` pointer | Different vertex range. Opt-geo sections are not the file sections (`M2Scene.cpp:1803`). |
| `m_shader & 0x4000` | `SetVertices` takes that bit as `texCoord` (`CM2SceneRender.cpp:190`). |
| batch flag `0x40` | Multi-sample writes `M2VertexPNT2` through `TransformVerticesNoUVSelect` (`CM2SceneRender.cpp:187`, `M2Model.cpp:4519`). The other path writes 32-byte `PNT`. |
| Doodad group membership | `FillDoodadSkin` always passes texCoord 0 (`CM2SceneRender.cpp:603`). The matrix list is the group's instances (`CM2SceneRender.cpp:640`), not the shared. |

Reuse rules that hold:

- Skinned cache key is the `CM2Model` plus the section pointer actually drawn, plus env bit, plus multi-sample. `CM2Shared` is the wrong owner: two instances of one file do not share bones.
- Rigid static vertex and index buffers are per skin section on the shared. Two doodad groups that name the same section bind that buffer twice and upload two matrix lists. The second call must not rebuild the mesh. `DrawRigidDoodads` rewrites vertices and indices on every call today (`CM2SceneRender.cpp:644`, `CM2SceneRender.cpp:667`).
- The function-static `worlds` vector is cleared per call (`CM2SceneRender.cpp:636`). Do not memoize it on `(shared, section, frame)`. Two groups, same section, different instance sets.
- Sort order stays. The cache must not pull the two batches together to make the old adjacency skip fire.

`DrawBatch` already rebinds bone constants when the model or `m_boneComboIndex` changes (`CM2SceneRender.cpp:431`). A vertex-cache hit does not skip that, `SetShaders` (`CM2SceneRender.cpp:464`), or material setup.

## Bone update between passes

`CM2Scene::Animate` bumps `m_frameStamp` once (`M2Scene.cpp:1702`) and writes the whole palette. `AnimateMT` / `AnimateMTSimple` stamp the model at the end (`M2Model.cpp:5709`, `M2Model.cpp:4843`). A later `Animate` returns when the stamps match (`M2Model.cpp:5848`). Billboard rows are part of that write (`M2Model.cpp:5443`).

Color passes all see that palette if nothing stores a skin earlier and nothing writes bones again. The pass order is fixed: `CWorldScene::Render` animates (`CWorldSceneRender.cpp:189`), then missile `ForceAnimate` (`CWorldSceneRender.cpp:193`, `M2Model.cpp:5908`), then `CShadowQuery::Update` (`CWorldSceneRender.cpp:194`). `CGWorldFrame` draws `M2PASS_0` (type 0, then doodads), then liquids and weather, then `M2PASS_1` and `M2PASS_2` in camera-dependent order (`WorldFrame.cpp:1863`, `WorldFrame.cpp:1873`).

A cache keyed only by frame stamp is wrong in three places:

1. **`AnimateSM` does not stamp.** Shadow gather calls it (`M2Model.cpp:7726`). On a stamp match it returns (`M2Model.cpp:6203`). On a miss for a model with no attach parent and no billboard bones, it writes **bone 0 only** (`M2Model.cpp:6212`) and leaves `m_frameStamp` stale. The next `Animate` still rewrites the full palette (`M2Model.cpp:5848`). A skin recorded from that bone-0 patch and replayed after the full animate is a stale pose. Attach and billboard models take the full `Animate` path instead (`M2Model.cpp:6207`).
2. **`ForceAnimate` moves the stamp backwards** (`M2Model.cpp:5910`) and writes bones again. Trajectory models do this after the scene animate and before either color pass. Any `ForceAnimate` after the first cache fill in the frame invalidates that model's entries. Frame id did not change.
3. **Shadow replaces the device view** (`ShadowQuery.cpp:406`) and then draws casters (`ShadowQuery.cpp:428`). The palette stays in the color-camera space from `Animate`, except the bone-0 patch above. Do not fill the CPU cache under the shadow look-at. `RenderBatchShadowMap` reads the palette into shader constants (`M2Model.cpp:2960`); it is not a CPU skin. Leave it that way so the color cache has one writer.

Inside one doodad group the `done += instances` slices share one palette (`CM2SceneRender.cpp:771`). Caching each **model's** section once and replaying it for later slices of that same model is fine. Caching slice 0's packed stream as "the section" is not: the next slice is other models (`CM2SceneRender.cpp:802`).

## Instances crossing the capacity step

`m_instances` starts at 1 (`M2Shared.cpp:1150`). `m_maxInstances` is the minimum of `0x10000 / indexCount` and, per section, `m_boneCountMax / m_boneCount`, and is at least 1 (`M2Shared.cpp:1136`). `AllocInstances` grows only when `count > m_instances` and `m_instances < m_maxInstances`, rounds with `(count + 15) & ~15`, clamps to the max, then `DestroyBuffers` (`M2Shared.cpp:771`).

Callers that cross:

- Every doodad group, **before** the rigid early-out (`CM2SceneRender.cpp:711`, `CM2SceneRender.cpp:717`). A rigid batch that steps the capacity frees the shared pools even though today's rigid path never binds them.
- Shadow batches (`M2Model.cpp:2937`), which run in `CShadowQuery::Update` and again from `SetShadowMapGenericGlobal` during map-obj render (`World.cpp:3034`). That is before color `Draw`, and it can be two bands in one frame.

Holds for an in-place double:

- **Clamp to `m_maxInstances`.** Past that, the draw loop already slices (`CM2SceneRender.cpp:713`, `CM2SceneRender.cpp:771`, `M2Model.cpp:2939`). A double that ignores the cap overflows the uint16 index space `SetIndices` builds by adding `vertexCount` per instance (`M2Shared.cpp:363`).
- **Do not `PoolDestroy` on the step** (`M2Shared.cpp:602`). The same `CM2Shared` may already have a draw in this command buffer: an earlier doodad group, or a shadow band. Retire the old allocation with the frame garbage. The steady camera must not call `DestroyBuffers` at all; `m_instances` never shrinks today, and a shrink-every-frame hysteresis brings the destroy back.
- **`m_hasData` does not mean the layout is current.** `SetIndices` and both `SetVertices` arms refill only when the flag is clear (`M2Shared.cpp:348`, `M2Shared.cpp:398`, `M2Shared.cpp:430`). The index cursor for section *i* is the sum of earlier sections times `m_instances` (`M2Shared.cpp:372`). Growing 16 → 32 without clearing the flag leaves section 1 at the old offset and leaves the new instance slots unwritten. Clear the flag or rewrite the grown range. Do that before the second group's `Draw`.
- **One grow per group, then slices.** `AllocInstances` is not called inside the `done` loop. The cross is between groups (or between shadow and the first color group), not between slices of one group.
- Buffers are sized to `m_instances`, and the returned count may be larger than `total`. Callers clamp the draw to `total`. Keep that. Filling only the live count and then drawing with the rounded `m_indexStart` reads off the end of the refill.

At the ceiling (`m_instances == m_maxInstances`) a larger `count` must not grow and must not destroy. The existing condition already guarantees that (`M2Shared.cpp:772`).

## Influences count change

`s_transformVertices` has five slots (`M2Model.h:225`). Slot 1 is the one-bone transform. Slots 0 and 2–4 are the blended transform (`M2Model.cpp:3871`). `FillDoodadSkin` indexes the table with the section's raw `m_boneInfluences` (`CM2SceneRender.cpp:792`, `CM2SceneRender.cpp:603`).

The rigid predicate is not that field. `DrawRigidDoodads` requires `m_boneCount == 1`, a non-zero vertex and index count, one shared `m_indices.b[0]` across the section, and a bone id in range (`CM2SceneRender.cpp:609`, `CM2SceneRender.cpp:617`). It then instances `m_boneMatrices[bone]` (`CM2SceneRender.cpp:641`). A section with `m_boneInfluences == 1` and `m_boneCount > 1` stays on `TransformVerticesOneBone`, which ignores weights and reads each vertex bone (`M2Model.cpp:3162`). A section with `m_boneCount == 1` and a uniform bone id takes the rigid path even when `m_boneInfluences` is not 1. Switching the predicate to the influences field moves both of those.

Opt-geo stores the **max** influences on the merged section (`M2Model.cpp:4245`) and also raises `m_boneCount` (`M2Model.cpp:4241`). The optimized CPU skin does not use that max. It dispatches each original section with its own count (`M2Model.cpp:2712`). One skin of the merged header through `s_transformVertices[merged.m_boneInfluences]` changes every subsection whose count was lower: one-bone versus blend, and the blend path consumes weights (`M2Model.cpp:3130`). The cached bytes have to be the concatenation `SetVertices` already writes.

Load remaps only the first `m_boneInfluences` indices through `m_boneCombos` (`M2Shared.cpp:1198`). The blend loop still walks up to four non-zero weights. Two sections with different influence counts are not the same skin, even for one model in one frame.

These stays are outside the CPU cache and still move when the count changes:

- Doodad combine requires equal influences (`M2Model.cpp:6993`). Shadow sort is influences-major (`M2Model.cpp:6936`).
- `RenderBatchShadowMap` rebinds the default shader when the count differs from the previous section (`M2Model.cpp:2989`).
- `ComputeElementShaders` feeds the count into the permute (`M2Scene.cpp:1478`). A vertex-cache hit on `DrawBatch` must not skip `SetShaders`.
- Hit-test splits on `m_boneInfluences == 1` (`M2Scene.cpp:662`). It reads the section, not the draw cache.

## Section with zero verts

`DrawRigidDoodads` returns 0 when `m_vertexCount` or `m_indexCount` is 0 (`CM2SceneRender.cpp:609`), and `DrawBatchDoodad` falls through into the general path (`CM2SceneRender.cpp:717`). That fall-through is the edge.

- `BufStream` of `instances * 0` sets the device stream size to 0 and clears `m_hasData` (`CM2SceneRender.cpp:776`, `CGxDevice.cpp:1062`). That stream object is shared by every M2 lock. A zero-vert section in the middle of the frame drops the previous section's stream bookkeeping.
- `stride` is 0 (`CM2SceneRender.cpp:791`), so every instance in the group calls the transform at the same address (`CM2SceneRender.cpp:603`). The transform writes nothing when the range is empty (`M2Model.cpp:3107`). The call still runs.
- `m_maxIndex` is a `uint16_t` (`Types.h:184`). `instances * 0 - 1` becomes 65535 (`CM2SceneRender.cpp:818`). `DrawBatch` does the same with `m_vertexCount - 1` (`CM2SceneRender.cpp:491`). A zero-vert section that still has indices draws against that max.
- `GetRegionBounds` returns 0 below 4 vertices (`M2Model.cpp:2734`), and `DrawBatchProj` then returns (`CM2SceneRender.cpp:516`). A cache hit must not make that path emit a projected quad.
- `SetIndices` adds `m_instances * m_indexCount` to the cursor (`M2Shared.cpp:372`). A zero count is a no-op only when the cursor does not get reset to 0 and then bound as if it were the previous section.

Static bind and the frame cache should treat vertex count 0 as draw nothing and publish nothing. Do not insert an entry whose empty buffer can satisfy a later lookup, and do not `BufStream` the global vertex stream to discover the count.

`m_maxInstances` divides by `m_boneCount`, not by vertex count (`M2Shared.cpp:1139`). A section that is empty **and** has `m_boneCount == 0` hits that divide during skin init, before any draw. The capacity clamp has to skip that section the same way the rigid path already rejects `m_boneCount != 1`.
