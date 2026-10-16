# Campaign 1 scene — rigid city camera

Golden check for the one-bone path. One city camera, one character position, one scene time, many placements of one rigid doodad. `CShaderEffect::s_enableShaders` stays 0, so `DrawBatchDoodad` enters `DrawRigidDoodads` (`Engine/Source/Model2/CM2SceneRender.cpp:717`).

The frame is that doodad only. Every placement shares one `CM2Shared`, one lighting record, and one batch index, so `SortDoodad` keeps them in one run (`Engine/Source/Model2/M2Scene.cpp:2147-2179`). `m_count` on the first element is the copy count (`M2Scene.cpp:2178`). A run of one is type 0 (`M2Scene.cpp:2172-2176`) and is outside this picture. The copy count stays inside the `m_instances` already reserved, so `AllocInstances` does not call `DestroyBuffers` (`Engine/Source/Model2/M2Shared.cpp:771-779`).

Each section in the run passes the one-bone gates and `DrawRigidDoodads` returns 1 (`CM2SceneRender.cpp:609-633`, `CM2SceneRender.cpp:692`): `m_boneCount == 1`, cache flag `8` clear, the single-bone flag `0x40` clear, every vertex `m_indices.b[0]` equal, bone in range, section inside the skin.

## On screen

Same camera, same positions, same time. Color and depth of every copy match. The copies differ by the bone matrix. The mesh, the triangles, and the batch state are shared.

| On screen | Same value | Where it is produced |
| --- | --- | --- |
| Where each copy sits | `m_boneMatrices[bone]`, `bone` = `verts[0].m_indices.b[0]` | `CM2SceneRender.cpp:618`, `CM2SceneRender.cpp:641` |
| Projected position | `vec4(inPosition, 1) * world * view * proj`, `world` = that matrix | `Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:80-82` |
| Which matrix the vertex shader reads | `GpuDrawItem.world` because `m_gpuDrawMode` is 1 | `Ffp.vert:66-68`, `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1496-1499` |
| Positions, normals, UV0 | Untransformed section vertices: `m_position`, `m_normal`, `m_texcoord[0]`, 8 floats, stride 32 | `CM2SceneRender.cpp:651-660`, `Engine/Source/Gx/Buffer.cpp:24-27` |
| Triangles | One index range, `bias + skin index`, `bias = -m_vertexStart`, then instanced | `CM2SceneRender.cpp:673-678`, `CM2SceneRender.cpp:685-691` |
| How many copies, in what order | The `SortDoodad` run, `instanceCount = total` | `M2Scene.cpp:2147-2179`, `CGxDeviceVulkan.cpp:1469-1470` |
| Shade, fog, alpha | The 768-byte uniform. Mode 1 replaces `world` and leaves lights, fog, and tex matrices on that uniform | `Ffp.vert:66-77`, `CGxDeviceVulkan.cpp:971-1045` |
| Textures and wrap | `SetupTextures` for this batch | `CM2SceneRender.cpp:99-125` |
| Sphere / env stage | TexGen 6, reflection of the view position and normal, then the uniform tex matrix | `Engine/Source/ShaderEffect/ShaderEffect.cpp:389`, `Ffp.vert:32-53`, `CGxDeviceVulkan.cpp:913-917` |
| Blend, write mask, alpha ref, cull, depth | `SetupMaterial` / `SetupLighting` on the batch before the rigid return | `CM2SceneRender.cpp:706-708`, `CM2SceneRender.cpp:284-314` |

UV0 on screen is the stream write of `m_texcoord[0]` (`CM2SceneRender.cpp:658-659`). The shared static fill stores `M2VertexPNT2` (40 bytes) and, when the env bit is set, swaps which source texcoord lands in `[0]` (`M2Shared.cpp:398-425`). This camera still shows the 32-byte `GxVBF_PNT` UV0.

`DrawRigidDoodads` does not read the multi-sample batch flag. A one-bone section still shows this stream. The shader-on shared bind (`CM2SceneRender.cpp:730-731`) is a different capture.

Vulkan `AllocDrawItems` succeeds, so placement is one indexed instanced draw (`CGxDeviceVulkan.cpp:1486-1499`). The world stack at the start of the M2 pass is identity (`CM2SceneRender.cpp:1068-1071`).

## Counters

Names are the rigid line in `docs/perf-squads/m2-rigid/counter.md`. Sample one frozen second. `N` is `m2_rigid_instances`. `B` is `m2_rigid_static_binds`.

Before this slice, a successful call moves `32 * V + 2 * I + 64 * N` (`docs/perf-squads/m2-rigid/accountant.md`). `V` and `I` are the section counts. The `64 * N` term is one `C44Matrix` per copy (`Engine/Source/Tempest/C44Matrix.h:21-36`) copied into `GpuDrawItem.world` (`CGxDeviceVulkan.cpp:1491-1493`, `Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:90`). That vector is outside `m2_rigid_stream_bytes` (`CM2SceneRender.cpp:636-642`).

Matrices-only on this camera:

| Field | Pass | Site that feeds it |
| --- | --- | --- |
| `m2_rigid_stream_bytes` | `0` | Vertex lock `CM2SceneRender.cpp:645` (`V * 32`) and index lock `CM2SceneRender.cpp:668` (`I * 2`) |
| `m2_rigid_vertices` | `0` | `CM2SceneRender.cpp:645` |
| `m2_rigid_indices` | `0` | `CM2SceneRender.cpp:668` |
| `m2_rigid_instances` | Same `N` as the before sample, `N > 1` | `DrawInstanced` at `CM2SceneRender.cpp:691` |
| `m2_rigid_static_binds` | Same `B` as the before sample, `B` equal to the rigid batches | Bind at `CM2SceneRender.cpp:683` |

`m2_rigid_stream_bytes == 0` with `m2_rigid_instances == N` and `m2_rigid_static_binds == B` is the proof. The mesh locks added nothing. The batch still bound once and still submitted every copy. The bytes that still scale with the copies are `64 * m2_rigid_instances` into `GpuDrawItem.world`.

`m2_rigid_instances == 0` means the section left `DrawRigidDoodads` before `DrawInstanced`. Those copies are on the skin stream (`CM2SceneRender.cpp:776`), which this check does not treat as a rigid pass.

`m2_rigid_clocks` times the function. It does not say what was uploaded.
