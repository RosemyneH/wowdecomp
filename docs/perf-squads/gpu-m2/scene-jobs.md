# G1 scene jobs

The two `s_transformVertices` calls are `WriteSkin` (`Engine/Source/Model2/M2Scene.cpp:1842`). They fill a CPU byte buffer for the color-pass meshes that `M2SkinTake` copies at draw time. They are the type `0` batches and the multi-bone type `2` doodad groups. Shadow-map casters, particles, and projected batches are other functions, and G1 does not move this file onto `GpuSkinBind`.

`M2SkinPrepare` (`M2Scene.cpp:1872`) walks `scene->m_elements` after `CM2Scene::Animate` has already sorted them. `Job::Start` (`M2Scene.cpp:1931`) runs `SkinRange` (`M2Scene.cpp:1864`) across terrain. `CM2Scene::Draw` joins with `M2SkinFinish` (`M2Scene.cpp:2471`) before it walks the pass list.

## What those calls skin

`Animate` assigns `m_type` while it builds the list (`M2Scene.cpp:2095`). `M2SkinPrepare` keeps an element only when it has a model, a batch, and a skin section (`M2Scene.cpp:1887`), `m_type` is `0` or `2` (`M2Scene.cpp:1895`), a type `2` section has `m_boneCount != 1` (`M2Scene.cpp:1891`), and the batch flag `0x40` is clear (`M2Scene.cpp:1899`). `SkinVertexCount` (`M2Scene.cpp:1810`) then drops a section whose `m_boneInfluences` is above 4, and the enqueue stops once `s_skinUsed` is past 64 MiB (`M2Scene.cpp:1913`).

The job key is `(model, section, texCoord, optimized)` (`M2Scene.cpp:1700`, `M2Scene.cpp:1905`). A second element with the same key does not get a second job.

| Call | When | What it writes |
| --- | --- | --- |
| `M2Scene.cpp:1844` | `job.optimized == 0` | One section. `s_transformVertices[m_boneInfluences]` writes 32-byte `GxVBF_PNT` (position, normal, one UV). `texCoord` selects `m_texcoord[0]` or `m_texcoord[1]` inside `TransformVertices_*` (`M2Model.cpp:3625`, `M2Model.cpp:3646`). |
| `M2Scene.cpp:1859` | `job.optimized != 0` | The opt-geo batch range `m_optGeo->m_batchRanges[section->m_skinSectionId]` (`M2Scene.cpp:1848`). Each live sub-section is skinned in order, then `dst` advances by `32 * m_vertexCount` (`M2Scene.cpp:1860`). Same packing as the optimized loop in `CM2Model::SetVertices` (`M2Model.cpp:3177`). |

`optimized` and `texCoord` are chosen at enqueue (`M2Scene.cpp:1903`):

- Type `0`: `optimized` is `m_flags & 4` (set when the model has opt-geo, `M2Scene.cpp:2125`). `texCoord` is 1 when `m_batch->m_shader & 0x4000`, otherwise 0.
- Type `2`: `optimized` is 0 and `texCoord` is 0, matching `FillDoodadSkin` (`CM2SceneRender.cpp:605`).

Type `0` is every mesh batch that is projected-off and either not doodad-compatible or transparent (`M2Scene.cpp:2110`). A doodad group of one is rewritten to type `0` before prepare runs (`M2Scene.cpp:2433`). Type `2` that survives the `m_boneCount == 1` skip is a multi-bone doodad group (`IsBatchDoodadCompatible`, `M2Model.cpp:880`).

`m_shadowValue` is not part of the key. `ComputeElementShaders` (`M2Scene.cpp:1457`) stores a shadow permute on the same element, and `SetupLighting` applies it at draw (`CM2SceneRender.cpp:206`). The job still skins that mesh. The permute is a later shader input, not a second vertex stream.

### Shadow casters, particles, projected

These three do not enter `WriteSkin`.

| Population | Element | Where it actually draws | Relation to the jobs |
| --- | --- | --- | --- |
| Projected batches | Type `1` when `(m_flags & 4)` and `m_projectTextureCallback` is set (`M2Scene.cpp:2095`, `M2Scene.cpp:2110`) | `DrawBatchProj` (`CM2SceneRender.cpp:498`, dispatched at `CM2SceneRender.cpp:1164`) | `GetRegionBounds` blends a few bones for a box (`M2Model.cpp:3202`). No `s_transformVertices`, no `M2SkinTake`. Prepare rejects type `1`. |
| Particles | Type `4` from `AddParticleElement` (`M2Scene.cpp:1671`, enqueued at `M2Scene.cpp:2368`) | `DrawParticle` / `DrawBatchedParticle` (`CM2SceneRender.cpp:1016`, `CM2SceneRender.cpp:902`) | Emitter verts (`PNCT` or `PCT`). Prepare rejects type `4`. |
| Shadow-map casters | Not an `M2Element` type | `CShadowQuery` render callback calls `CM2Model::RenderModelBatchesShadowMap` (`WoW/Source/WorldClient/ShadowQuery.cpp:428`) | `RenderBatchShadowMap` (`M2Model.cpp:3382`) binds `CM2Shared::SetVertices` (`M2Model.cpp:3415`, `M2Model.cpp:3488`) and uploads bone constants (`M2Model.cpp:3430`). That shared upload copies bind-pose position and normal (`M2Shared.cpp:415`). It does not call `s_transformVertices` or `M2SkinTake`. |

Ribbons (type `3`, `M2Scene.cpp:2264`, `DrawRibbon` at `CM2SceneRender.cpp:868`) and draw callbacks (type `5`, `M2Scene.cpp:2292`) miss the same type filter.

Also absent from the byte buffer, and skinned later only if their own path asks:

- Type `2` with `m_boneCount == 1`. Prepare skips them (`M2Scene.cpp:1891`). `DrawRigidDoodads` (`CM2SceneRender.cpp:612`) keeps the bind-pose mesh and draws one matrix per copy. If that function returns 0, `FillDoodadSkin` skins at draw time because `M2SkinTake` misses (`CM2SceneRender.cpp:605`).
- Batch flag `0x40`. Prepare skips it (`M2Scene.cpp:1899`). `SetBatchVertices` sends it to `SetVerticesMultiSample` (`CM2SceneRender.cpp:187`), which calls `TransformVerticesNoUVSelect_cpp` (`M2Model.cpp:5020`).
- `m_boneInfluences` above 4. No job (`M2Scene.cpp:1812`, `M2Scene.cpp:1832`). `SetVertices` still indexes `s_transformVertices` at draw time (`M2Model.cpp:3191`).

## Who reads the bytes

`M2SkinTake` (`M2Scene.cpp:1943`) memcpys `job.bytes` into the caller’s lock. Two call sites:

| Take | Key it looks up | Draw that follows |
| --- | --- | --- |
| `CM2Model::SetVertices` (`M2Model.cpp:3169`) | The `(section, texCoord, optimized)` that `SetBatchVertices` passes (`CM2SceneRender.cpp:190`) | `DrawBatch` (`CM2SceneRender.cpp:418`), one draw, `m_minIndex == 0` (`CM2SceneRender.cpp:490`) |
| `FillDoodadSkin` (`CM2SceneRender.cpp:605`) | `(section, texCoord 0, optimized 0)` per instance | `DrawBatchDoodad` packs `instances` into one `GxVBF_PNT` stream and draws once (`CM2SceneRender.cpp:813`, `CM2SceneRender.cpp:861`) |

`CM2SceneRender::Draw` is the order (`CM2SceneRender.cpp:1159`): type `0` → `DrawBatch`, type `2` → `DrawBatchDoodad`. Pass 0 draws `m_passElements[0]`, then `m_doodadElements` (`M2Scene.cpp:2485`). Passes 1 and 2 are the liquid split (`WorldFrame.cpp:1767`). The same element index can sit in both pass 1 and pass 2 (`M2Scene.cpp:2205`). A depth-sort copy (`M2Scene.cpp:2221`) shares the original key because flag `1` is not in `SkinKey`. One job, two takes. `M2SkinFinish` only joins (`M2Scene.cpp:1938`); it does not clear the bytes, so the later pass still hits.

`SetBatchVertices` skips `CM2Model::SetVertices` when shaders are on (`CM2SceneRender.cpp:171`) and when the one-bone shared path is on (`m_bones.m_count == 1` and cache flag `0x40`, `CM2SceneRender.cpp:462`). Prepare does not know either condition, so it can still write a type `0` job that this frame never takes. The shared one-bone draw uses `CM2Shared::SetVertices` and `m_boneMatrices[0]` as the world matrix (`CM2SceneRender.cpp:475`).

## GpuSkinBind later, same element order

The sort is already finished when the jobs start. `Animate` runs `SortDoodadProxy`, `SortOpaque`, and `SortTransparent` (`M2Scene.cpp:2408`, `M2Scene.cpp:2447`) and returns. `CWorldScene::Render` then calls `M2SkinPrepare` (`CWorldSceneRender.cpp:191`) and only afterwards `CShadowQuery::Update` and `RenderChunks` (`CWorldSceneRender.cpp:197`, `CWorldSceneRender.cpp:200`). The jobs do not draw and do not reorder `m_passElements`.

`GpuSkinBind` copies one palette and arms the next `Draw`, then that draw clears the arm (`docs/gpu-m2-plan.md`). The bind that preserves order is the one already specified next to an existing draw:

- Type `0`, `optimized == 0`, `texCoord == 0`. `DrawBatch` already draws this element in list order. `GpuSkinBind(m_boneMatrices, m_bones.m_count)` immediately before that `Draw` is the G1 character path. The section indices stay section-local with `m_minIndex == 0`. Dropping the matching CPU job does not move the element.
- Type `0`, optimized. Still one `DrawBatch` of the concatenated range. A later bind works if the bind-pose buffer is packed in the same sub-section order as `WriteSkin` (`M2Scene.cpp:1851`). The element stays where `SortOpaque` / `SortTransparent` put it.
- Type `0`, `texCoord == 1` (`m_shader & 0x4000`). Same `DrawBatch` slot. The CPU kernel bakes `m_texcoord[1]` into the only UV of `GxVBF_PNT`. The G1 shader blends position and normal and leaves both UV sets in the mesh, so this key stays a CPU take until that draw samples UV 1.
- Type `2`. G2 replaces the `FillDoodadSkin` rewrite with one bind-pose buffer and one `GpuSkinBind` plus `Draw` per instance (`docs/gpu-m2-plan.md`). Those draws stay inside `DrawBatchDoodad`, in the current `indices[]` order (`CM2SceneRender.cpp:813`). The group is still visited when the type `2` case runs (`CM2SceneRender.cpp:1167`). Neighboring elements stay put. A full ring returns 0 and `FillDoodadSkin` keeps the CPU loop, which is the path these jobs already feed.

`WriteSkin` is the wrong place to call `GpuSkinBind`.

- The workers run while terrain and the shadow map are drawing. The arm would stick to whichever device `Draw` comes next, which is not this M2 batch.
- The arm is one draw. Pass 1 and pass 2, and the depth-sort copy, each take the same bytes (`M2SkinTake` does not consume them).
- `SkinRange` is parallel (`M2Scene.cpp:1867`). The bone-ring cursor is one device allocation.
- The job writes skinned `PNT`. `GpuSkinBind` expects the mesh to stay in bind pose (`GxVBF_PBNT2`) and the bones to arrive with the draw.
- `GpuSkinBind` returns 0 when `boneCount` is 0, above 256, or the ring is full. `WriteSkin` indexes `m_boneMatrices` with no 256 cap. Those models keep this CPU buffer.

## Why G1 leaves them on the CPU

G1 is done when character batches with `texCoord == 0` stop calling `TransformVertices_*` on the non-shared shaders-off path (`docs/gpu-m2-plan.md`). That edit is `CM2Model::SetVertices` for `!optimized && texCoord == 0`, and `GpuSkinBind` in `DrawBatch`. The scene-job file stays a CPU producer until that path is right.

`WriteSkin` is a wider set than that character key:

| Job shape | G1 disposition |
| --- | --- |
| Type `0`, `optimized == 0`, `texCoord == 0` | The character key. G1 uploads bind pose from `SetVertices` and binds in `DrawBatch`. This job remains the fallback when `GpuSkinBind` returns 0. |
| Type `0`, `optimized != 0` | The `M2Scene.cpp:1859` concatenation. Plan: optimized multi-section stays on the CPU. |
| Type `0`, `texCoord == 1` | Env UV baked at `M2Scene.cpp:1844`. Plan: `texCoord != 0` stays on the CPU. |
| Type `2`, `texCoord == 0`, `optimized == 0` | Multi-bone doodads. That switch is G2 `FillDoodadSkin`, not this campaign. |
| Flag `0x40`, influences above 4, rigid one-bone type `2` | Already outside the job. They stay on their current CPU or rigid draws. |

`M2SkinTake` is the first successful branch in `SetVertices` (`M2Model.cpp:3169`). A G1 character that still consumed these bytes would upload skinned `PNT` and never reach the bind-pose upload. Leaving `WriteSkin` on `s_transformVertices` keeps the fallback and the non-character keys on the CPU, and leaves the sorted walk in `CM2SceneRender::Draw` unchanged.
