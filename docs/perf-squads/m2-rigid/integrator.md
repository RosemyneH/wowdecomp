# m2-rigid integrator

Campaign 1, one-bone static bind. This squad lands first on the two Model2 files it shares with m2-skin. It does not enter `DrawInstanced`.

Group A owns `CM2SceneRender`, `M2Shared`, and the skinning entry points (`docs/perf-agent-plan.md:45`). Group B is uniforms, then vulkan: campaign 6 starts when campaign 2 closes (`docs/perf-agent-plan.md:46`).

## Merge order

Two sequences. They meet only if a hunk crosses into `DrawInstanced`. A crossing hunk from this squad is dropped.

### 1. m2-rigid, then m2-skin

Same files:

- `Engine/Source/Model2/CM2SceneRender.cpp`
- `Engine/Source/Model2/M2Shared.h`
- `Engine/Source/Model2/M2Shared.cpp`

`DrawRigidDoodads` returns `0` when the section is not one static bone (`Engine/Source/Model2/CM2SceneRender.cpp:609`, `:614`, `:622`, `:627`, `:633`) and returns `1` only after `DrawInstanced` (`Engine/Source/Model2/CM2SceneRender.cpp:691-692`). `DrawBatchDoodad` treats that `1` as the batch already drawn (`Engine/Source/Model2/CM2SceneRender.cpp:717-719`). The CPU skin loop is the residual (`Engine/Source/Model2/CM2SceneRender.cpp:770-820`).

m2-skin lands on a tree that already has that gate. Its frame cache is for sections this function refused. Landing skin first lets a rewrite of `DrawBatchDoodad` delete the gate, so one-bone sections are cached and then skipped.

On a `DrawBatchDoodad` conflict, keep:

| Lines | From |
| --- | --- |
| `Engine/Source/Model2/CM2SceneRender.cpp:697-716` | Neither. Setup stays. |
| `Engine/Source/Model2/CM2SceneRender.cpp:717-719` | m2-rigid. The early return. |
| `Engine/Source/Model2/CM2SceneRender.cpp:721-769` | Neither. `SetIndices` and the shader-on branch stay. Shader-on already binds shared vertices at `:731`. |
| `Engine/Source/Model2/CM2SceneRender.cpp:770-820` | m2-skin. |

A failed rigid bind still returns `0` (`Engine/Source/Model2/CM2SceneRender.cpp:648`, `:671` today). Skin must still be able to draw that batch.

`CM2Shared` layout comments stop at `m_updateLink` (`Engine/Source/Model2/M2Shared.h:74`). Both squads append after that member, and append methods after `SubstituteSpecializedShaders` (`Engine/Source/Model2/M2Shared.h:108`). Rigid's fields and declarations come first. Skin appends after them. Neither inserts above `:74`.

`DestroyBuffers` (`Engine/Source/Model2/M2Shared.cpp:602-632`) keeps the four existing teardowns through `:631`. A rigid append follows. A skin append follows that.

### 2. uniforms, then vulkan

`CGxDeviceVulkan::DrawInstanced` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1478-1500`) calls `Draw` at `:1499`. `Draw` (`:1183`) calls `IBuildUniforms` at `:1359` (`IBuildUniforms` is `:971`) and binds the descriptor set at `:1442`. The D3D fallback is `CGxDevice::DrawInstanced` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:127-144`). Declarations: `Engine/Source/Gx/CGxDevice/CGxDevice.h:218`, `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h:159`.

uniforms owns the pack and the bind. vulkan owns buffer lifetime in the same function, and only after uniforms has closed. This squad's only contact is the call at `Engine/Source/Model2/CM2SceneRender.cpp:691`.

## Functions this squad may own

| Function | Where | Allowed change |
| --- | --- | --- |
| `DrawRigidDoodads` | `Engine/Source/Model2/CM2SceneRender.cpp:608-693` | Whole function. Replace the per-batch stream rewrite (`:644-683`) with a bind of the static section mesh. Keep the one-bone proof (`:609-634`), the matrix gather (`:636-642`), and the `DrawInstanced` call (`:685-692`). |
| `CM2SceneRender::DrawBatchDoodad` | `Engine/Source/Model2/CM2SceneRender.cpp:717-719` only | The early return that calls `DrawRigidDoodads`. |
| One new `CM2Shared` bind | Declare after `Engine/Source/Model2/M2Shared.h:108`. Define beside, not inside, `SetVertices`. Fields after `Engine/Source/Model2/M2Shared.h:74`. | Upload a one-bone section once into a static pool and bind it. Teardown is an append after `Engine/Source/Model2/M2Shared.cpp:631`. |

The mesh buffers already on the shared object are `m_indexPool`, `m_indexBuf`, `m_vertexPool`, `m_vertexBuf` (`Engine/Source/Model2/M2Shared.h:64-67`), filled by `SetIndices` (`Engine/Source/Model2/M2Shared.cpp:338-384`) and `SetVertices` (`Engine/Source/Model2/M2Shared.cpp:388-459`). This squad may call them. It does not edit them.

## Hands off

m2-skin:

- `DoodadSkin` (`Engine/Source/Model2/CM2SceneRender.cpp:588-596`)
- `FillDoodadSkin` (`Engine/Source/Model2/CM2SceneRender.cpp:598-605`)
- `DrawBatchDoodad` skin loop (`Engine/Source/Model2/CM2SceneRender.cpp:770-820`)
- `CM2Model::SetVertices` (`Engine/Source/Model2/M2Model.cpp:2700-2729`)
- `CM2Model::TransformVertices_cpp` (`Engine/Source/Model2/M2Model.cpp:3101`), `TransformVertices_sse` (`Engine/Source/Model2/M2Model.cpp:3183`), `TransformVertices_avx2` (`Engine/Source/Model2/M2SkinAvx2.cpp:110`)

Later Campaign 1 slice, not this squad: `CM2Shared::AllocInstances` (`Engine/Source/Model2/M2Shared.cpp:771-783`), called from `DrawBatchDoodad` at `Engine/Source/Model2/CM2SceneRender.cpp:711` before the gate. It calls `DestroyBuffers` at `Engine/Source/Model2/M2Shared.cpp:779`. Do not edit the body.

uniforms and vulkan:

- `CGxDeviceVulkan::DrawInstanced` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1478-1500`)
- `CGxDevice::DrawInstanced` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:127-144`)
- `CGxDeviceVulkan::Draw` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1183`) and `IBuildUniforms` (`:971`, call at `:1359`, bind at `:1442`)

Leave the non-doodad path in place. `DrawBatch` (`Engine/Source/Model2/CM2SceneRender.cpp:418-494`) already draws the single-bone model flag (`:462-488`). `DrawRigidDoodads` returns `0` for that flag (`:613-615`). `SetBatchVertices` (`Engine/Source/Model2/CM2SceneRender.cpp:170-193`) owns the env-map UV choice (`:179-184`). `SetupMaterial` owns the one-bone normal-normalize (`Engine/Source/Model2/CM2SceneRender.cpp:325-330`).
