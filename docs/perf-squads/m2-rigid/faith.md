# Campaign 1 faith — rigid M2

Baseline is the current tree. No static-bind slice has landed. `DrawRigidDoodads` still stream-locks the section, then `DrawInstanced` supplies `m_boneMatrices[bone]`. Vulkan is the fixed-function device: `CGxDeviceVulkan` reports every shader target as 0 (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:490`), so cache flag 8 stays clear and `InitializeSkinProfile` rewrites skin vertices into `m_data->m_vertices` with identity lookup (`Engine/Source/Model2/M2Shared.cpp:1187-1214`).

| Invariant | Verdict | Holds at |
| --- | --- | --- |
| One-bone test | **Pass** | `CM2SceneRender.cpp:609-628` |
| Index bias | **Pass** on the stream the function builds | `CM2SceneRender.cpp:674-690` |
| `vertexStart` | **Pass** | `CM2SceneRender.cpp:617`, `675`, `690` |
| Env UV | **Fail** | `CM2SceneRender.cpp:658-659` |
| Shader-on branch | **Pass** | `CM2SceneRender.cpp:717-718`, `730-768` |
| `DrawInstanced` results | **Pass** on Vulkan FFP | `CM2SceneRender.cpp:640-691`, `CGxDeviceVulkan.cpp:1478-1499`, `Ffp.vert:66-86` |

Faith does not reject “stop rewriting the rigid mesh.” Faith rejects binding `CM2Shared`’s static vertex buffer and static index buffer as a pair while the draw’s vertex offset stays 0. Those two buffers are not in the same coordinate space. Detail is under index bias.

## One-bone test — pass

`DrawRigidDoodads` returns 0 unless all of these hold (`Engine/Source/Model2/CM2SceneRender.cpp:609-628`):

- `section->m_boneCount == 1`, and the section has vertices and indices.
- Cache flag 8 is clear, and the model is not the single-bone flag `0x40` case.
- Every vertex in `[m_vertexStart, m_vertexStart + m_vertexCount)` has the same `m_indices.b[0]`.
- That bone is inside `m_bones`.

The flag-8 / flag-`0x40` predicate is the same one `CM2Shared::SetIndices` uses to decide whether indices stay absolute (`Engine/Source/Model2/M2Shared.cpp:355-361`). Models on the absolute-index path never enter this function. The vertex scan matches `TransformVerticesOneBone_*`, which also applies `m_indices.b[0]` and ignores weights (`Engine/Source/Model2/M2Model.cpp:3162-3177`). `s_transformVertices[1]` is that one-bone function (`Engine/Source/Model2/M2Model.cpp:3873`).

`DrawBatchDoodad` calls this only from the shader-off branch (`Engine/Source/Model2/CM2SceneRender.cpp:717`). A section that fails the test falls through to `FillDoodadSkin`.

## Index bias — pass on the stream

Skin indices address the skin vertex array. The stream packs the section at local vertex 0 (`Engine/Source/Model2/CM2SceneRender.cpp:644-660`), so each index is rebased:

```text
bias = uint16(-m_vertexStart)
indexOut[j] = uint16(bias + skinIndices[fileIndexStart + j])
```

`fileIndexStart` is `m_skinData->m_skinSections[sectionIndex].m_indexStart` (`CM2SceneRender.cpp:674`), the file value. `SetIndices` later overwrites `m_skinSections[i].m_indexStart` with the packed GPU cursor (`M2Shared.cpp:372`) and does not write the skin-data copy. The batch then draws that packed buffer at `m_start = 0`, `m_minIndex = 0`, `m_maxIndex = m_vertexCount - 1` (`CM2SceneRender.cpp:685-690`).

For `vertexStart == 100` and a skin index of 105, bias is `uint16(-100)` and the stored index is 5. That is local vertex 5 of this section. The same formula is instance 0 of `SetIndices` when `sharedVertices` is false (`M2Shared.cpp:361-365`).

### What a static-buffer bind breaks when the bias is wrong

`SetVertices` on the shader-off path writes positions, normals, and UVs at absolute index `j` (`M2Shared.cpp:410-416`). `SetIndices` stores `skinIndex - vertexStart` for instance 0, then adds `m_vertexCount` per further instance (`M2Shared.cpp:363-369`). Vulkan draws with vertex offset 0 and ignores `m_minIndex` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1470`). OpenGL uses the index as the vertex id (`Engine/Source/Gx/CGxDeviceOpenGL/CGxDeviceOpenGl.cpp:3981`). D3D adds `m_index / m_itemSize` from the buffer, not `m_minIndex` (`Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:3291-3299`).

Bind those two static buffers, or apply the stream bias to a buffer that already has it, and the triangles fetch a different slot:

| Bind | Index that gets fetched | Mesh that shows up |
| --- | --- | --- |
| Static vertices at absolute `j`, indices already biased, vertex offset 0 | `skinIndex - vertexStart` | The vertex `vertexStart` slots earlier. Section 0 (`vertexStart == 0`, bias 0) is unchanged. Every later section shows an earlier section’s positions, normals, and UVs. |
| Bias applied again to indices `SetIndices` already biased | `uint16(skinIndex - 2 * vertexStart)` | Wrapped `uint16` ids near the top of the 16-bit range. The draw reads off the end of this section. |
| File indices left absolute, vertex buffer packed to local 0 the way the stream is | `skinIndex` (still includes `vertexStart`) | Past this section, into the next section. |

`m_minIndex` / `m_maxIndex` do not move the fetch. Widening them only changes D3D’s declared range. `DrawInstanced` then instances that wrong mesh once per doodad.

A static bind also has to use the instance-0 slice. Later slices in `m_indexBuf` are already shifted by `instance * m_vertexCount`. `DrawInstanced` draws one index range with many worlds (`CM2SceneRender.cpp:685-691`). Feeding it a later slice shifts every doodad by that many vertices.

## `vertexStart` — pass

The stream uses one field for both ends of the rebase:

- Vertex slice: `&data->m_vertices[section->m_vertexStart]` (`CM2SceneRender.cpp:617`). After the flag-8 rewrite, `m_skinData->m_vertices[j] == j` (`M2Shared.cpp:1213-1214`), so this is the skin vertex.
- Bias: `uint16(-m_vertexStart)` (`CM2SceneRender.cpp:675`).
- Draw range: local `0 .. m_vertexCount - 1` because the slice was packed (`CM2SceneRender.cpp:689-690`).

`SetIndices` does not modify `m_vertexStart`. The element’s section pointer is `&m_skinSections[batch->m_skinSectionIndex]` (`Engine/Source/Model2/M2Scene.cpp:1808`), which is the array `DrawRigidDoodads` subtracts at `CM2SceneRender.cpp:630`.

The static vertex buffer keeps the other convention: slot `j` holds skin vertex `j` (`M2Shared.cpp:410-412`). A bind that keeps the stream’s `m_minIndex = 0` is valid with the biased indices above. It is not valid with absolute indices into that static buffer.

## Env UV — fail

`0x4000` is the env-UV bit. `SubstituteSimpleShaders` sets it when the texture-coord combo is 1 (`M2Shared.cpp:656-658` and `678-680`). `SetBatchVertices` names that bit `env` and passes it as the texcoord index (`CM2SceneRender.cpp:179-184`). Shader-off `SetVertices` then stores `m_texcoord[texCoordIndex]` in GPU slot 0 and the other set in slot 1 (`M2Shared.cpp:415-416`). Stage 0 samples texcoord 0: the default coord index for stage `i` is `i` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1424`).

`DrawRigidDoodads` always writes `m_texcoord[0]` and binds `GxVBF_PNT` (`CM2SceneRender.cpp:658-665`). It never reads `m_shader & 0x4000`. A one-bone env batch therefore samples model UV0. The doodad CPU path has the same hole: `FillDoodadSkin` calls `s_transformVertices[...](..., 0)` (`CM2SceneRender.cpp:603`). Type-0 batches go through `SetBatchVertices` and can sample UV1. This rigid path does not.

Sphere-map texgen is a different bit and still runs. `DrawBatchDoodad` calls `SetupTextures` before the rigid return (`CM2SceneRender.cpp:708-718`). Bit 3 of the shader nibble calls `SetTexMtx_SphereMap` (`CM2SceneRender.cpp:118-125`), which sets texgen 6 (`Engine/Source/ShaderEffect/ShaderEffect.cpp:389`). `Ffp.vert` consumes `u.stageTexGen` in `StageTexCoord` (`Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:32-53`). A static bind has to leave that state alone.

`SetVertices` will not repair a later env change. The fill is skipped once `m_hasData` is set (`M2Shared.cpp:398-423`), and the format is `GxVBF_PNT2` (40 bytes, two UVs) rather than the stream’s `GxVBF_PNT`. Binding that buffer onto a rigid doodad changes which UV reaches stage 0, and it changes the vertex stride the pipeline is reading.

## Shader-on branch — pass

The rigid function is behind `!CShaderEffect::s_enableShaders` (`CM2SceneRender.cpp:717-718`). With shaders on, `DrawBatchDoodad` does not stream this mesh. It binds the shared buffers and uploads bone constants:

- `SetIndices`, then `SetVertices(0)` (`CM2SceneRender.cpp:721-731`). On the shader-on side of `SetVertices` the argument is not a texcoord index; the upload copies `M2Vertex` and writes palette indices (`M2Shared.cpp:430-457`).
- Bone `i` of each instance comes from `m_boneCombos[i + m_boneComboIndex]` into vertex constants at register 31 (`CM2SceneRender.cpp:738-763`).
- One `Draw` per instance group, `m_start = m_indexStart`, `m_count = instances * m_indexCount` (`CM2SceneRender.cpp:725-768`). No `DrawInstanced`.

Cache flag 8 is what makes `SetIndices` keep absolute indices (`M2Shared.cpp:355-361`). That matches shader-on vertices stored at absolute `j`. This branch has to stay on that pair.

## `DrawInstanced` results — pass

For a section the one-bone test accepted, Vulkan instances the same transform the one-bone CPU skin would have baked in.

- World `n` is `elements[indices[n]].m_model->m_boneMatrices[bone]` (`CM2SceneRender.cpp:640-641`), the bone the scan proved every vertex uses.
- `CGxDeviceVulkan::DrawInstanced` copies those matrices into `GpuDrawItem.world`, sets `m_gpuDrawMode = 1`, and draws once with `firstInstance` at the item cursor (`CGxDeviceVulkan.cpp:1478-1499`). The item SSBO is bound at offset 0 (`CGxDeviceVulkan.cpp:1410`). `gl_InstanceIndex` includes that base, and `Ffp.vert:67-68` loads `gpu.items[gl_InstanceIndex]`.
- The multiply matches `TransformVerticesOneBone_cpp`: row-vector times `C44Matrix`, position uses the translation row, normal does not (`Ffp.vert:80-85`, `M2Model.cpp:3170-3175`). `C44Matrix` is sixteen floats, `a0..d3` (`Engine/Source/Tempest/C44Matrix.h:21-35`), and the shader declares the item buffer `row_major`.
- `Ffp.vert:86` normalizes the transformed normal for every mode. CPU-skinned doodads hit the same shader with the world stack left at identity (`CM2SceneRender.cpp:1066-1071`), so both paths normalize on this device.
- `Draw` clears `m_gpuDrawMode`, `m_drawInstances`, and `m_firstInstance` on the way out (`CGxDeviceVulkan.cpp:1184-1192`). The next batch is a normal draw.
- If `AllocDrawItems` fails, the base implementation draws once per matrix and restores the saved world (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:127-144`). Same bone, same mesh.

The pixels match only while the index bias matches the bound vertices. A wrong bias instances the wrong section, once per doodad.

D3D does not get the shader’s unconditional normalize. The CPU doodad loop sets `GxRs_NormalizeNormals` from the bone’s scale (`CM2SceneRender.cpp:800-815`). `DrawRigidDoodads` never sets it, and the default is off (`CGxDevice.cpp:497`). A non-uniform bone on D3D keeps scaled normals. Campaign 1’s device is the Vulkan FFP path, where `Ffp.vert:86` covers both draws.
