# G1 GPU skin — Counter

Counters and `OsGetAsyncTimeClocks` zones only. No behavior change. `GpuSkinBind` / `GpuSkinSupported` are not in the tree yet; this file names the sites to wire when Device, Bind pose, and Draw land them. Field names below stay fixed.

G1 is character batches with `texCoord == 0`. Doodad multi-bone binds are G2. `M2Scene.cpp` stays on the CPU in G1.

Storage, when a later pass wires this, matches Campaign 0: namespace-scope `inline std::atomic<uint64_t>`, `memory_order_relaxed`, behind `WOWDECOMP_FRAME_LOG` (default off). Flag off: no clock reads and no increments. The hot path does not allocate, log, or change a return value. Sum raw clock deltas. Divide by `OsGetAsyncClocksPerSecond` (`Engine/Source/Os/Linux/TimeManager.cpp:78`) when a line is printed. On Linux `SystemMethod2` the ticks are `CLOCK_MONOTONIC` nanoseconds (`Engine/Source/Os/Linux/TimeManager.cpp:59`).

## Profiler

Leave `Profiler::Id` at the seven zones in `Engine/Source/Base/Profiler.h:8-17`. `kNames` stays the list at `Engine/Source/Base/Profiler.cpp:56-64`. `offsetof(SharedProfile, frames)` stays 264 (`Profiler.cpp:68`). `Tools/profile-mcp/server.py` keeps `ZONE_COUNT = 7` and `FRAMES = 264`.

`CM2Scene::Draw` already opens `Profiler::M2Draw` (`Engine/Source/Model2/M2Scene.cpp:2470`). `CGxDeviceVulkan::Draw` already opens `Profiler::GxDraw` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1272`). G1 zones are separate atomics on the same clock `Profiler::Scope` uses. They are not `Profiler::Scope` objects, so `M2.Draw` and `Gx.Draw` self time stay as they are. `WOW_PROFILE=0` does not gate these atomics.

## Fields

| Field | Unit | One sample |
| --- | --- | --- |
| `gpu_skin_binds` | binds | `CGxDeviceVulkan::GpuSkinBind` returns non-zero |
| `gpu_skin_cpu_fallbacks` | sections | One G1-eligible section stayed on the CPU writers |
| `gpu_skin_bone_bytes` | bytes | `boneCount * sizeof(C44Matrix)` copied into the bone ring |
| `gpu_skin_bind_pose_uploads` | uploads | One memcpy of a section's `M2Vertex` span as `GxVBF_PBNT2` |
| `gpu_skin_bind_pose_bytes` | bytes | `section->m_vertexCount * sizeof(M2Vertex)` of that memcpy |

`sizeof(C44Matrix)` is 64 (`Engine/Source/Tempest/C44Matrix.h:21-36`, sixteen floats, no virtuals). `M2Vertex` is 48 bytes (`Engine/Source/Model2/M2Data.h:119-126`) and `GxVBF_PBNT2` stride is 48 (`Engine/Source/Gx/Buffer.cpp:123`).

Zones, same clock, raw deltas:

| Zone | Bracket |
| --- | --- |
| `zone_gpu_skin_bind_ns` | `CGxDeviceVulkan::GpuSkinBind` from entry through every return, for a call with `bones != nullptr` and `boneCount != 0` |
| `zone_gpu_skin_bone_ns` | The matrix `memcpy` inside that function |
| `zone_gpu_skin_bind_pose_ns` | The `M2Vertex` `memcpy` inside `CM2Model::SetVertices` |
| `zone_gpu_skin_fallback_ns` | The CPU writer that earns `gpu_skin_cpu_fallbacks` |

`zone_gpu_skin_bone_ns` sits inside `zone_gpu_skin_bind_ns`. Adding those two zones double-counts the copy. `zone_gpu_skin_bind_pose_ns` and `zone_gpu_skin_fallback_ns` are different `SetVertices` exits.

Later print, its own line, this order, this spelling. Do not insert these names into the Campaign 0 `frame` line.

```text
gpu-m2 gpu_skin_binds=%llu gpu_skin_cpu_fallbacks=%llu gpu_skin_bone_bytes=%llu gpu_skin_bind_pose_uploads=%llu gpu_skin_bind_pose_bytes=%llu zone_gpu_skin_bind_ns=%llu zone_gpu_skin_fallback_ns=%llu zone_gpu_skin_bone_ns=%llu zone_gpu_skin_bind_pose_ns=%llu
```

## `gpu_skin_binds`

Tick inside `CGxDeviceVulkan::GpuSkinBind` when the function is about to return non-zero. One successful arm is one tick. `CGxDeviceVulkan::Draw` consumes the arm (`u.misc[2] = 1`, then the arm clears). That consume is not a second bind.

`CGxDevice::GpuSkinBind` returns 0 (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp`, next to `CGxDevice::Draw` at `CGxDevice.cpp:124`). D3D and OpenGL keep that default. A 0 return adds no bind.

A clear is `GpuSkinBind(nullptr, 0)` or `boneCount == 0`. It adds no bind, no bone bytes, and no fallback, and it stays outside `zone_gpu_skin_bind_ns`.

G1 caller, once the Draw pass adds it: `CM2SceneRender::DrawBatch`, the shaders-off non-shared branch, immediately before `Draw`.

```489:492:Engine/Source/Model2/CM2SceneRender.cpp
    } else {
        batch.m_minIndex = 0;
        batch.m_maxIndex = this->m_curSkinSection->m_vertexCount - 1;
        g_theGxDevicePtr->Draw(&batch, 1);
    }
```

Arguments: `this->m_curModel->m_boneMatrices` (`Engine/Source/Model2/M2Model.h:177`) and `this->m_data->m_bones.m_count` (`M2Data.h:325`, `M2Array::m_count` at `M2Data.h:15`). That count is the skeleton, not `M2SkinSection::m_boneCount` (`M2Data.h:307`).

The tick stays in the Vulkan method, so a later G2 caller (`DrawBatchDoodad`) is counted without a second increment site.

These draws are not binds:

| Branch | Lines | Why it stays out |
| --- | --- | --- |
| Shader constants | `CM2SceneRender.cpp:431-451` | `s_enableShaders` is off on Vulkan. Twelve floats per section bone, not the bone ring |
| Shared one-bone | `CM2SceneRender.cpp:475-488` | World matrix is `m_boneMatrices[0]`. `DrawRigidDoodads` already covers doodads |
| `Draw` arm consume | `CGxDeviceVulkan::IBuildUniforms` `CGxDeviceVulkan.cpp:1055`, `misc[0]` / `misc[1]` at `1128-1129` | `misc[2]` is the armed flag. The uniform cache key is `m_uniformGpuDrawMode` (`CGxDeviceVulkan.cpp:1474`) |

`VkContext::PrepareFrame` resets `frame.uniformOffset` at `Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:736`. The bone-ring cursor reset belongs next to that store. The reset is not a bind and adds no bytes.

A return of 0 because `boneCount > 256` or the 8 MiB ring is full (`docs/gpu-m2-plan.md`) adds no bind. The caller then keeps the CPU path, which is `gpu_skin_cpu_fallbacks`.

## `gpu_skin_bone_bytes`

On that same successful return, add `boneCount * sizeof(C44Matrix)` for the `boneCount` argument that was copied. `zone_gpu_skin_bone_ns` brackets that `memcpy` only.

Leave these sizes out of the field:

- Alignment up to `minStorageBufferOffsetAlignment`. The cursor moves by the padded step. The counter is the copied matrices.
- The descriptor range `256 * sizeof(C44Matrix)` (16384). That is the bound window, not the write.
- The 8 MiB ring allocation. Campaign 6 owns `vk_alloc` and `host_visible_bytes`. A bone-ring create does not add to either from this file.

A rejected bind copies nothing. Bytes stay put. `CGxDevice::GpuSkinBind` copies nothing.

`M2CommitVertexUpload` (`M2Model.cpp:485-486`) copies bones into the CPU upload slot. That is the skinned-vertex cache, not the storage-buffer ring.

Check, when every successful bind used the skeleton count:

`gpu_skin_bone_bytes == gpu_skin_binds * skeletonBones * 64`

only when every bind in the sample had the same `m_bones.m_count`. Otherwise the bytes are the sum of each call's `boneCount * 64`.

## `gpu_skin_bind_pose_uploads`

Tick in `CM2Model::SetVertices` (`M2Model.cpp:3152`) on the memcpy of the section's bind-pose vertices. One memcpy is one upload. Add `section->m_vertexCount * sizeof(M2Vertex)` to `gpu_skin_bind_pose_bytes`. `zone_gpu_skin_bind_pose_ns` brackets that memcpy.

Source span, the same one `TransformVertices_cpp` reads at `M2Model.cpp:3576`:

`&model->m_shared->m_data->m_vertices[section->m_vertexStart]`, `section->m_vertexCount` vertices (`M2Data.h:337`, `M2Data.h:304-305`).

The gate is `optimized == 0 && texCoord == 0 && GpuSkinSupported() != 0`. Format is `GxVBF_PBNT2`. The memcpy runs before `M2BeginVertexUpload` (`M2Model.cpp:3156`). No `TransformVertices_*` on this exit.

A later draw that finds the cached buffer and only calls `GxPrimVertexPtr(buf, GxVBF_PBNT2)` is a rebind. The plan caches that buffer without comparing bones. A rebind adds no upload and no bytes. `M2UploadBonesMatch` (`M2Model.cpp:220`) is the CPU-slot `memcmp`. The bind-pose cache does not call it.

`CM2Model::SetVertices` has one caller: `CM2SceneRender::SetBatchVertices` (`CM2SceneRender.cpp:190`). `CM2Shared::SetVertices` (`M2Shared.cpp:389`, callers at `CM2SceneRender.cpp:173`, `:184`, `:773`, `M2Model.cpp:3359`, `:3415`, `:3488`) writes the shared static mesh. Leave it out.

## `gpu_skin_cpu_fallbacks`

One tick is one G1-eligible section that published CPU-skinned vertices. Eligible means `optimized == 0` and `texCoord == 0`. `SetBatchVertices` passes `optimized` as `m_curElement->m_flags & 4` and `texCoord` as the env bit `m_shader & 0x4000` (`CM2SceneRender.cpp:179`, `:190`).

Tick once on each of these exits when the call is eligible. `zone_gpu_skin_fallback_ns` is that exit.

| Exit | Line | What ran |
| --- | --- | --- |
| Skinned-vertex cache hit | `M2Model.cpp:3156` | `M2BeginVertexUpload` returned 1. Stride 32, `GxVBF_PNT` |
| Scene-job copy | `M2Model.cpp:3169` | `M2SkinTake` (`M2Scene.cpp:1943`) copied 32-byte skinned verts |
| CPU skin | `M2Model.cpp:3191` | `s_transformVertices[section->m_boneInfluences]` |

The lock failure at `M2Model.cpp:3165` returns 0 and publishes nothing. Leave it unticked.

`DrawBatch` recovery: `GpuSkinBind` returned 0 after `SetVertices` already published `GxVBF_PBNT2`. The CPU skin that then runs lives in `DrawBatch`, in the branch at `CM2SceneRender.cpp:489`, and does not call `SetVertices`. Tick once around that `TransformVertices_*` call. A correct draw takes either the `SetVertices` CPU exits or this recovery, since a bind-pose upload returns before `M2Model.cpp:3156`.

D3D and OpenGL take the three `SetVertices` exits. `GpuSkinSupported` is 0, so the bind-pose memcpy does not run. Steady-camera cache hits at `3156` still tick: the section is on the CPU.

These stay off the fallback field. G1 leaves them on the CPU on purpose.

| Path | Site | Reason |
| --- | --- | --- |
| Optimized multi-section | `M2Model.cpp:3177-3188` | `optimized != 0` |
| Env UV | `SetVertices` with `texCoord != 0` | `m_shader & 0x4000` |
| Multi-sample | `CM2Model::SetVerticesMultiSample` `M2Model.cpp:4990`, `TransformVerticesNoUVSelect_cpp` at `5020` and `5025` | Batch flag `0x40` (`CM2SceneRender.cpp:188`) |
| Doodad CPU loop | `FillDoodadSkin` `CM2SceneRender.cpp:598`, call at `606`, from `DrawBatchDoodad` `837` / `839` | G2 |
| Rigid doodads | `DrawRigidDoodads` `CM2SceneRender.cpp:612` | One bone, already instanced |
| Scene jobs | `M2Scene.cpp` skin jobs that feed `M2SkinTake` | G1 does not switch `M2Scene.cpp`. The fallback tick is the `M2SkinTake` hit in `SetVertices`, not the job |

The skin bodies below stay uninstrumented. `SetVertices` and the `DrawBatch` recovery already cover the eligible call. Ticking the body as well counts one section twice, and the SSE unaligned forward calls the C++ body (`M2Model.cpp:3655`, `:3707`).

| Function | File | Line |
| --- | --- | --- |
| `CM2Model::TransformVertices_cpp` | `M2Model.cpp` | 3571 |
| `CM2Model::TransformVerticesOneBone_cpp` | `M2Model.cpp` | 3632 |
| `CM2Model::TransformVertices_sse` | `M2Model.cpp` | 3653 |
| `CM2Model::TransformVerticesOneBone_sse` | `M2Model.cpp` | 3705 |
| `CM2Model::TransformVertices_avx2` | `M2SkinAvx2.cpp` | 110 |
| `CM2Model::TransformVerticesOneBone_avx2` | `M2SkinAvx2.cpp` | 139 |
| `CM2Model::TransformVerticesNoUVSelect_cpp` | `M2Model.cpp` | 3514 |

`s_transformVertices` is installed at `M2Model.cpp:4343-4363`. The fallback zone stays on the call at `3191` (and the `DrawBatch` recovery call), so AVX2, SSE, and the C++ body share one sample.

## Check

Eligible character draw: `DrawBatch` else branch (`CM2SceneRender.cpp:489`), `optimized == 0`, `texCoord == 0`, batch flag `0x40` clear, `sharedVertices` clear (`CM2SceneRender.cpp:462`).

| Device | After the section is cached | What moves each draw |
| --- | --- | --- |
| Vulkan, bind accepted | `gpu_skin_bind_pose_uploads` stays 0 | `gpu_skin_binds` +1, `gpu_skin_bone_bytes` += `m_bones.m_count * 64`, fallbacks unchanged |
| Vulkan, bind returned 0 | Bind-pose bytes may already be counted | `gpu_skin_cpu_fallbacks` +1, binds and bone bytes unchanged |
| D3D / OpenGL | Uploads stay 0, binds stay 0, bone bytes stay 0 | Each eligible `SetVertices` entry that hits `3156`, `3169`, or `3191` is one fallback |

`SetBatchVertices` skips `SetVertices` when the previous element matches type, model, section, and env bit (`CM2SceneRender.cpp:186`). That draw still calls `GpuSkinBind` when the bound buffer is bind-pose. Binds can exceed uploads. A skipped `SetVertices` is not a fallback.

`gpu_skin_bind_pose_bytes == gpu_skin_bind_pose_uploads`'s vertex counts times 48. A rebind contributes to neither side.
