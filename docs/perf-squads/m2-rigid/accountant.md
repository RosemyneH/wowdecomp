# Campaign 1 accountant — rigid doodad bytes

Static count from source. No Campaign 0 frame log is in tree, so this is bytes per `DrawRigidDoodads` call, not a scene total.

## Call

A call that returns 1 (`Engine/Source/Model2/CM2SceneRender.cpp:692`) moves:

| Term | Formula | Where |
| --- | --- | --- |
| Vertex stream | `32 * V` | `BufStream(GxPoolTarget_Vertex, 32, V)` at `CM2SceneRender.cpp:644`. The lock writes 8 floats (position, normal, `texcoord[0]`) and steps 8 (`CM2SceneRender.cpp:651-660`). Bound as `GxVBF_PNT` (`CM2SceneRender.cpp:665`). That format’s stride is 32 (`Engine/Source/Gx/Buffer.cpp:25-27`). |
| Index stream | `2 * I` | `BufStream(GxPoolTarget_Index, 2, I)` at `CM2SceneRender.cpp:667`. Each stored index is a `uint16_t` (`CM2SceneRender.cpp:677-679`). |
| `C44Matrix` vector | `64 * N` | `worlds` is one `m_boneMatrices[bone]` per instance (`CM2SceneRender.cpp:636-642`). `C44Matrix` is 16 floats, fields `0x0000`–`0x003C`, 64 bytes, no virtuals (`Engine/Source/Tempest/C44Matrix.h:21-36`). |

`V` = `section->m_vertexCount` (`Engine/Source/Model2/M2Data.h:304`).
`I` = `section->m_indexCount` (`M2Data.h:306`).
`N` = `total`, the instance count passed into the call.

**Bytes moved = `32 * V + 2 * I + 64 * N`.**

`DrawInstanced` (`CM2SceneRender.cpp:691`) consumes that vector. On Vulkan each matrix is copied into `GpuDrawItem.world` (`float world[16]`, `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1491-1493`, `Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:90`). That copy is the same `64 * N` matrix upload.

Calls that return 0 before the streams (`CM2SceneRender.cpp:609-634`) move none of these bytes. That includes the shared-vertex flags (`m_flags & 8`, or one bone and `m_flags & 0x40`) at `CM2SceneRender.cpp:613`.

## Shared buffers already hold the mesh

`DrawBatchDoodad` returns on success before `SetIndices` / `SetVertices` (`CM2SceneRender.cpp:717-718`). The stream above rewrites a mesh those functions already build once.

`CM2Shared::SetVertices` (`Engine/Source/Model2/M2Shared.cpp:388`):

- Static pool, `sizeof(M2Vertex) * m_instances * skinVertexCount` (`M2Shared.cpp:390-391`). `M2Vertex` ends at `0x30` (48 bytes) (`M2Data.h:119-126`).
- Shaders off: one fill when `!m_hasData || !m_valid`, written as `M2VertexPNT2` (ends at `0x28`, 40 bytes) (`M2Data.h:112-117`, `M2Shared.cpp:398-423`), then bound as `GxVBF_PNT2` (`M2Shared.cpp:425`).

`CM2Shared::SetIndices` (`M2Shared.cpp:338`):

- Dynamic pool, `sizeof(uint16_t) * m_instances * skinIndexCount` (`M2Shared.cpp:340-341`).
- One `uint16_t` fill when the buffer is empty (`M2Shared.cpp:348-379`). Each section is repeated `m_instances` times.

The rigid stream is a different record: 32-byte `GxVBF_PNT` (one UV) and one index range of `2 * I`, then `N` matrices. The shared vertex fill is 40-byte `PNT2`. The shared index fill is the instance-expanded copy.

## Budget

**Rigid batches upload matrices only.**

| | Bytes per successful call |
| --- | --- |
| Now | `32 * V + 2 * I + 64 * N` |
| Budget | `64 * N` |

`32 * V + 2 * I` is the per-batch mesh rewrite. It is zero when the section binds the shared static vertex and index buffers and the only upload is the `C44Matrix` vector.

## Allocs beside the formula

`DrawBatchDoodad` calls `AllocInstances(total)` before the rigid draw (`CM2SceneRender.cpp:711`). A grow rounds `m_instances` up to a multiple of 16 and calls `DestroyBuffers` (`M2Shared.cpp:771-779`, destroy at `M2Shared.cpp:602`). A steady camera does not take that branch: capacity is already at the peak.

`worlds` is function-static (`CM2SceneRender.cpp:636`). `clear` + `reserve(N)` allocates only when `N` exceeds the retained capacity. Each call still copy-constructs `N` matrices (the `64 * N` term).

The two `BufStream` calls resize the device stream buffer when the pool is shorter than the lock (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1062-1074`). `BufLock` then writes `32 * V` and `2 * I` into the stream ring (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1596-1640`). Those writes are the mesh term.
