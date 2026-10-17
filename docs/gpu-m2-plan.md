# GPU M2 skinning

G1 and G2 are in the client. Characters with one UV set keep a bind-pose mesh and skin in `Ffp.vert`. Multi-bone doodads do the same, one bone bind per instance, and fall back to `FillDoodadSkin` if the ring is full. Env, optimized, and multi-sample batches stay on the CPU. G3 still has to keep the doodad bind-pose buffer across frames. G4 moves those static meshes to device-local memory.

Vulkan stays on the fixed-function path (`m_shaderTargets` are all 0, so `CShaderEffect::s_enableShaders` stays off). Retail D3D9 / DXVK skins in the vertex shader. This campaign does the same job without porting the `.bls` shader library: the mesh stays in bind pose, bone matrices go up in a storage buffer, and `Ffp.vert` blends position and normal before the existing world / view / proj and lighting.

Do not advertise shader targets. Terrain, WMO, liquids, and UI stay on the current path. D3D9 and OpenGL keep CPU skinning (`GpuSkinBind` returns 0).

## Why the CPU path exists

`CM2Model::SetVertices` (`Engine/Source/Model2/M2Model.cpp`) locks a 32-byte `GxVBF_PNT` stream and calls `TransformVertices_*`. Bone indices in `M2Vertex` (`ubyte4`, `Engine/Source/Model2/M2Data.h`) index `model->m_boneMatrices` directly. `Ffp.vert` then multiplies that already-skinned position by `u.world`.

`M2Vertex` is 48 bytes and matches `GxVBF_PBNT2` (`Engine/Source/Gx/Buffer.cpp`). Vulkan currently drops blend weight and blend indices (`s_gxAttribLocation` is -1 for both).

## Contract

```cpp
// CGxDevice. Default returns 0. Clear with bones == nullptr or boneCount == 0.
virtual int32_t GpuSkinBind(const C44Matrix* bones, uint32_t boneCount);
virtual int32_t GpuSkinSupported() const;
```

- `GpuSkinSupported` is 1 only on the Vulkan device.
- `GpuSkinBind` copies `boneCount` matrices into the current frame's bone ring and arms the next `Draw`. Returns 0 if `boneCount` is 0, above 256, or the ring is full (caller keeps the CPU path).
- `Draw` consumes the arm: `u.misc[2] = 1`, dynamic storage offset selects the palette, then the arm clears so the next draw does not skin.
- Skin in model space, then the existing `u.world`. Same order as CPU (`position * bone`, then FFP world).
- `VkDrawUniforms` stays 768 bytes. Do not edit `FfpUniforms.glsl`.
- `C44Matrix` is row-major. The bone block is `std430, row_major`, same multiply as `vec4(pos, 1) * bone`.

## Vertex shader

In `Ffp.vert`, when `u.misc[2] != 0`:

- `location = 5`: blend weights, `R8G8B8A8_UNORM` (already 0..1).
- `location = 6`: blend indices, `R8G8B8A8_UINT` (not unorm).
- Binding 4: `readonly buffer BonePalette { mat4 bones[]; }`, `std430`, `row_major`.
- Four influences. Skip a weight of 0. Write blended position and normal, then run the existing lighting path.

`IBuildUniforms` sets `misc[2]` from the armed skin flag. A skin change must miss the uniform cache (`m_uniformGpuDrawMode` is the pattern).

## Vulkan plumbing

- Pipeline attributes grow from 5 to 7. Locations 0..4 stay position, normal, color, uv0, uv1. `s_gxAttribLocation[GxVA_BlendWeight] = 5`, `[GxVA_BlendIndices] = 6`.
- Blend indices use `VK_FORMAT_R8G8B8A8_UINT`. Do not send them through `s_gxAttribToVkFormat` (that table's type 1 is unorm).
- Set layout binding 4: `STORAGE_BUFFER_DYNAMIC`, vertex stage. Descriptor pool gains a matching count. `vkCmdBindDescriptorSets` dynamic offsets are `[uniformOffset, boneOffset]` (binding order).
- Cached sets stay keyed by textures. The bone buffer object is stable for the frame; the dynamic offset changes per draw.
- One bone ring per `VkFrame`, host-visible, `STORAGE_BUFFER | VERTEX` not required, just storage. Size 8 MiB. Align the cursor to `minStorageBufferOffsetAlignment`. Reset the cursor where `uniformOffset` resets (`PrepareFrame`).
- Range written into the descriptor is `256 * sizeof(C44Matrix)`.

Regenerate `Shaders/Generated/Ffp.vert.inc` with:

```bash
glslc -O --target-env=vulkan1.1 -mfmt=c -I .. -o Ffp.vert.inc ../Ffp.vert
```

from `Engine/Source/Gx/CGxDeviceVulkan/Shaders/Generated`. Do not change the 768-byte uniform block. `Tools/spirv-pin-compare.py` must still accept the module.

## M2 callers

CPU skin remains the fallback whenever `GpuSkinBind` returns 0.

**Characters** (`CM2Model::SetVertices`): if `!optimized && texCoord == 0` and `GpuSkinSupported()`, upload the section's `M2Vertex` range once as `GxVBF_PBNT2` (memcpy, no `TransformVertices_*`) and bind it. Cache that buffer without comparing bones. Env (`texCoord != 0`), optimized multi-section, and multi-sample stay on the CPU.

**Draw** (`CM2SceneRender::DrawBatch`, the non-shared, shaders-off branch): `GpuSkinBind(m_boneMatrices, m_bones.m_count)` immediately before `Draw`, then clear. Indices stay section-local, `m_minIndex == 0`, matching today's CPU batch. Bind even when `SetBatchVertices` skipped the upload; the bind-pose buffer is already on the device and the bones are new.

**Doodads** (`DrawBatchDoodad`): `DrawRigidDoodads` already instances one-bone meshes. Leave it. The `FillDoodadSkin` loop is the multi-bone case: if the shared section can sit in one bind-pose buffer, draw once per instance with that model's `GpuSkinBind` instead of rewriting vertices. If bind fails, keep the CPU loop.

**Other skin jobs** (`M2Scene.cpp`) stay on the CPU until the character path is correct.

## Squad

Ten agents per campaign. One writer owns each file. Reviewers do not edit those files.

| Campaign | Done when |
| --- | --- |
| G1 GPU skin | Character batches with `texCoord == 0` no longer call `TransformVertices_*`. D3D path unchanged. |
| G2 Doodad instances | Multi-bone doodads bind per instance instead of `FillDoodadSkin`, falling back when the ring is full. |
| G3 Upload once | Bind-pose buffers are static and survive a bone change. |
| G4 Device-local | Static meshes live in device memory; the CPU copy is staging only. |

G1 files:

| Agent | Writes |
| --- | --- |
| Device | `CGxDevice.h`, `CGxDevice.cpp`, `CGxDeviceVulkan.h`, `CGxDeviceVulkan.cpp`, `VkContext.h`, `VkContext.cpp`, `Shaders/Ffp.vert`, `Shaders/Generated/Ffp.vert.inc` |
| Bind pose | `M2Model.cpp` `SetVertices` only |
| Draw | `CM2SceneRender.cpp` `DrawBatch` and the doodad fallback |
| Scene jobs | read `M2Scene.cpp`; do not switch it in G1 |
| Faith, edges, leak, counter, scene, loop | `docs/perf-squads/gpu-m2/*.md` only |
