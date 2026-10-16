# Faith — Campaign 1, M2 mesh

Reviewed `94c1e172` (`DrawRigidDoodads`, `FillDoodadSkin`) and the skin paths Campaign 1 must leave alone. Vulkan fixed-function is `s_enableShaders == 0`. That flag is cache flag `0x8` (`WoW/Source/WorldClient/World.cpp:1724`). Vulkan reports no shader targets, so flag `0x8` stays off and the rigid early-out is live. D3D9 with shader targets sets flag `0x8`, and `DrawRigidDoodads` returns before drawing (`Engine/Source/Model2/CM2SceneRender.cpp:613-615`).

## Verdict

**FAIL.** Do not build the next slice on this early-out.

| Invariant | Result |
| --- | --- |
| A cache must not reuse verts after bones change | **PASS** |
| Multi-sample and env UV paths stay distinct | **FAIL** |
| Shader-on `SetVertices` stays correct | **PASS** |
| Instance index replication stays correct | **PASS** |

The rigid early-out also fails two behavior checks below. Those are integrator fixes on this approach, not a second algorithm.

## Cache versus bones — PASS

Nothing in this slice keeps skinned positions and replays them after `m_boneMatrices` changes.

- `DrawRigidDoodads` clears `worlds` and copies `m_boneMatrices[bone]` on every call (`Engine/Source/Model2/CM2SceneRender.cpp:636-642`). The stream lock rewrites bind-pose positions (`651-662`). Bind-pose does not go stale when a bone moves. `BufStream` clears `m_hasData` before that lock (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1073`).
- `FillDoodadSkin` skins each instance from that model's current bones into a fresh stream (`Engine/Source/Model2/CM2SceneRender.cpp:601-603`). There is no `(model, section, frame)` cache yet.
- The AVX2 blend-row cache is local to one `TransformVertices_*` call and rebuilds when the weight or bone-index key misses (`Engine/Source/Model2/M2SkinAvx2.cpp:38-47`). It does not survive the call.
- `CM2Shared::SetVertices` with shaders off stores bind-pose, not skinned positions (`Engine/Source/Model2/M2Shared.cpp:406-416`). The single-bone shared draw applies the current `m_boneMatrices[0]` as the world matrix (`Engine/Source/Model2/CM2SceneRender.cpp:475-479`).

## Multi-sample and env UV — FAIL

Multi-sample is still its own call. Env UV is not.

**Multi-sample stays on its own path.** `SetBatchVertices` sends batch flag `0x40` to `SetVerticesMultiSample` and every other shader-off section to `SetVertices` with the env bit (`Engine/Source/Model2/CM2SceneRender.cpp:187-190`). `SetVerticesMultiSample` writes both UV sets through `TransformVerticesNoUVSelect` (`Engine/Source/Model2/M2Model.cpp:4519-4541`, `3094-3095`) and binds `GxVBF_PNT2`. `DrawRigidDoodads` is only reached from `DrawBatchDoodad` (`CM2SceneRender.cpp:717`), so it does not swallow type-0 batches.

**Env UV collapses in the shared buffer.** Shader flag `0x4000` is supposed to select UV set 1 (`CM2SceneRender.cpp:179-184`). `CM2Shared::SetVertices` writes `m_texcoord[texCoordIndex]` only when `m_hasData` is clear (`Engine/Source/Model2/M2Shared.cpp:399-416`), then leaves `m_hasData` set (`422`). The next call with the other env bit hits that guard and rebinds the first fill. One cached buffer serves both env and non-env. This is the shader-off shared path (`cache flags & 0x40`, one bone). Do not copy that `m_hasData` key into a frame skin cache.

Doodad skin was already UV0, and the new path matches it: `FillDoodadSkin` passes texCoord `0` (`CM2SceneRender.cpp:603`) and `DrawRigidDoodads` writes `m_texcoord[0]` (`658-659`). That is not a new merge. It is also not an env-aware path.

## Shader-on `SetVertices` — PASS

The rigid early-out is guarded with `!CShaderEffect::s_enableShaders` (`CM2SceneRender.cpp:717`). Shader-on doodads still call `CM2Shared::SetVertices(0)` (`731`) and then upload this section's bones as shader constants (`738-763`).

`s_enableShaders` is flag `0x8` (`World.cpp:1724`), so the shader branch in `SetVertices` (`M2Shared.cpp:430-455`) runs with the same flag `SetIndices` treats as shared vertices (`355`). That branch still copies both UV sets and blend indices, then adds `0x01010101 * instance * boneCount` (`440-445`). Texcoord argument `0` is unused on this branch. `SetBatchVertices` still returns before the CPU env and multi-sample split (`CM2SceneRender.cpp:171-176`).

## Instance index replication — PASS

`SetIndices` still builds one index range per instance. Shared vertices (flag `0x8`, or one bone with flag `0x40`) keep absolute indices and step by the full vertex count. Otherwise the bias is `-vertexStart` and the step is the section vertex count (`M2Shared.cpp:355-373`). `DrawBatchDoodad` still uses that buffer whenever `DrawRigidDoodads` returns 0: shader-on `m_maxIndex` steps by the full vertex count (`CM2SceneRender.cpp:767`), and the CPU fallback steps by the section vertex count (`818`).

The rigid draw does not replicate indices. It biases one section by `-vertexStart` (`674-678`), the same bias as instance 0 of `SetIndices`, and repeats that range with `DrawInstanced` (`691`). `vkCmdDrawIndexed` takes that index count and the instance count (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1469-1470`). `gl_InstanceIndex` selects `GpuDrawItem` (`Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:67-68`). `AllocDrawItems` returns the cursor the draw passes as `firstInstance` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1065-1066`, `CGxDeviceVulkan.cpp:1496-1498`). Record size stays 448 (`CGxDeviceVulkan.cpp:20`).

## Rigid early-out — FAIL

Two predicates diverge from the CPU doodad path this function replaces.

**Bone influences.** The CPU path calls `s_transformVertices[m_boneInfluences]` (`CM2SceneRender.cpp:603`, `792`). Slot 1 is the one-bone matrix. Every other slot is the weighted blend (`M2Model.cpp:3872-3876`). `DrawRigidDoodads` gates on `m_boneCount == 1` and a shared `m_indices.b[0]` (`CM2SceneRender.cpp:609-624`), then applies that bone as a full matrix (`641`, `651-655`). It never reads `m_boneInfluences`. A section with bone count 1 and any other influence count skips the weighted skin. The gate has to be `m_boneInfluences == 1` before this early-out matches the CPU path.

**Normal renormalization.** Setup for doodads does not set `GxRs_NormalizeNormals` (`CM2SceneRender.cpp:318`). The CPU loop still does, from bone count and bone scale (`800-815`). The rigid path returns first (`717-718`), so the render state stays whatever the previous draw left. D3D9 applies that state (`Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:1655-1658`). Vulkan's fixed-function shader normalizes after the world multiply (`Ffp.vert:85-86`), which hides the missing state there. The D3D9 and GL fixed-function results do not.
