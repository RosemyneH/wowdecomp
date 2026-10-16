# Campaign 1 — CPU skin counters

Vulkan is on the fixed-function path (`CShaderEffect::s_enableShaders == 0`). Bones are applied on the CPU. Two atomics, no behavior change. Time the skin body with `OsGetAsyncTimeClocks` (`Engine/Source/Os/Linux/TimeManager.cpp:59`).

Tick the skin counter only inside the `TransformVertices_*` function that writes the section. `FillDoodadSkin` and `CM2Model::SetVertices` are the callers. Ticking them as well counts the same skin twice. The SSE unaligned forward calls the C++ body; only the body that writes vertices ticks.

Tick `DestroyBuffers` calls once, at entry of `CM2Shared::DestroyBuffers`. `AllocInstances` is the growth caller. Ticking that call as well counts one growth twice.

| Counter | What one tick is |
| --- | --- |
| `skins_per_model_per_frame` | One CPU skin of one `(CM2Model, M2SkinSection)` |
| `destroy_buffers_calls` | One entry to `CM2Shared::DestroyBuffers` |

`skin_clocks` accumulates `OsGetAsyncTimeClocks` across the same skin bodies. Add it next to the skin tick. Do not divide in the hot path. A one-second log line divides skins by the frames in that second.

## How often a skin runs

A world frame draws `M2PASS_0`, then `M2PASS_1` and `M2PASS_2` (`WoW/Source/Ui/WorldFrame.cpp:1863`, `:1875`, `:1878`, `:1880`, `:1884`). `CM2Scene::Draw` draws that pass’s elements, and draws the doodad list only on `M2PASS_0` (`Engine/Source/Model2/M2Scene.cpp:2222`, `:2225`).

Shader-on draws upload bone constants and do not skin (`CM2SceneRender.cpp:171`, `:730`). The one-bone shared path (`m_bones.m_count == 1` and cache flag `0x40`) copies bind-pose vertices in `CM2Shared::SetVertices` and does not skin (`CM2SceneRender.cpp:462`, `:182`). `DrawRigidDoodads` copies bind-pose vertices and instances matrices; it returns before `FillDoodadSkin` (`CM2SceneRender.cpp:717`).

### `FillDoodadSkin`

`Engine/Source/Model2/CM2SceneRender.cpp`, `FillDoodadSkin`, line 603. Counter: `skins_per_model_per_frame`.

```598:604:Engine/Source/Model2/CM2SceneRender.cpp
static void FillDoodadSkin(Job::Range range, void* user) {
    DoodadSkin* skin = static_cast<DoodadSkin*>(user);

    for (uint32_t n = range.begin; n < range.end; ++n) {
        CM2Model* model = skin->elements[skin->indices[skin->done + n]].m_model;
        CM2Model::s_transformVertices[skin->influences](model, skin->section, skin->vertices + n * skin->stride, 0);
    }
}
```

`DrawBatchDoodad` reaches this only when shaders are off and `DrawRigidDoodads` returns 0. The loop at `CM2SceneRender.cpp:771` walks the batch in chunks of `AllocInstances`. Each chunk calls `FillDoodadSkin` once, or `Job::ParallelFor` when the chunk has at least 8 instances (`CM2SceneRender.cpp:795`, `:797`).

Each iteration of line 603 is one skin of that instance’s model and this batch’s section. The instances are partitioned across chunks and jobs, so a model in the batch is skinned once here. Tick at the callee, not at 795 or 797: a job split would otherwise multiply the count.

Doodad batches are type 2 and run on `M2PASS_0` only. A single doodad is demoted to type 0 and leaves this path (`M2Scene.cpp:2172`). A model with several sections is several batches (`HashDoodadElement` includes `m_index`, `M2Scene.cpp:205`), so it skins once per section on that pass.

### `CM2Model::SetVertices`

`Engine/Source/Model2/M2Model.cpp`, `CM2Model::SetVertices`, lines 2717 and 2722. Counter: `skins_per_model_per_frame`.

```2708:2723:Engine/Source/Model2/M2Model.cpp
    if (optimized) {
        M2SkinProfile* skin = this->m_shared->m_skinData;
        M2BatchRange* range = &this->m_optGeo->m_batchRanges[section->m_skinSectionId];

        for (uint32_t b = range->m_first; b <= range->m_last; b++) {
            uint16_t sectionIndex = skin->m_batches[b].m_skinSectionIndex;

            if (this->m_skinSections[sectionIndex]) {
                M2SkinSection* batchSection = &skin->m_skinSections[sectionIndex];
                CM2Model::s_transformVertices[batchSection->m_boneInfluences](this, batchSection, vertices, texCoord);
                vertices += 32 * batchSection->m_vertexCount;
            }
        }
    } else {
        CM2Model::s_transformVertices[section->m_boneInfluences](this, section, vertices, texCoord);
    }
```

`SetBatchVertices` calls this for a type 0 batch when shaders are off, the shared one-bone path is off, and the batch flag `0x40` is clear (`CM2SceneRender.cpp:186`, `:190`). The optimized arm (2717) skins every live section in the opt-geo range. The other arm (2722) skins the element’s section once.

The call is skipped when the previous element has the same type, model, skin section, and env-map bit (`m_shader & 0x4000`, `CM2SceneRender.cpp:179`, `:186`). The check is only against the previous element. A later repeat of the same `(model, section)` in that pass skins again. Pass 0, pass 1, and pass 2 each have their own element list, so a model on all three passes skins its sections on each pass.

Batch flag `0x40` takes `SetVerticesMultiSample` instead (`CM2SceneRender.cpp:188`), which skins through `TransformVerticesNoUVSelect_cpp` at `M2Model.cpp:4536` (one per live section in the opt range) and `:4541` (one section).

### `TransformVertices_*`

Counter: `skins_per_model_per_frame`, plus `skin_clocks` around the write loop. `s_transformVertices[1]` is the one-bone body. Indices 0 and 2–4 share the blended body (`M2Model.cpp:3871`). AVX2 wins when `WOWDECOMP_AVX2_SKIN` is set and `OsHasAvx2()` is true; otherwise SSE when `CM2Cache::s_processorFeatures & 4`; otherwise the C++ body (`M2Model.cpp:3878`, `:3887`).

| Function | File | Line | Tick |
| --- | --- | --- | --- |
| `CM2Model::TransformVertices_cpp` | `Engine/Source/Model2/M2Model.cpp` | 3101 | +1 at entry |
| `CM2Model::TransformVerticesOneBone_cpp` | `Engine/Source/Model2/M2Model.cpp` | 3162 | +1 at entry |
| `CM2Model::TransformVertices_sse` | `Engine/Source/Model2/M2Model.cpp` | 3183 | +1 after the unaligned forward at 3185 |
| `CM2Model::TransformVerticesOneBone_sse` | `Engine/Source/Model2/M2Model.cpp` | 3235 | +1 after the unaligned forward at 3237 |
| `CM2Model::TransformVertices_avx2` | `Engine/Source/Model2/M2SkinAvx2.cpp` | 110 | +1 at entry |
| `CM2Model::TransformVerticesOneBone_avx2` | `Engine/Source/Model2/M2SkinAvx2.cpp` | 139 | +1 at entry |
| `CM2Model::TransformVerticesNoUVSelect_cpp` | `Engine/Source/Model2/M2Model.cpp` | 3044 | +1 at entry |

```3183:3186:Engine/Source/Model2/M2Model.cpp
void CM2Model::TransformVertices_sse(CM2Model* model, M2SkinSection* section, uint8_t* vertices, int32_t texCoord) {
    if (reinterpret_cast<uintptr_t>(vertices) & 0xF) {
        CM2Model::TransformVertices_cpp(model, section, vertices, texCoord);
        return;
```

The one-bone SSE forward is the same shape at `M2Model.cpp:3236`.

On a steady camera the budget is one skin per `(model, section)` per pass that draws it. Today `FillDoodadSkin` already does that for a doodad batch, and `SetBatchVertices` does it for adjacent repeats. The count is higher when the same section is drawn on another pass, when a non-adjacent element repeats the pair, or when the env-map bit flips and the same section is skinned for the other UV set.

## `DestroyBuffers` calls

`Engine/Source/Model2/M2Shared.cpp`, `CM2Shared::DestroyBuffers`, line 602. Counter: `destroy_buffers_calls`. +1 at entry, before the pool teardown.

```602:602:Engine/Source/Model2/M2Shared.cpp
void CM2Shared::DestroyBuffers() {
```

Three callers:

| Caller | File | Line | When it fires |
| --- | --- | --- | --- |
| `CM2Shared::AllocInstances` | `Engine/Source/Model2/M2Shared.cpp` | 779 | Capacity steps up |
| `CM2Cache::UpdateShared` | `Engine/Source/Model2/M2Cache.cpp` | 413 | Shared idle for 10 seconds |
| `CM2Shared::~CM2Shared` | `Engine/Source/Model2/M2Shared.cpp` | 1882 | Shared is destroyed |

### `CM2Shared::AllocInstances`

`Engine/Source/Model2/M2Shared.cpp`, `CM2Shared::AllocInstances`, line 779. Counter: `destroy_buffers_calls` (the callee ticks).

```771:782:Engine/Source/Model2/M2Shared.cpp
uint32_t CM2Shared::AllocInstances(uint32_t count) {
    if (this->m_instances < count && this->m_instances < this->m_maxInstances) {
        this->m_instances = (count + 15) & ~15u;

        if (this->m_instances > this->m_maxInstances) {
            this->m_instances = this->m_maxInstances;
        }

        this->DestroyBuffers();
    }

    return this->m_instances;
}
```

`m_instances` starts at 1 (`M2Shared.cpp:1150`). A larger `count` rounds up to a multiple of 16 and caps at `m_maxInstances` (`M2Shared.cpp:1136`). Each step up destroys the shared index and vertex pools. Capacity does not shrink, so a second batch of the same size does not destroy.

`DrawBatchDoodad` calls `AllocInstances` on every type 2 batch, before the rigid return (`CM2SceneRender.cpp:711`). `RenderBatchShadowMap` calls it on every shadow batch (`M2Model.cpp:2937`). A rigid or shadow batch can still destroy pools when it is the batch that steps capacity up. It does not skin.

On a steady camera, after the peak batch size, line 779 does not run. `UpdateShared` still destroys buffers for a shared that `Draw` has not touched for 10 seconds (`M2Cache.cpp:409`). `Draw` refreshes `m_lastUsedTime` for every element it walks (`CM2SceneRender.cpp:1115`). The destructor runs when the shared is released, not on a steady view of the same models.

Budget read: `destroy_buffers_calls` stays 0 for the shareds on screen once capacity has reached the peak. A non-zero sample on a steady camera is an idle shared crossing 10 seconds, or a shared being destroyed.
