# m2-skin integrator

Campaign 1, skin half. `m2-rigid` edits `DrawRigidDoodads` in the same translation unit. This squad owns the CPU-skin frame cache and instance-capacity hysteresis. One writer at a time. Land the three slices below in order. Do not retouch a function the other squad owns.

## Merge order

1. **`m2-rigid` — static bind.** `DrawRigidDoodads` only (`Engine/Source/Model2/CM2SceneRender.cpp:607-693`). Freeze the signature at `608` and the call at `717-719`. After this slice a one-bone batch returns `1` and uploads matrices only. It binds the shared static buffers from `CM2Shared::SetVertices` / `SetIndices`. It does not rewrite mesh bytes through `BufStream`.
2. **`m2-skin` — frame cache.** `FillDoodadSkin` (`CM2SceneRender.cpp:598-605`), the shader-off loop that fills it (`770-820`), and `CM2Model::SetVertices` (`Engine/Source/Model2/M2Model.cpp:2700-2729`) with its call in `SetBatchVertices` (`CM2SceneRender.cpp:186-191`). Skinned output goes to a dynamic pool keyed by `(model, section, frame)`. It does not go in the stream ring `DrawRigidDoodads` still locks until slice 1 lands (`644-663`).
3. **`m2-skin` — `AllocInstances` hysteresis.** `Engine/Source/Model2/M2Shared.cpp:771-783`. Double capacity in place. Delete the `DestroyBuffers()` call at `779`. Land this last so the `CGxBuf*` values slice 1 binds (`m_vertexBuf` at `M2Shared.h:67`, `m_indexBuf` at `M2Shared.h:65`) stay valid across a grow.

Slice 2 and slice 1 touch disjoint line ranges in `CM2SceneRender.cpp`, so the textual merge is the rigid function, then the skin loop under it. Slice 3 is a different file. Apply slice 3 only after both binds exist, and check that a capacity step does not free those buffers.

## Do not edit by both

| Function | Owner | Range | The other squad |
| --- | --- | --- | --- |
| `DrawRigidDoodads` | `m2-rigid` | `CM2SceneRender.cpp:607-693` | Skin does not edit. Includes the comment at `607`. |
| `FillDoodadSkin` | `m2-skin` | `CM2SceneRender.cpp:598-605` | Rigid does not edit. |
| `DoodadSkin` | `m2-skin` | `CM2SceneRender.cpp:588-596` | Argument block for `FillDoodadSkin`. Rigid does not edit. |
| `CM2Model::SetVertices` | `m2-skin` | `M2Model.cpp:2700-2729` | The frame cache. Rigid does not edit. Declaration stays `M2Model.h:370`. |
| `CM2Shared::AllocInstances` | `m2-skin` | `M2Shared.cpp:771-783` | Rigid does not edit. Declaration stays `M2Shared.h:85`. |

`DrawBatchDoodad` (`CM2SceneRender.cpp:697-822`) is the only function whose body contains both call sites. Split it by line. Neither squad edits the other's range.

| Lines | Who |
| --- | --- |
| `697-708` | Neither. Material, lighting, textures. |
| `710-715` | Neither. `AllocInstances(total)` and the clamp down to `total`. |
| `717-719` | Neither squad. Integrator only, and only if the rigid signature has to change. |
| `721` | Neither. `SetIndices` for the fall-through path. |
| `730-769` | Neither. Shader-on branch. Already uses shared vertices. |
| `770-820` | `m2-skin` only. Shader-off loop. Calls `FillDoodadSkin` at `795` and `797`. |

`SetBatchVertices` (`CM2SceneRender.cpp:170-193`) is the other `SetVertices` split:

| Lines | Who |
| --- | --- |
| `171-185` | Neither. Calls `CM2Shared::SetVertices` (`173`, `184`). |
| `186-191` | `m2-skin` only. `SetVerticesMultiSample` at `188` stays a call. The cache wraps `CM2Model::SetVertices` at `190`. |

## Not the skin cache

`CM2Shared::SetVertices` (`M2Shared.cpp:388-460`) is the static upload. `m_hasData` already skips a second fill (`399`, `430`). Rigid may **call** it from `DrawRigidDoodads`. Skin must not turn it into the skinned frame cache. Same rule for `CM2Shared::SetIndices` (`M2Shared.cpp:338-384`): call it, do not rewrite the fill.

`CM2Shared::DestroyBuffers` (`M2Shared.cpp:602-632`) stays the teardown used by the idle reclaim (`M2Cache.cpp:413`) and the destructor (`M2Shared.cpp:1882`). Skin removes the grow call at `M2Shared.cpp:779`. Neither squad rewrites the body.

Leave the skin kernels alone: `TransformVertices_cpp` (`M2Model.cpp:3101`), `TransformVertices_sse` (`M2Model.cpp:3183`), `TransformVertices_avx2` (`M2SkinAvx2.cpp:110`), and the dispatch table (`M2Model.cpp:3871-3892`). `FillDoodadSkin` keeps calling `s_transformVertices` at `CM2SceneRender.cpp:603`.

`CM2Model::SetVerticesMultiSample` (`M2Model.cpp:4519-4548`) stays the multi-sample path.

## Contracts that keep the merge building

- `DrawRigidDoodads` returns `1` only when that batch is finished. Return `0` (`609`, `614`, `622`, `627`, `633`, `648`, `671`) means `FillDoodadSkin` still owns it, including a `m_boneCount == 1` section the rigid predicate rejected.
- The call passes `total`, the batch count (`710`, `717`), not the clamped `instances`. `AllocInstances` may return a doubled capacity. The clamp at `713-715` keeps the skin loop on the real count. Do not pass capacity into `DrawRigidDoodads`.
- Rigid re-reads `m_vertexBuf` / `m_indexBuf` every call, after `AllocInstances` at `711`. It does not cache those pointers across a grow.
- The skin pool is not `m_vertexBuf`. That buffer is the unskinned mesh (fixed-function `PNT2` at `M2Shared.cpp:400-425`, shader `PBNT2` at `430-457`).
