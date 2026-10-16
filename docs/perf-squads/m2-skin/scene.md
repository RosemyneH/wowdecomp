# Campaign 1 scene

Golden check on the Campaign 0 city camera. Same character position and same camera before and after each slice. The sample that closes the budget is a still camera, after instance capacity has already stepped up.

Fixed function: `CShaderEffect::s_enableShaders == 0`, so bones stay on the CPU (`CM2SceneRender.cpp:83`, `CM2SceneRender.cpp:717`). Leave `M2BatchDoodads` at `1` so cache flag `0x20` stays set (`WoW/Source/Client_CVar.cpp:700`, `WoW/Source/Client_CVar.cpp:546`). Cache flag `0x8` stays clear; if it is set, `DrawRigidDoodads` returns before the mesh path (`CM2SceneRender.cpp:613`).

## The shot

One view, two populations.

**Several skinned characters.** Three or more `CM2Model`s with more than one bone. They fail `IsBatchDoodadCompatible` (`Engine/Source/Model2/M2Model.cpp:431`) and are element type `0` (`Engine/Source/Model2/M2Scene.cpp:1851`). `Draw` sends them through `DrawBatch` (`Engine/Source/Model2/CM2SceneRender.cpp:1118`). The shared-vertex shortcut is one bone plus cache flag `0x40` (`Engine/Source/Model2/CM2SceneRender.cpp:462`); these models miss it, so `SetBatchVertices` calls `SetVertices` or `SetVerticesMultiSample` (`Engine/Source/Model2/CM2SceneRender.cpp:186`). Each character has more than one batch, so a second batch of the same model is in the frame. Idle, or a loop that does not translate them. They stay in frame for the whole hold.

**A dense doodad area that grows instance capacity.** Many placements of one shared M2, batched as element type `2` (`Engine/Source/Model2/M2Scene.cpp:1854`). A group of one is demoted to type `0` (`Engine/Source/Model2/M2Scene.cpp:2172`); the cluster must stay grouped, with `m_count = j - i` (`Engine/Source/Model2/M2Scene.cpp:2179`) greater than 16. `DrawBatchDoodad` passes that count to `AllocInstances` (`Engine/Source/Model2/CM2SceneRender.cpp:710`). `m_instances` starts at 1 (`Engine/Source/Model2/M2Shared.cpp:1150`). When `count` is larger and `m_maxInstances` allows it (`Engine/Source/Model2/M2Shared.cpp:1136`, `Engine/Source/Model2/M2Shared.cpp:772`), capacity becomes `(count + 15) & ~15` (`Engine/Source/Model2/M2Shared.cpp:773`). Use a simple repeated prop so `m_maxInstances` is above that step (`Engine/Source/Model2/M2Shared.cpp:775`).

Frame the camera so both populations are visible together. The approach into the cluster is what steps capacity. The pass is the hold after the last step.

Also in frame, so the picture covers the paths that must not change:

- An env-map batch, `m_shader & 0x4000` (`Engine/Source/Model2/CM2SceneRender.cpp:179`).
- A multi-sample batch, `m_flags & 0x40` (`Engine/Source/Model2/CM2SceneRender.cpp:187`, flag set at `Engine/Source/Model2/M2Shared.cpp:1241`).

## Hold

1. Walk the city camera into the cluster until capacity has stepped (`m2_instance_grow` below). Today that step calls `DestroyBuffers` (`Engine/Source/Model2/M2Shared.cpp:779`). That approach line is the witness that the area grew. It is not the pass.
2. Stop. Do not move the player or the camera. The visible set stays the same models and the same doodad `m_count`.
3. Wait out the unused-shared sweep. `CM2Cache::UpdateShared` calls `DestroyBuffers` when `m_lastUsedTime` is 10000 ms old (`Engine/Source/Model2/M2Cache.cpp:409`). Models still drawn are relinked every frame (`Engine/Source/Model2/CM2SceneRender.cpp:1115`, `Engine/Source/Model2/M2Cache.cpp:421`) and are not swept. Shares that fell out of the frustum on the way in can fire once. Let that spike pass.
4. Read at least three one-second log lines with the camera still. No model load or unload in that window, so the destructor path (`Engine/Source/Model2/M2Shared.cpp:1882`) stays out.

## What must look the same

Same pixels as the Campaign 0 city frame of this camera, and the same pixels across the capacity step. A rounded-up `m_instances` still draws `m_count` (`Engine/Source/Model2/CM2SceneRender.cpp:713`).

- **Who is drawn, and where.** Character placement and bone pose. Rigid copies use `m_boneMatrices[bone]` (`Engine/Source/Model2/CM2SceneRender.cpp:641`). No missing instance, no slide, no T-pose when the pools grow or stop being destroyed.
- **Sort.** Opaque order is `SortOpaque` → `SortOpaqueGeoBatches` (`Engine/Source/Model2/M2Scene.cpp:1249`, `Engine/Source/Model2/M2Scene.cpp:1052`, sort at `Engine/Source/Model2/M2Scene.cpp:2186`). Transparent order is depth, then the split flag, then `m_priorityPlane` (`Engine/Source/Model2/M2Scene.cpp:1277`, sort at `Engine/Source/Model2/M2Scene.cpp:2187`). Doodad groups keep `SortDoodadProxy` (`Engine/Source/Model2/M2Scene.cpp:1038`, `Engine/Source/Model2/M2Scene.cpp:2147`).
- **Materials.** Blend, alpha ref, cull, depth write (`Engine/Source/Model2/CM2SceneRender.cpp:284`).
- **Env-map UVs.** Sphere-map texgen (`Engine/Source/Model2/CM2SceneRender.cpp:118`) and the `0x4000` texcoord picked for the CPU skin (`Engine/Source/Model2/CM2SceneRender.cpp:190`, written at `Engine/Source/Model2/M2Model.cpp:3156`).
- **Multi-sample sections.** Flag `0x40` still goes through `SetVerticesMultiSample` (`Engine/Source/Model2/CM2SceneRender.cpp:187`, `Engine/Source/Model2/M2Model.cpp:4519`).
- **Shader-on branch.** Not this counter sample. One extra still of the same camera with shaders on must match the picture. That branch already binds shared vertices (`Engine/Source/Model2/CM2SceneRender.cpp:171`, `Engine/Source/Model2/CM2SceneRender.cpp:730`).

## Counters

Campaign 0’s one-second line. These fields are atomics at the sites below.

### One skin per model per frame

| Field | What it counts |
| --- | --- |
| `m2_cpu_skin` | CPU skins. One per call. |
| `m2_skin_models` | Distinct `CM2Model*` among those calls, this frame. |

`m2_cpu_skin` increments only at:

- `CM2Model::SetVertices` → `s_transformVertices` (`Engine/Source/Model2/M2Model.cpp:2717`, `Engine/Source/Model2/M2Model.cpp:2722`)
- `CM2Model::SetVerticesMultiSample` → `TransformVerticesNoUVSelect_cpp` (`Engine/Source/Model2/M2Model.cpp:4536`, `Engine/Source/Model2/M2Model.cpp:4541`)
- `FillDoodadSkin` → `s_transformVertices` (`Engine/Source/Model2/CM2SceneRender.cpp:603`)

`DrawRigidDoodads` rewrites vertices at `Engine/Source/Model2/CM2SceneRender.cpp:644`. That is not a skin and must not increment `m2_cpu_skin`.

**Pass, every still frame:** `m2_cpu_skin == m2_skin_models`. The count is at least the skinned characters in view (three or more) and it stays flat across the three still seconds. A second batch of the same model (`Engine/Source/Model2/CM2SceneRender.cpp:186`) that skins again makes `m2_cpu_skin` larger than `m2_skin_models`.

### No `DestroyBuffers` while the camera is still

| Field | What it counts |
| --- | --- |
| `m2_destroy_buffers` | Entries to `CM2Shared::DestroyBuffers` (`Engine/Source/Model2/M2Shared.cpp:602`). |
| `m2_instance_grow` | Capacity steps at `Engine/Source/Model2/M2Shared.cpp:773`. Witness for the approach, not the still pass. |

Callers of `DestroyBuffers` are the capacity step (`Engine/Source/Model2/M2Shared.cpp:779`), the 10s sweep (`Engine/Source/Model2/M2Cache.cpp:413`), and the destructor (`Engine/Source/Model2/M2Shared.cpp:1882`). The hold is defined so the last two are already quiet.

**Pass, each still second:** `m2_destroy_buffers` does not increase, and `m2_instance_grow` does not increase. The approach line before the hold must show `m2_instance_grow` greater than zero, so the zero is the peak of a cluster that grew, not a view that never called `AllocInstances`.
