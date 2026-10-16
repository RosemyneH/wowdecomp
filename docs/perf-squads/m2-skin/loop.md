# Campaign 1 — M2 skin loop

**NEXT SLICE**

Cycle 1. No skin slice has landed. The verdict is from the source. There is no Campaign 0 frame log to diff. `m2-rigid` still owns `DrawRigidDoodads`.

## Budget

| Line | Result | Code |
| --- | --- | --- |
| One CPU skin per model section per frame | Open | Transform runs on every batch entry. Nothing keys `(model, section, frame)`. |
| `DestroyBuffers` stays off the steady camera | Held | The grow call is behind `m_instances < count`. A steady view never takes it. |

### One CPU skin

Shaders are off on this path (`CShaderEffect::s_enableShaders == 0`). A skin is one call to `CM2Model::s_transformVertices` or `TransformVerticesNoUVSelect_cpp`.

`CM2Model::SetVertices` (`Engine/Source/Model2/M2Model.cpp:2700-2723`) stream-locks a fresh vertex buffer and transforms the section on every entry. The optimized loop (`M2Model.cpp:2712-2718`) does that once per sub-section. `SetVerticesMultiSample` (`M2Model.cpp:4519-4541`) does it again through `TransformVerticesNoUVSelect_cpp`.

`SetBatchVertices` (`Engine/Source/Model2/CM2SceneRender.cpp:186-191`) skips that only when the previous element in this `CM2SceneRender` is the same model, the same section, and the same env bit (`CM2SceneRender.cpp:179-180`). `m_prevModel` and `m_prevSkinSection` start null (`CM2SceneRender.cpp:40`, `CM2SceneRender.cpp:53`). `CM2Scene::Draw` constructs a new render every pass (`Engine/Source/Model2/M2Scene.cpp:2221-2222`).

`WorldFrame` draws pass 0, pass 1, and pass 2 (`WoW/Source/Ui/WorldFrame.cpp:1863`, `WorldFrame.cpp:1875`, `WorldFrame.cpp:1878-1884`). A transparent batch that is both above and below the liquid plane is queued in pass 1 and pass 2 (`M2Scene.cpp:1944-1949`). Those are two renders, so the previous-element test cannot see the first skin. The same `(model, section)` is transformed twice in that frame.

`FillDoodadSkin` (`CM2SceneRender.cpp:598-604`) has no previous-element test and no frame stamp. The shader-off doodad loop (`CM2SceneRender.cpp:770-798`) stream-locks `instances * vertexCount` bytes and runs it for every instance, in parallel when `instances >= 8` (`CM2SceneRender.cpp:794-797`). `HashDoodadElement` mixes `m_index` into the doodad key (`M2Scene.cpp:211`), and each key becomes its own group (`M2Scene.cpp:2151-2179`). Two batches that share a skin section are two groups, and each group transforms that model section again. The next `BufLock` drops the stream copy.

`CM2Scene::m_frameStamp` increments once per `Animate` (`M2Scene.cpp:1702`). Skinning never reads it. `CM2Model::m_frameStamp` (`M2Model.h:154`) is the animation stamp, not a skin stamp.

### DestroyBuffers

`CM2Shared::AllocInstances` (`Engine/Source/Model2/M2Shared.cpp:771-782`) calls `DestroyBuffers` (`M2Shared.cpp:779`) only when `m_instances < count` and `m_instances < m_maxInstances`. Capacity moves up and stays. After the peak batch count, a steady camera’s `m_curElement->m_count` (`CM2SceneRender.cpp:710-711`) is already inside `m_instances`, and the call does not run.

`CM2Cache::UpdateShared` (`Engine/Source/Model2/M2Cache.cpp:409-413`) destroys a shared only when `m_updateTime - m_lastUsedTime` is at least 10 seconds, then breaks on the first newer node. `Draw` refreshes every drawn shared (`CM2SceneRender.cpp:1115`, `M2Cache.cpp:421-423`) and links it to the tail. The steady view stays inside that window.

The destructor call (`M2Shared.cpp:1882`) is not a frame path. The idle reclaim at `M2Cache.cpp:413` stays. It is the teardown for shareds that have not been drawn for 10 seconds.

A grow still rounds up to a multiple of 16 and destroys (`M2Shared.cpp:773-779`). That is the arrival spike. It is the hysteresis slice, after this one.

## Next writer task

**Frame cache for CPU skin.** One concern. The rigid static bind and `AllocInstances` hysteresis stay out of this change.

1. Key the cache by model, skin section, and `CM2Scene::m_frameStamp` (`M2Scene.cpp:1702`). One transform per key per frame. A later batch in pass 1, pass 2, or another doodad group reuses those bytes.
2. Keep the records where `CM2SceneRender` cannot see them. Each `CM2Scene::Draw` builds a new render (`M2Scene.cpp:2221`). The cache is not the stream ring from `BufStream`, and it is not `CM2Shared::m_vertexBuf` (the unskinned static mesh at `M2Shared.cpp:398-425`).
3. Leave the offset-pinned `CM2Model` prefix through `unused_02EC` (`M2Model.h:223`) in place. A pointer may append after that member. Shared-pool fields, if this slice needs them, append after `m_updateLink` (`M2Shared.h:74`) and after any `m2-rigid` fields already appended there.
4. `FillDoodadSkin` (`CM2SceneRender.cpp:598-604`): a hit copies the cached section into `vertices + n * stride`. A miss transforms once with texCoord `0` (`CM2SceneRender.cpp:603`), stores the bytes, then copies. `Job::ParallelFor` stays at `CM2SceneRender.cpp:794`. Each `n` is one model and writes that model’s record. The doodad group still draws once (`CM2SceneRender.cpp:819`).
5. `CM2Model::SetVertices` (`M2Model.cpp:2700-2723`), including each `batchSection` in the optimized loop (`M2Model.cpp:2712-2718`), and `SetVerticesMultiSample` (`M2Model.cpp:4519-4541`) consult the same key. `SetBatchVertices` keeps both calls (`CM2SceneRender.cpp:188`, `CM2SceneRender.cpp:190`).
6. Env batches (`m_shader & 0x4000`, `CM2SceneRender.cpp:179-190`) and the other batches of that section read different source UVs, and both currently land in the single `GxVBF_PNT` slot. The one transform writes each variant that a later batch of this section will bind. A later batch selects its variant. It does not call `s_transformVertices` or `TransformVerticesNoUVSelect_cpp` again. If one walk cannot feed those batches without changing the UV the fixed-function draw already reads, stop this slice and leave it unlanded. That is a Faith reject of the approach.
7. `DrawRigidDoodads` (`CM2SceneRender.cpp:607-693`), the early return at `CM2SceneRender.cpp:717-719`, the shader-on branch (`CM2SceneRender.cpp:730-769`), `AllocInstances` (`M2Shared.cpp:771-783`), `DestroyBuffers` (`M2Shared.cpp:602-632`), and the transform kernels (`M2Model.cpp:3101`, `M2Model.cpp:3183`, `Engine/Source/Model2/M2SkinAvx2.cpp:110`) stay as they are.

After this lands, the next loop remeasures skins per `(model, section, m_frameStamp)`. The following writer slice is hysteresis: double `m_instances` in place and drop the `DestroyBuffers` call at `M2Shared.cpp:779`.
