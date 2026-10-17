# Faith — GPU M2, G1

**Verdict.** The storage-buffer approach matches the CPU pose only where the plan already says `vec4(pos, 1) * bone` against the full `m_boneMatrices` array. Four plan sentences, implemented as written, draw a different pose. Do not ship those sentences.

| Invariant | Result |
| --- | --- |
| Row-vector multiply, no transpose, `w` rebuilt as 1 | **PASS** if the shader matches `C3Vector * C44Matrix` |
| Bone indices are global `uint8`s into `m_boneMatrices` | **PASS** on the shader-off vertex copy |
| `texCoord != 0` stays on `TransformVertices_*` | **PASS** in the plan; must stay |
| D3D / GL `GpuSkinBind` returns 0 | **PASS** in the plan; must stay |
| Uniform block stays 768 bytes; skin flag is `misc[2]` | **PASS** only with a new cache bit |
| Alpha and draw order stay on the sorted pass lists | **PASS** if the arm dies inside that draw |

## Plan mistakes that move the mesh

### Bones are already view space

`docs/gpu-m2-plan.md:24` says to skin in model space, then apply `u.world`. The bone array is not model space.

`CM2Model::AnimateMTSimple` stores `m_matrixF4 = m_worldTransform * *view` (`Engine/Source/Model2/M2Model.cpp:5245`, same store at `M2Model.cpp:5600`). The view pointer is `CM2Scene::m_view` (`Engine/Source/Model2/M2Scene.cpp:1986-1987`). A root bone's parent is that matrix (`M2Model.cpp:5771-5772`). Every other bone is `local * *parentMatrix` (`M2Model.cpp:5923`). `TransformVertices_cpp` then does `vertex->m_position * matrix` with `bones = model->m_boneMatrices` (`M2Model.cpp:3575`, `M2Model.cpp:3618`).

`CM2SceneRender::Draw` has already replaced the device view and world with identity before `DrawBatch` (`Engine/Source/Model2/CM2SceneRender.cpp:1108-1113`). `Ffp.vert` still does `vec4(inPosition, 1.0) * worldMat`, then `* u.view`, then `* u.proj` (`Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:86-88`). With both matrices identity, the skinned position is not transformed again. That is why the CPU pose lands on the camera.

A second multiply by `m_worldTransform` or `m_scene->m_view`, or a world-stack write of either, runs the mesh through the camera twice. `GpuSkinBind` must not touch `m_xforms[GxXform_World]`. The shared one-bone branch is the only writer, and it restores identity (`CM2SceneRender.cpp:475-488`).

Lighting uses that same post-world position (`Ffp.vert:91-121`). The lights are packed for the identity world `Draw` installed (`CGxDeviceVulkan.cpp:1086-1098`). Moving the skin into a real model matrix without moving the lights lights the wrong place. Leave both stacks alone.

### Do not keep the blended `w`

`C3Vector operator*(const C3Vector&, const C44Matrix&)` writes only xyz (`Engine/Source/Tempest/C44Matrix.cpp:309-314`). The CPU blend never reads `a3`, `b3`, `c3`, or `d3` (`M2Model.cpp:3587-3598`). `Ffp.vert:86` builds `w` as `1.0` from that xyz.

`vec4(pos, 1) * bone` matches xyz for an affine bone. Its `w` is the sum of the weights when each bone has `d3 == 1`. `C44Matrix::Translate` does not touch `d3` (`C44Matrix.cpp:143-147`), and the root starts at `d3 = 1` (`M2Model.cpp:5871`). Chaining that `vec4` into `* u.world` scales the world translation by the weight sum. Weights that do not add up to 255 leave `w != 1` and the clip position is wrong.

The blended position that enters the existing path is `vec4(skinned.xyz, 1.0)`. The blended normal is `vec4(normal.xyz, 0.0)` so the translation row is dropped, which is what `M2Model.cpp:3623-3625` does. Then the existing normalize at `Ffp.vert:92` runs. Do not normalize inside the blend.

### "Skip a weight of 0" is the wrong loop

`docs/gpu-m2-plan.md:35` says four influences, skip a weight of 0. `SetVertices` does not call one blend. It calls `s_transformVertices[section->m_boneInfluences]` (`M2Model.cpp:3191`).

| `m_boneInfluences` | Kernel | Weight rule |
| --- | --- | --- |
| 1 | `TransformVerticesOneBone_cpp` (`M2Model.cpp:4344`, body `M2Model.cpp:3632-3642`) | Slot 0 only. The weight bytes are not read. The bone is applied at full strength. |
| 0, 2, 3, 4 | `TransformVertices_cpp` (`M2Model.cpp:4343-4347`, body `M2Model.cpp:3585-3601`) | Slot 0 is always consumed, even when its byte is 0. Slots 1..3 stop at the first 0. They do not resume. |

A four-weight blend that skips zeros and continues diverges in two poses:

- Influences == 1 and slot 0 is not 255. CPU applies the whole bone. The GPU scales it, or drops it when the byte is 0 and the section collapses.
- Influences != 1 and a later slot is non-zero after a zero, for example `(255, 0, 128, 0)`. CPU keeps only bone 0. A skip-and-continue shader adds bone 2.

The scale on the weighted path is `m_weights.b[k] * 0.0039215689f` (`M2Model.cpp:3586`). The shader input is `R8G8B8A8_UNORM`, already divided by 255. A raw 0..255 weight blows the bone up by 255.

The blend is a sum of matrices, then one multiply (`M2Model.cpp:3600-3618`). It is not `pos * bone0 * bone1`.

### Weight bytes must not use the format table

`docs/gpu-m2-plan.md:42` keeps blend indices off `s_gxAttribToVkFormat` because type 1 is unorm. Blend weights are type 2 (`Engine/Source/Gx/Buffer.cpp:92`). The table maps type 2 to `VK_FORMAT_B8G8R8A8_UNORM` as well (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:41`).

`M2Vertex` stores influence 0 in byte 0 (`Engine/Source/Model2/M2Data.h:119-125`, `ubyte4::b` at `M2Data.h:35`). `GxVBF_PBNT2` puts those bytes at offset 12 and the indices at offset 16 (`Engine/Source/Gx/Gx.cpp:34`). `B8G8R8A8` assigns byte 0 to `.b` and byte 2 to `.r`. A shader that treats `.r` as influence 0 swaps influences 0 and 2.

Both attributes bypass the table. Weights are `VK_FORMAT_R8G8B8A8_UNORM`. Indices are `VK_FORMAT_R8G8B8A8_UINT`. Locations stay 5 and 6 (`docs/gpu-m2-plan.md:32-33`). The current table has both at -1 (`CGxDeviceVulkan.cpp:45`), so unskinned draws do not fetch them. A stale skin flag on a `GxVBF_PNT` draw fetches unbound indices and skins anyway.

## Multiply order

Copy `C44Matrix` as 16 floats, 64 bytes, no transpose. Memory order is `a0, a1, a2, a3, b0 ... d3` (`Engine/Source/Tempest/C44Matrix.h:21-36`). The bone block is `std430, row_major`, and the multiply is `vec4(pos, 1) * bone`, the same order as `Ffp.vert:86` and as `position * matrix`.

The shader-on upload in `DrawBatch` is a different layout and a different index space. It writes `a0, b0, c0, d0` as the first four floats (`CM2SceneRender.cpp:436-447`) and only `m_boneCount` matrices, looked up through `m_boneCombos`. That transpose in a row-major buffer mirrors the skeleton. G1 does not use it. `s_enableShaders` stays off, and that branch is left as it is (`CM2SceneRender.cpp:431-452`, `471-474`).

## Bone index space

On the Vulkan path cache flag `0x8` is clear, so `CM2Shared::InitializeSkinProfile` rewrites each vertex (`Engine/Source/Model2/M2Shared.cpp:1299-1312`):

```text
m_indices.b[k] = (uint8)m_boneCombos[section->m_boneComboIndex + m_skinData->m_bones[j].b[k]]
```

It then zeros every section `m_boneComboIndex` and sets `m_boneCombos[i] = i` (`M2Shared.cpp:1316-1322`). After that, `TransformVertices_cpp` indexes `m_boneMatrices` with the `uint8` directly (`M2Model.cpp:3585-3601`). The file's skin-bone bytes are section-local combo slots. Those bytes are the wrong palette index once the vertex has been rewritten.

`GpuSkinBind` therefore receives `m_curModel->m_boneMatrices` and `m_data->m_bones.m_count` (`docs/gpu-m2-plan.md:62`). Not `m_curSkinSection->m_boneCount`. Not the combo range. A count above 256 returns 0 and the CPU kernel still runs (`docs/gpu-m2-plan.md:22`). The `uint8` cast is already what the CPU indexes, so the fallback matches it.

`docs/gpu-m2-plan.md:62` says indices stay section-local and `m_minIndex == 0`. That sentence is the draw range, and only the draw range. The non-shared shader-off batch already uses `m_minIndex = 0` and `m_maxIndex = m_vertexCount - 1` (`CM2SceneRender.cpp:489-492`). `CM2Shared::SetIndices` biases those indices by `-section->m_vertexStart` when the mesh is not shared (`M2Shared.cpp:356-366`). The bound buffer's element 0 is `m_vertices[section->m_vertexStart]`, the same pointer `TransformVertices_cpp` starts from (`M2Model.cpp:3576`). An absolute index with `m_minIndex == 0` draws the wrong vertices.

Reading "section-local" as the bone index and copying the shader combo loop uploads `m_boneMatrices[0 .. boneCount)` after the identity rewrite. Global `uint8`s then address the wrong bones.

The one-bone shared branch is the other index space. `m_bones.m_count == 1` and cache flag `0x40` set the world matrix to `m_boneMatrices[0]` and draw with `m_minIndex = m_vertexStart` (`CM2SceneRender.cpp:462`, `475-483`). Those vertices are bind-pose `GxVBF_PNT2` with no blend indices (`M2Shared.cpp:412-427`). Arming skin there applies the bone again. G1 does not call `GpuSkinBind` on that branch.

Optimized geometry rebases indices across concatenated sections (`M2Model.cpp:3115-3136`). Multi-sample is `SetVerticesMultiSample` (`CM2SceneRender.cpp:187-188`). Both stay on the CPU, as the plan says (`docs/gpu-m2-plan.md:60`). A single section buffer with `m_minIndex == 0` does not match those index ranges.

## `texCoord != 0` stays on the CPU

`TransformVertices_cpp` writes one UV into the 32-byte stream: `out[6]` and `out[7]` are `m_texcoord[texCoord]` (`M2Model.cpp:3626-3627`). `SetBatchVertices` passes `texCoord = 1` when batch shader flag `0x4000` is set (`CM2SceneRender.cpp:179-190`). `GxVBF_PBNT2` always puts `m_texcoord[0]` at location 3 and `m_texcoord[1]` at location 4 (`Gx.cpp:34`, `Ffp.vert:9-10`). `u.stageCoord` is `GxRs_TextureCoord`, a different switch (`CGxDeviceVulkan.cpp:1070`, `Ffp.vert:38`). Uploading the raw vertex for an env batch shows UV0 where the CPU stream showed UV1.

G1's bind-pose upload is only `!optimized && texCoord == 0` (`docs/gpu-m2-plan.md:60`). Every other `SetVertices` call still goes through `s_transformVertices`.

## D3D and GL return 0

`GpuSkinBind` defaults to 0. `GpuSkinSupported` is 1 only on the Vulkan device (`docs/gpu-m2-plan.md:5`, `docs/gpu-m2-plan.md:21-22`). D3D9 and OpenGL do not override either function. A non-zero D3D bind with no skin shader draws the bind pose.

`CGxDevice::Draw` is empty (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:124-125`). The CPU skin is `SetVertices` → `TransformVertices_*` (`M2Model.cpp:3169-3191`). That call stays for every device whose bind returns 0, including a full bone ring and a `boneCount` above 256. G1's done line is the same gate (`docs/gpu-m2-plan.md:74`).

## 768-byte uniforms

`VkDrawUniforms` is 768 bytes (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h:26-50`). The GLSL block is `FfpUniforms.glsl` and is not edited (`docs/gpu-m2-plan.md:25`). Bones are a separate storage buffer. The skin flag uses the existing `misc[2]` (`docs/gpu-m2-plan.md:23`, `docs/gpu-m2-plan.md:37`).

`misc` is already assigned (`CGxDeviceVulkan.cpp:1127-1129`):

| Component | C index | Today |
| --- | --- | --- |
| `u.misc.x` | `misc[0]` | Point size |
| `u.misc.y` | `misc[1]` | `m_gpuDrawMode` |
| `u.misc.z` | `misc[2]` | Free. This is the skin arm. |
| `u.misc.w` | `misc[3]` | Free. Leave it 0. |

`u.misc.y != 0` replaces `u.world` with `GpuDrawItem.world` (`Ffp.vert:58-70`). Storing the skin arm in `m_gpuDrawMode` takes that path. `gl_InstanceIndex` then reads an item buffer that this draw did not fill, and the bone multiply is applied on top of the wrong world. `m_uniformGpuDrawMode` is the cache of that mode (`CGxDeviceVulkan.cpp:1474`, stored at `CGxDeviceVulkan.cpp:1520`). It is the pattern for a new field. It is not the field that holds the skin flag.

`IBuildUniforms` writes `misc[2]` from the arm. The rebuild predicate must miss when the arm changes, the same way it misses when `m_gpuDrawMode` changes. A cache hit keeps the previous 768 bytes. The skinned draw then keeps `misc[2] == 0` and shows the bind pose, or the next draw keeps `misc[2] == 1` and skins a mesh that was already skinned on the CPU.

`Draw` consumes the arm and clears it before it returns (`docs/gpu-m2-plan.md:23`). `DrawBatch` binds, draws, and clears on the non-shared shader-off branch only (`CM2SceneRender.cpp:489-492`). The bind is immediate: `SetShaders` and the material setup do not draw, and nothing may draw between the arm and this `Draw`. Bind again when `SetBatchVertices` skips the upload (`CM2SceneRender.cpp:186-191`). The bind-pose buffer is still the right vertices, and the bones are this frame's `m_boneMatrices`. Skipping the bind on that hit leaves the arm clear and draws the bind pose.

## Alpha and draw order

Skinning does not sort and does not change `m_alpha`.

`CM2Scene::Draw` walks `m_passElements[pass]` in the order `M2HeapSort` already wrote (`M2Scene.cpp:2447-2449`, `M2Scene.cpp:2486`). Pass 0 is opaque. Passes 1 and 2 are the transparent lists (`SortTransparent`). A transparent batch is type 0, so it already goes through `DrawBatch` (`M2Scene.cpp:2112-2113`, `M2Scene.cpp:2195-2214`). Sort depth was computed from these same bone matrices (`M2Scene.cpp:2143`, `M2Scene.cpp:2147`). The GPU palette has to be that array or the mesh and the sort key disagree.

`SetupMaterial` writes `element->m_alpha` into diffuse `.w` before the draw (`CM2SceneRender.cpp:402-407`). Blend mode, alpha ref, and depth write are the render state from `SetupMaterial` (`CM2SceneRender.cpp:285-313`). `GpuSkinBind` does not change them and does not move the draw to the end of the frame. A deferred skin pass draws alpha after ribbons and particles that the sort placed later.

The arm is one draw. The next element in `CM2SceneRender::Draw` may be a ribbon, a particle, a doodad, or another character (`CM2SceneRender.cpp:1159-1177`). A skin flag that survives this `Draw` skins that element with this model's palette. G1 does not replace `FillDoodadSkin`. That loop is G2 (`docs/gpu-m2-plan.md:64-75`). Until then it still calls `s_transformVertices` with texCoord 0, and it must see a clear arm.
