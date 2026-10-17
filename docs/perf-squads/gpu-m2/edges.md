# G1 edges — GPU M2 skin

The plan's bind returns 0 for an empty palette, a palette above 256, and a full ring, and it leaves env, optimized, and multi-sample on the CPU. These nine are the draws that still come out wrong when that return is treated as "the CPU mesh is already bound."

Upload happens in `CM2Model::SetVertices` (`Engine/Source/Model2/M2Model.cpp:3248`). The bind happens later, in the non-shared shaders-off branch of `CM2SceneRender::DrawBatch` (`Engine/Source/Model2/CM2SceneRender.cpp:489`). `vkCmdDrawIndexed` passes vertex offset 0 (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1700`). `m_minIndex` does not move the fetch.

Bone indices in the vertex are absolute skeleton slots. `CM2Shared::InitializeSkinProfile` writes `m_boneCombos[m_boneComboIndex + skinBone]` into `M2Vertex.m_indices` and then turns the combo table into an identity (`Engine/Source/Model2/M2Shared.cpp:1310`, `M2Shared.cpp:1316`). `Ffp.vert` reads `skin.bones[inIndices[influence]]` (`Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:107`). The palette `GpuSkinBind` copies is `m_boneMatrices[0 .. m_bones.m_count)`, the same array `TransformVertices_*` indexes (`M2Model.cpp:3679`). `section->m_boneCount` is a different number.

## Bone count 0

**Trigger.** `m_data->m_bones.m_count == 0`, or `GpuSkinBind` is called with `bones == nullptr` or `boneCount == 0`. The non-shared branch is this model: `sharedVertices` requires a count of 1 (`CM2SceneRender.cpp:462`). A section with `m_boneCount == 0` on a model that still has bones is not this trigger. `InitializeSkinProfile` divides by the section count (`M2Shared.cpp:1251`); the bind argument stays the skeleton size.

**Expected.** No palette slot is consumed and `m_skinActive` is clear, so `u.misc[2]` stays 0. The draw is the CPU `GxVBF_PNT` from `TransformVertices_*` or `M2SkinTake`. A zero-length `memcpy` is not a successful pose. `GpuSkinBind(nullptr, 0)` after a draw that already consumed its arm is only a disarm; it does not skin that draw a second time and it does not allocate.

**Handles it.** `CGxDeviceVulkan::GpuSkinBind` disarms and returns 0. `M2BindPose` (`M2Model.cpp:139`) refuses the pose, and `CM2Model::SetVertices` publishes the CPU buffer and clears the mesh flag (`GpuSkinMesh(0)`) on every exit, including the lock failure at `M2Model.cpp:3267`. A stale flag there makes `DrawBatch` bind the previous section.

## Skeleton above 256

**Trigger.** `m_bones.m_count > 256`, or any vertex in the section has `m_indices.b[k] >= 256` or `>= m_bones.m_count`. Count 256 is in range: a `ubyte4` slot names bones 0..255, and the descriptor range is `256 * sizeof(C44Matrix)`. The load path stores the combo id with `static_cast<uint8_t>` (`M2Shared.cpp:1311`), so a skeleton past 256 is already truncated in the vertex. `M2NoteBone` rejects `bone >= 256` for the CPU upload cache (`M2Model.cpp:116`).

**Expected.** The whole batch stays on `TransformVertices_*`. The GPU does not upload a 256-prefix and let index 255 read a zeroed tail. `Ffp.vert` starts the blend at `vec3(0)` and skips a weight of 0 (`Ffp.vert:97`), so a zero matrix or a dropped influence pulls the vertex toward the origin. Passing `section->m_boneCount` instead of `m_bones.m_count` is the same failure: after the identity remap an index of 200 is bone 200, and a section whose combo length is 8 still names it.

**Handles it.** `M2BindPose` refuses before `GpuSkinMesh(1)`. `CGxDeviceVulkan::GpuSkinBind` returns 0 for `boneCount > 256` and writes nothing. `CM2SceneRender::DrawBatch` keeps passing `m_boneMatrices` and `m_bones.m_count` (`CM2SceneRender.cpp:495`).

## Ring exhaustion mid-frame

**Trigger.** An earlier `GpuSkinBind` in this command buffer succeeded. The next aligned slot does not fit. The slot is the full 256-matrix range rounded up to `minStorageBufferOffsetAlignment`, not `boneCount * sizeof(C44Matrix)` (`CGxDeviceVulkan.cpp:1298`). A tail that holds the live matrices and not the 16384-byte range is still exhaustion. `CM2Model::SetVertices` has already run: `M2BindPose` cached the `GxVBF_PBNT2` and set the mesh flag (`M2Model.cpp:3252`). `SetBatchVertices` may also have skipped that upload because the previous element matched type, model, section, and env (`CM2SceneRender.cpp:186`).

**Expected.** This draw and every later bind in the frame use the CPU `PNT`. The ring cursor stays where the last successful palette ended. Earlier draws keep the offsets already recorded. The failed call disarms (`GpuSkinBind` clears `m_skinActive` on entry, `CGxDeviceVulkan.cpp:1280`), so drawing the cached pose afterward shows the bind pose. The plan's "bind even when the upload was skipped" holds only when this call returns 1 and the bound buffer is that pose.

**Handles it.** `CGxDeviceVulkan::GpuSkinBind` returns 0 without a `memcpy` and without moving `boneCursor`. `CM2SceneRender::DrawBatch` is the function that sees the 0 after the pose is bound: on 0 it rebinds the CPU stream (`M2SkinTake` / `TransformVertices_*`) and draws with the mesh flag clear. `WriteSkin` (`M2Scene.cpp:1842`) stays the CPU producer for that take. `M2Scene.cpp` does not call `GpuSkinBind`.

## Optimized batches

**Trigger.** `element->m_flags & 4`, set when `m_optGeo` is live (`M2Scene.cpp:2125`). `SetBatchVertices` passes that bit as `optimized` (`CM2SceneRender.cpp:190`). `SetVertices` walks `m_optGeo->m_batchRanges` and concatenates each live sub-section (`M2Model.cpp:3280`). The merged header stores `m_vertexStart = 0`, a summed `m_vertexCount` (`M2Model.cpp:4846`), and the max `m_boneInfluences` (`M2Model.cpp:4820`). `CM2Model::SetIndices` rebases each original section by its own `m_vertexStart` and packs with `vertexBase` (`M2Model.cpp:3228`). One-bone sub-sections go through `TransformVerticesOneBone_*`, which ignores weights.

**Expected.** The draw is that concatenation as `GxVBF_PNT`, and `u.misc[2]` is 0. A `memcpy` of `&m_vertices[merged.m_vertexStart]` for `merged.m_vertexCount` copies the file prefix, which is not the order `SetIndices` indexed. One blend of the merged header using the max influence count moves every sub-section whose own count was lower.

**Handles it.** `CM2Model::SetVertices` keeps the optimized loop on `s_transformVertices` and does not call `M2BindPose`. `CM2Model::SetIndices` keeps the per-sub-section rebase. `CM2SceneRender::DrawBatch` does not call `GpuSkinBind` when `m_flags & 4` (`CM2SceneRender.cpp:494`). `WriteSkin` (`M2Scene.cpp:1859`) keeps feeding `M2SkinTake`.

## Multi-sample

**Trigger.** `m_curBatch->m_flags & 0x40`. `SetBatchVertices` calls `SetVerticesMultiSample` (`CM2SceneRender.cpp:187`), which writes `M2VertexPNT2` through `TransformVerticesNoUVSelect_cpp` (`M2Model.cpp:5094`, `M2Model.cpp:5124`). That kernel copies both `m_texcoord[0]` and `m_texcoord[1]` (`M2Model.cpp:3668`). `M2SkinPrepare` skips the flag (`M2Scene.cpp:1899`), so there is no `M2SkinTake` for it. The adjacency skip in `SetBatchVertices` compares section and env and does not compare flag `0x40`.

**Expected.** The draw is the CPU `GxVBF_PNT2` with skinned positions and both UVs. `GpuSkinBind` is not called. A skipped upload that leaves the previous `GxVBF_PBNT2` bound, with `GpuSkinMesh` still 1, draws bind-pose positions. The following batch of another format then skins a buffer that has no blend attributes: missing locations 5 and 6 come from `m_dummyVertices` (`CGxDeviceVulkan.cpp:1422`), whose first 16 bytes are 0 (`CGxDeviceVulkan.cpp:384`). Every weight is 0, `Ffp.vert` writes `position = vec3(0)`, and the mesh sits on the origin.

**Handles it.** `CM2Model::SetVerticesMultiSample` stays on `TransformVerticesNoUVSelect_cpp` and clears the mesh flag. `CM2SceneRender::SetBatchVertices` uploads again when the `0x40` bit differs from the previous batch. `CM2SceneRender::DrawBatch` does not bind while the bit is set.

## Env UV

**Trigger.** `m_shader & 0x4000`. `SubstituteSimpleShaders` sets the bit when the texture-coord combo is 1 (`M2Shared.cpp:661`). `SetBatchVertices` passes `texCoord != 0` (`CM2SceneRender.cpp:179`). `TransformVertices_*` writes `m_texcoord[texCoord]` into the only UV of `GxVBF_PNT` (`M2Model.cpp:3729`). `M2BindPose` copies an `M2Vertex` unchanged, so `texcoord[0]` and `texcoord[1]` stay in separate channels (`M2Model.cpp:176`). `Ffp.vert` samples `inTexCoord0` unless `u.stageCoord` selects `inTexCoord1` (`Ffp.vert:44`). The CPU path does not set that stage; it bakes the chosen channel into UV 0. The pose cache key is `(model, section)` and has no texCoord (`M2Model.cpp:146`).

**Expected.** The env batch is CPU `PNT` with `m_texcoord[1]` in UV 0, and `u.misc[2]` is 0. The bind-pose buffer is not this draw's vertex buffer. A later non-env batch of the same section still finds its own pose; the env bytes never occupy that slot.

**Handles it.** `CM2Model::SetVertices` keeps `texCoord != 0` on `TransformVertices_*` / `M2SkinTake` and clears the mesh flag. `CM2SceneRender::DrawBatch` does not bind when `m_shader & 0x4000`. `WriteSkin` keeps texCoord in the job key (`M2Scene.cpp:1905`).

## Index rebase vs vertexStart

**Trigger.** A non-shared section with `m_vertexStart != 0`. After the identity remap, file index `I` names vertex `I` in `m_vertices`. The section occupies `[m_vertexStart, m_vertexStart + m_vertexCount)`. `CM2Shared::SetIndices` stores `uint16_t(fileIndex - m_vertexStart)` when cache flag 8 is clear and the model is not the one-bone shared case (`M2Shared.cpp:362`), then overwrites the shared section's `m_indexStart` with the cursor in that index buffer (`M2Shared.cpp:373`). `DrawBatch` draws `m_start = m_indexStart`, `m_minIndex = 0` (`CM2SceneRender.cpp:468`, `CM2SceneRender.cpp:490`). The element's section is that shared copy (`M2Scene.cpp:2069`). Vulkan's vertex offset is the constant 0 (`CGxDeviceVulkan.cpp:1700`). D3D uses `m_minIndex` as a range hint, not as the base (`Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:3291`).

**Expected.** The pose buffer's vertex 0 is `m_vertices[m_vertexStart]`, and the index buffer is the one `SetIndices` already rebased. Those two agree, and nothing subtracts `m_vertexStart` again. Setting `m_minIndex = m_vertexStart` (the shader and one-bone branches, `CM2SceneRender.cpp:481`) does not repair a full-mesh buffer on Vulkan, and it does not undo a rebase that already happened. The one-bone path leaves indices absolute (`offset = 0`) and draws the whole shared mesh; a section slice bound with that index buffer fetches `fileIndex` past the slice. Opt-geo is the other rebase: `vertexBase + fileIndex - original.m_vertexStart` against a concatenation (`M2Model.cpp:3228`), while the merged header's `m_vertexStart` is 0. G1 does not put that header through `M2BindPose`.

**Handles it.** `CM2Shared::SetIndices` owns the character rebase. `M2BindPose` copies `&m_vertices[section->m_vertexStart]` for `m_vertexCount` vertices and no other range. `CM2SceneRender::DrawBatch` keeps `m_minIndex == 0` on the non-shared branch. `CGxDeviceVulkan::Draw` keeps vertex offset 0.

## Two frames in flight

**Trigger.** `FramesInFlight` is 2 (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:141`). `EndFrame` advances `m_frameIndex` (`VkContext.cpp:945`). `PrepareFrame` on a slot that is not `prepared` waits that slot's fence, then zeros `boneCursor` (`VkContext.cpp:733`, `VkContext.cpp:742`). The zero is outside the `VK_SUCCESS` branch, so a timeout (`fenceUnsignaled`, 100 ms) still resets the cursor and skips `Collect`. The other slot's `PrepareFrame` does not wait on this fence. `m_skinActive` and `m_skinOffset` live on the device. The pose `VkBuffer` is one allocation both slots read. Descriptor sets are per `VkFrame` and store that frame's `bones` buffer (`CGxDeviceVulkan.cpp:1579`).

**Expected.** A palette written on slot 0 is sampled only by slot 0's command buffer, at the offset `GpuSkinBind` stored for that slot. Slot 1's `Draw` does not apply slot 0's offset to slot 1's ring. A timed-out wait does not mean the bytes are free: `GpuSkinBind` leaves them unread and returns 0, and the batch takes the CPU path. The pose buffer stays immutable while either fence is pending; the CPU fallback writes the stream, not that allocation. `IInvalidateCmdState` already drops `m_boundSet` (`CGxDeviceVulkan.cpp:609`). A set created against slot 0's bone buffer is not rebound on slot 1 when the textures match.

**Handles it.** `VkContext::PrepareFrame` resets `boneCursor` for this slot only. `CGxDeviceVulkan::GpuSkinBind` writes `Frame().bones` and refuses the write when this slot's fence wait timed out. `CGxDeviceVulkan::Draw` consumes an offset only when the current `m_frameIndex` is the slot that took the bind. `CM2Model::SetVertices` leaves the pose buffer alone on the CPU fallback.

## Skin flag on the next draw

**Trigger.** `GpuSkinBind` sets `m_skinActive`. `Draw` copies it to `m_skinThisDraw` and clears it (`CGxDeviceVulkan.cpp:1341`), but the copy sits after the `!m_frameActive` return (`CGxDeviceVulkan.cpp:1337`). `IBuildUniforms` writes `u.misc[2]` from `m_skinThisDraw` (`CGxDeviceVulkan.cpp:1132`). The uniform cache misses when `m_uniformSkin` differs (`CGxDeviceVulkan.cpp:1538`). `IInvalidateCmdState` clears `m_uniformsValid` and the bound set (`CGxDeviceVulkan.cpp:617`) and does not clear `m_skinActive`. Terrain runs in `CWorldScene::RenderChunks` (`WoW/Source/WorldClient/CWorldSceneRender.cpp:200`) before `CM2Scene::Draw` (`WoW/Source/Ui/WorldFrame.cpp:1734`, `WorldFrame.cpp:1757`). The first draws of the next frame are terrain. `DrawTerrainBatch` sets `m_gpuDrawMode = 2` and calls `Draw` (`CGxDeviceVulkan.cpp:1805`). It never calls `GpuSkinBind`. The same `Ffp.vert` serves that draw. Locations 5 and 6 are absent on terrain, so they bind `m_dummyVertices` at offset 0.

**Expected.** A terrain, liquid, weather, particle, ribbon, or UI draw records `u.misc[2] == 0` and bone offset 0. The position stays `inPosition`. An arm that survived the early return is dropped before that draw, including when `DrawBatch`'s post-clear (`CM2SceneRender.cpp:500`) does not run because the bind returned 0. A cache hit on the previous skinned uniform block is a miss, same as `m_gpuDrawMode`. With the flag left on, every weight is 0, the blend sum stays `vec3(0)`, and the terrain chunk is drawn at the origin.

**Handles it.** `CGxDeviceVulkan::Draw` disarms on every exit, including the return before the copy. `CGxDeviceVulkan::IBuildUniforms` publishes `misc[2]` only for the draw that consumed a successful bind. `DrawTerrainBatch` does not arm. `Ffp.vert` applies the blend only when `u.misc.z != 0`.
