# Campaign G2 — GPU M2 doodad loop

**NEXT SLICE**

G2, cycle 1. No doodad slice has landed. `DrawBatchDoodad` still skins every multi-bone instance on the CPU. G1's `GpuSkinBind` / `GpuSkinSupported` are the API this slice calls. This note does not re-open the device, `Ffp.vert`, or the character batch.

## Budget

Multi-bone doodads draw one shared bind-pose mesh per instance, or they fall back to the stream skin.

| Outcome | What moves | Code now |
| --- | --- | --- |
| Section can sit in one bind-pose buffer, `GpuSkinBind` returns non-zero | One `M2Vertex` memcpy of the section, then `64 * boneCount` per instance | Open. The batch still writes `32 * instances * V` skinned bytes |
| `GpuSkinBind` returns 0, or the section cannot sit in one buffer | The existing `FillDoodadSkin` loop | Open. That loop is the only shaders-off path after `DrawRigidDoodads` |

`V` = `section->m_vertexCount`. `boneCount` = `m_data->m_bones.m_count`.

## What the shaders-off batch does

`DrawRigidDoodads` (`Engine/Source/Model2/CM2SceneRender.cpp:612`) still owns `m_boneCount == 1`. `DrawBatchDoodad` returns when it returns 1 (`CM2SceneRender.cpp:759-761`). That function stays as it is.

The multi-bone case is the shaders-off loop (`CM2SceneRender.cpp:812-862`). `SetIndices` (`CM2SceneRender.cpp:763`, `Engine/Source/Model2/M2Shared.cpp:338-384`) builds `m_instances` copies of every section and rewrites `m_skinSections[i].m_indexStart`. Each chunk then stream-locks `instances * V` vertices of 32-byte `GxVBF_PNT` and runs `FillDoodadSkin` (`CM2SceneRender.cpp:598-608`). A hit copies `M2SkinTake` (`Engine/Source/Model2/M2Scene.cpp:1943`). A miss calls `s_transformVertices[m_boneInfluences]` with texCoord `0`. One `Draw` covers the chunk. `m_start` stays on the expanded index start; the vertex buffer holds only that chunk, packed from vertex 0.

`Draw` has already set world and view to identity (`CM2SceneRender.cpp:1108-1113`). CPU skin bakes `m_boneMatrices` into the positions. The fixed-function multiply after that is identity.

After `InitializeSkinProfile` with cache flag `8` clear (`M2Shared.cpp:1299-1327`), `m_data->m_vertices[section->m_vertexStart]` for `V` vertices is the section in bind pose. Those `m_indices` are absolute bone ids into `m_boneMatrices`. `M2Vertex` is 48 bytes and is `GxVBF_PBNT2` (`Engine/Source/Model2/M2Data.h:119-126`, `Engine/Source/Gx/Buffer.cpp:90-97`). Flag `8` skips that rewrite. An opt-geo section is not inside `shared->m_skinSections`.

## G1 call this slice uses

```cpp
virtual int32_t GpuSkinBind(const C44Matrix* bones, uint32_t boneCount);
virtual int32_t GpuSkinSupported() const;
```

`GpuSkinSupported` is 1 only on the Vulkan device. `GpuSkinBind` copies `boneCount` matrices and arms the next `Draw`. It returns 0 when `boneCount` is 0, above 256, or the ring is full. `Draw` consumes the arm. `GpuSkinBind(nullptr, 0)` clears an arm that will not be drawn.

## Next writer task

**Per-instance bone bind for multi-bone doodads.** One concern. One file: `Engine/Source/Model2/CM2SceneRender.cpp`. Add a file-local function beside `DrawRigidDoodads`. Call it from `DrawBatchDoodad` only in the shaders-off branch, after the rigid return.

1. Return 0, and leave the caller on today's CPU loop, unless all of these hold. `GpuSkinSupported()` is 1. `section->m_boneCount > 1`. `m_boneInfluences` is 1..4. `m_data->m_bones.m_count` is 1..256. Cache flag `8` is clear. `uint32_t sectionIndex = section - shared->m_skinSections` is inside `m_skinData->m_skinSections`. `m_vertexCount` and `m_indexCount` are non-zero. `m_data->m_vertices` is non-null.
2. Stream one bind-pose vertex buffer and one index buffer for the batch, then reuse both for every instance. Vertex `BufStream` is stride 48, count `V`, `memcpy` of `&m_data->m_vertices[section->m_vertexStart]`. Bind it as `GxVBF_PBNT2`. Index `BufStream` is `uint16_t`, count `m_indexCount`. Source is `m_skinData->m_skinSections[sectionIndex].m_indexStart` with bias `-m_vertexStart`, the same gather as `DrawRigidDoodads` (`CM2SceneRender.cpp:655-718`). A failed lock returns 0 before any `GpuSkinBind`.
3. Set `GxRs_NormalizeNormals` to 1. For instance `n` in order, `CM2Model* model = elements[indices[n]].m_model`. If `model->m_boneMatrices` is null, or `GpuSkinBind(model->m_boneMatrices, m_data->m_bones.m_count)` returns 0, stop. Otherwise `GxPrimVertexPtr` the bind-pose buffer, `PrimIndexPtr` the section indices, and `Draw` one triangle list: `m_start = 0`, `m_count = m_indexCount`, `m_minIndex = 0`, `m_maxIndex = V - 1`. Bind immediately before that `Draw`. The world stack stays the identity from `Draw`.
4. Return the number of leading instances drawn. `DrawBatchDoodad` returns when that number is `total`. Otherwise call `SetIndices` and run the existing shaders-off loop with `done` starting at that number, and with the `instances` value from `AllocInstances` (`CM2SceneRender.cpp:753-757`). The CPU chunk still uses `m_curSkinSection->m_indexStart`, `GxVBF_PNT`, and `FillDoodadSkin`. Rebind `m_indexBuf` before that chunk when any GPU draw ran. Instances already drawn are not packed again.
5. A full miss (`return 0`) keeps today's order: `SetIndices`, then the loop from `done = 0`. The shader-on branch (`CM2SceneRender.cpp:772-811`) stays on that side of `SetIndices`.

`FillDoodadSkin`, `DrawRigidDoodads`, `DrawBatch`, `CM2Model::SetVertices`, `M2Scene.cpp`, `CM2Shared`, and the G1 device files stay as they are. `M2SkinPrepare` may still fill the CPU cache for these sections. The fallback reads it through `M2SkinTake`. This slice does not switch that job off.

The bind-pose bytes are streamed for this call. They are not stored on `CM2Shared`, not placed in `GxPoolUsage_Static`, and not uploaded device-local.

After this lands, the next loop checks a qualifying multi-bone batch for one `GpuSkinBind` plus one section `Draw` per instance that bound, and a `FillDoodadSkin` tail only from the first 0 return. The writer after that is G3: keep the bind-pose buffer and skip the memcpy across a bone change. G4, device-local meshes, stays after G3.
