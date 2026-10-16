# Campaign 1 layout — rigid static bind

`DrawRigidDoodads` stops stream-locking the section. It binds the mesh `CM2Shared` already builds and uploads one bone matrix per doodad through the existing `DrawInstanced`.

One writer slice. `DrawRigidDoodads` and the shader-off call in `DrawBatchDoodad`. Do not edit the shader-on branch, `CM2Shared::SetVertices`'s shader arm, `Ffp.vert`, or `GpuDrawItem`.

`// OFFSET:` entry points stay. Batch order, material, lighting, and sphere-map texgen stay. New comments use the repo TextEmoji form.

## What the call does today

On success (`Engine/Source/Model2/CM2SceneRender.cpp:692`) the function:

1. Proves the section is one bone (`CM2SceneRender.cpp:609-628`).
2. Copies `m_boneMatrices[bone]` into a function-static `worlds` vector (`CM2SceneRender.cpp:636-642`).
3. `BufStream`s a fresh `GxVBF_PNT` of `m_vertexCount` verts (`CM2SceneRender.cpp:644-665`) and a fresh index range rebased by `uint16(-m_vertexStart)` (`CM2SceneRender.cpp:667-683`).
4. `DrawInstanced` with `m_start = 0`, `m_count = m_indexCount`, `m_minIndex = 0`, `m_maxIndex = m_vertexCount - 1` (`CM2SceneRender.cpp:685-691`).

`DrawBatchDoodad` returns before `SetIndices` / `SetVertices` when that succeeds (`CM2SceneRender.cpp:717-718`). The mesh those two functions already own is never bound. Per successful call the mesh term is `32 * V + 2 * I` on top of `64 * N` matrices (`docs/perf-squads/m2-rigid/accountant.md`).

Steady state after the shared buffers are valid: **`64 * N` only.** The first fill inside `SetVertices` / `SetIndices` is the existing one-time upload, not a per-batch rewrite.

## Shared buffers

Owner is `CM2Shared`. Both pointers are null in the constructor (`Engine/Source/Model2/M2Shared.cpp:1519-1523`). `DestroyBuffers` (`M2Shared.cpp:602-631`) is the only release: `~CM2Shared` (`M2Shared.cpp:1882`) and a capacity step in `AllocInstances` (`M2Shared.cpp:779`). This slice does not change `AllocInstances`. A destroy runs **before** the rigid call (`CM2SceneRender.cpp:711` then `:717`), so the bind does not alias pools that call just dropped. After `Draw` records, those pools stay until the existing `PoolDestroy` → `RetireBuffer` path. Do not free them at the end of the call.

| Buffer | Create | Usage | Bytes | Fill |
| --- | --- | --- | --- | --- |
| `m_vertexPool` / `m_vertexBuf` | `SetVertices` (`M2Shared.cpp:389-391`) | `GxPoolUsage_Static` | `sizeof(M2Vertex) * m_instances * skinVertexCount` | Once, while `!m_hasData \|\| !m_valid` |
| `m_indexPool` / `m_indexBuf` | `SetIndices` (`M2Shared.cpp:339-341`) | `GxPoolUsage_Dynamic` | `sizeof(uint16_t) * m_instances * skinIndexCount` | Once, same predicate (`M2Shared.cpp:348`) |

`M2Vertex` is 48 bytes, fields through `0x0020` (`Engine/Source/Model2/M2Data.h:119-126`). The index pool is not `GxPoolUsage_Static`. It is still the buffer this slice binds. Do not add a second index pool and do not change the usage argument: the shader-on path creates this same pool.

`m_instances` is the replica count baked into both allocations. It starts at 1 after `InitializeSkinProfile` (`M2Shared.cpp:1150`). This slice does not grow it.

### Vertex bytes, shaders off

`SetVertices` splits on `CShaderEffect::s_enableShaders` (`M2Shared.cpp:398`). The rigid call only runs when that flag is 0 (`CM2SceneRender.cpp:717`), so the fill that runs is the fixed-function arm:

- Casts the lock to `M2VertexPNT2*` and writes slot `j` for `j` in `[m_vertexStart, m_vertexStart + m_vertexCount)` (`M2Shared.cpp:406-416`).
- `M2VertexPNT2` is 40 bytes, fields through `0x0018` (`M2Data.h:112-117`). Slot `j` is byte `j * 40`, not `j * 48`. The pool is sized in `M2Vertex` units, which is larger, so the packed 40-byte span fits.
- Source is `m_data->m_vertices[m_skinData->m_vertices[j]]`. On this path cache flag 8 is clear (`CM2SceneRender.cpp:613`), and that load has already set `m_skinData->m_vertices[j] = j` (`M2Shared.cpp:1187-1214`). Slot `j` is skin vertex `j`, bind pose.
- Binds `GxVBF_PNT2` (`M2Shared.cpp:425`). Stride 40 (`Engine/Source/Gx/Buffer.cpp:39-43`).
- Does **not** replicate per `m_instances`. One copy of the skin.

`SetVertices(0)` stores `m_texcoord[0]` in GPU slot 0 and `m_texcoord[1]` in slot 1 (`M2Shared.cpp:415-416`). The fill does not run again once `m_hasData` is set.

The shader-on arm (`M2Shared.cpp:430-458`) writes `M2Vertex` / `GxVBF_PBNT2` and bone-index palettes. Leave it. Do not clear `m_hasData` from the rigid path: that would make the shader arm refill.

### Index bytes

`SetIndices` does not look at `s_enableShaders`. For every section it writes `m_instances` copies (`M2Shared.cpp:363-369`).

`DrawRigidDoodads` already returns 0 when `sharedVertices` is true (`CM2SceneRender.cpp:613`). That is the same predicate `SetIndices` uses (`M2Shared.cpp:355`). Models that stay in this function are the `sharedVertices == false` case:

- Copy 0 stores `uint16(skinIndex + uint16(-m_vertexStart))` (`M2Shared.cpp:361-365`).
- Copy `k` then adds `k * m_vertexCount`, with `uint16` wrap (`M2Shared.cpp:369`).
- `m_skinSections[i].m_indexStart` becomes the GPU cursor of that whole run (`M2Shared.cpp:372`). The file cursor stays on `m_skinData->m_skinSections[i].m_indexStart` (`M2Shared.cpp:360`). `SetIndices` reads the file cursor, so calling it from the rigid path does not eat its own output.

Copy 0 is section-local: skin index 105 with `vertexStart == 100` is stored as 5. Copies `1 .. m_instances-1` are the shader and CPU doodad draws (`CM2SceneRender.cpp:766`, `:817`), which draw `instances * m_indexCount` indices against a vertex buffer that was replicated or packed per instance. The shader-off vertex buffer has no such replicas. `DrawInstanced` draws **one** index range and many worlds (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:135`, `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1478-1499`). Bind copy 0 only.

## Why both buffers at offset 0 are the wrong mesh

Vulkan's indexed draw passes vertex offset 0 and ignores `m_minIndex` (`CGxDeviceVulkan.cpp:1470`). OpenGL uses the index as the vertex id (`Engine/Source/Gx/CGxDeviceOpenGL/CGxDeviceOpenGl.cpp:3981`). D3D adds `m_index / m_itemSize` from the buffer, not `m_minIndex` (`Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:3291-3299`).

| Bind | Index fetched | What shows |
| --- | --- | --- |
| `m_vertexBuf` at `m_index == 0`, copy-0 indices, no extra base | `skinIndex - vertexStart` | The vertex `vertexStart` slots earlier. Section 0 is fine. Every later section shows an earlier section. |
| Bias applied again to indices `SetIndices` already biased | `uint16(skinIndex - 2 * vertexStart)` | Wrapped ids. Off the end of the section. |
| File indices left absolute, vertices packed to local 0 | `skinIndex` | Into the next section. |

Do not rewrite either buffer to paper over this. The shader-on draw and the CPU doodad draw both consume the biased, replicated index buffer and the unshifted vertex buffer. A second copy of the indices, or a patch of `m_vertexBuf->m_index`, changes those draws.

## Vertex view

The device already binds `CGxBuf::m_index` as a byte offset (`CGxDeviceVulkan.cpp:1262-1263`, D3D `SetStreamSource` at `CGxDeviceD3d.cpp:984-990`). `BufCreate` stores that argument (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1667-1671`, `Engine/Source/Gx/CGxBuf.h:15`). Give the rigid draw a `CGxBuf` that names the same pool at the section's first vertex. Leave `m_vertexBuf->m_index` at the value `SetVertices` created (0).

No new GPU allocation. No new pool. Not linked into `m_bufList`. `DestroyBuffers` and `PoolDestroy` must not see it: they walk the pool list and `BufDestroy` (`M2Shared.cpp:603-630`).

Owner is `DrawRigidDoodads`. One function-static `CGxBuf`, process lifetime, constructed once with a null pool. Each successful bind overwrites the fields below and does not `LinkToTail`. `Draw` copies the `VkBuffer` and the numeric offset into the command buffer before it returns (`CGxDeviceVulkan.cpp:1256-1263`), so the next section may reuse the same object. The static object is what `m_primVertexBuf` keeps; a stack buf would dangle after return.

```cpp
static CGxBuf rigidView(nullptr, sizeof(M2VertexPNT2), 0, 0);

rigidView.m_pool      = shared->m_vertexBuf->m_pool;
rigidView.m_itemSize  = sizeof(M2VertexPNT2);          // 40
rigidView.m_itemCount = section->m_vertexCount;
rigidView.m_size      = sizeof(M2VertexPNT2) * section->m_vertexCount;
rigidView.m_index     = shared->m_vertexBuf->m_index
                      + section->m_vertexStart * sizeof(M2VertexPNT2);
rigidView.m_hasData   = 1;
rigidView.m_valid     = shared->m_vertexBuf->m_valid;
rigidView.m_dirty     = 0;
```

`GxPrimVertexPtr(&rigidView, GxVBF_PNT2)`.

Local index `k` then fetches byte `(vertexStart + k) * 40`, which is slot `vertexStart + k` of the `M2VertexPNT2` fill. That is the skin vertex `SetIndices` subtracted out.

D3D without stream offset sets the byte offset to 0 and uses `baseVertexIndex = m_index / m_itemSize` (`CGxDeviceD3d.cpp:984-986`, `:3291-3299`). `m_itemSize == 40` and `m_index == vertexStart * 40` make that base `vertexStart`. Same slot.

Bounds, or return 0 (fallback below):

- `m_vertexBuf->m_pool` is non-null.
- `section->m_vertexStart + section->m_vertexCount` is `<= m_skinData->m_vertices.m_count`.
- `rigidView.m_index + rigidView.m_size` is `<= m_vertexBuf->m_index + m_vertexBuf->m_size`.

Do not require a 16-byte offset. Stream locks already bind `m_index` at the caller's item size (`CGxDeviceVulkan.cpp:1605`, `VkContext.cpp:1086-1088`). This offset is the same kind of value: a multiple of the bound stride.

## Index range and matrices

After `SetIndices` returns 1, `section` is `&shared->m_skinSections[sectionIndex]` (`CM2SceneRender.cpp:630`). Its `m_indexStart` is the GPU cursor, not the file cursor.

```cpp
CGxBatch batch;
batch.m_primType = GxPrim_Triangles;
batch.m_start    = section->m_indexStart;   // copy 0
batch.m_count    = section->m_indexCount;   // not m_instances * count
batch.m_minIndex = 0;
batch.m_maxIndex = static_cast<uint16_t>(section->m_vertexCount - 1);
```

`PrimIndexPtr` is already done inside `SetIndices` (`M2Shared.cpp:381`). Do not bind a later replica. Do not chunk on `m_instances`. `total` may exceed `m_instances`; the extra replicas are not read. One `DrawInstanced` of this range with `total` matrices.

`worlds` stays the function-static vector (`CM2SceneRender.cpp:636`). `clear` keeps capacity. Push `elements[indices[n]].m_model->m_boneMatrices[bone]` for `n` in `0 .. total` (`CM2SceneRender.cpp:640-642`). That bone is the absolute index the scan proved (`CM2SceneRender.cpp:617-628`), after the flag-8 load wrote it into `m_indices.b[0]` (`M2Shared.cpp:1198-1200`). Do not index `m_boneCombos` here. The shader doodad path does (`CM2SceneRender.cpp:744`); this path must not.

`DrawInstanced(&batch, worlds.data(), total)`. Vulkan copies each matrix into `GpuDrawItem.world` and draws once (`CGxDeviceVulkan.cpp:1491-1499`). The 448-byte record and `Ffp.vert` stay. If `AllocDrawItems` fails, the base loop draws the same bound mesh once per matrix (`CGxDevice.cpp:127-144`). Still no mesh lock. Return 1 either way.

`Ffp.vert` multiplies the bind-pose position and normal by that matrix (`Shaders/Ffp.vert:66-86`), which is the one-bone CPU skin (`Engine/Source/Model2/M2Model.cpp:3169-3175`) with the world left on the matrix instead of baked into the vertices.

## When the buffers are not ready

`DrawRigidDoodads` runs before `DrawBatchDoodad` calls `SetIndices` (`CM2SceneRender.cpp:717-721`), and `AllocInstances` may have just nulled the pointers (`M2Shared.cpp:779`). Ready means both builders have returned 1 on this call and the bufs are still valid:

```text
if (!shared->SetIndices() || !shared->SetVertices(0))
    not ready
if (!m_vertexBuf || !m_indexBuf)
    not ready
if (!m_vertexBuf->m_hasData || !m_vertexBuf->m_valid)
    not ready
if (!m_indexBuf->m_hasData || !m_indexBuf->m_valid)
    not ready
if the view bounds above fail
    not ready
```

`SetVertices(0)` while shaders are off takes the `PNT2` arm and binds the full buffer at offset 0. Override that bind with `rigidView` after it returns. Do not call `SetVertices` when `s_enableShaders` is set; the caller already skips this function.

Not ready, and `shared->m_instances == 0` while `total > 0`: return **1** and do not draw. `AllocInstances` returns 0 when both `m_instances` and `m_maxInstances` are still 0 (`M2Shared.cpp:771`, `:1525`), which is before `InitializeSkinProfile` (`M2Shared.cpp:1150`). The fallback loop is `done < total; done += instances` (`CM2SceneRender.cpp:771`). A 0 from this function in that state does not advance. Skin that has reached `:1150` has `m_instances >= 1`, so a later failure still returns 0.

Every other not-ready case returns **0**. `DrawBatchDoodad` then `SetIndices` and skins (`CM2SceneRender.cpp:721`, `:770-819`). Do not `BufStream` inside `DrawRigidDoodads` on the way out. A null lock is not a second mesh path.

`total == 0` returns 1 after the gates below. `DrawInstanced` would no-op (`CGxDeviceVulkan.cpp:1479`). Falling through is harmless but is not a buffer miss.

## Gates that stay

Return 0, unchanged, so the fallback keeps these:

| Gate | Where | Why it stays |
| --- | --- | --- |
| Null `data` / `shared` / `cache` / `section`, `m_boneCount != 1`, zero vertex or index count | `CM2SceneRender.cpp:609` | Not one rigid mesh. |
| Cache flag 8, or one bone and cache flag `0x40` | `CM2SceneRender.cpp:613` | `SetIndices` left the indices absolute (`M2Shared.cpp:355-361`). A `vertexStart` byte offset would shift them a second time. Flag `0x40` already draws shared verts plus `m_boneMatrices[0]` from `DrawBatch` (`CM2SceneRender.cpp:462-488`). |
| Any vertex whose `m_indices.b[0]` differs, or the bone is past `m_bones` | `CM2SceneRender.cpp:617-628` | One matrix cannot skin the span. |
| `section - m_skinSections` past the shared array | `CM2SceneRender.cpp:630-633` | Opt-geo sections live on `m_optGeo` (`Engine/Source/Model2/M2Scene.cpp:1805`) and own another index buf (`M2Model.cpp:2645`). |

Add `section->m_boneInfluences != 1` to the `:609` test. Slot 1 is the one-bone transform (`M2Model.cpp:3873`). Any other slot blends weights (`M2Model.cpp:3872-3876`). A single matrix drops those weights.

Add, at the `DrawBatchDoodad` call only (`CM2SceneRender.cpp:717`), before entering `DrawRigidDoodads`:

- `m_curBatch->m_shader & 0x4000` — stored env UV. `SubstituteSimpleShaders` sets it when the coord combo is 1 (`M2Shared.cpp:656-658`). Stage 0 samples coord 0 (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1424`). The shared fill puts `m_texcoord[0]` in that slot when called as `SetVertices(0)`. The env batch needs `m_texcoord[1]` there. `SetVertices` will not refill (`M2Shared.cpp:398`). Fall through. The CPU doodad skin still passes texCoord 0 (`CM2SceneRender.cpp:603`); this slice does not fix that hole, and it does not bind the shared buffer over it.
- `m_curBatch->m_flags & 0x40` — multi-texture / multi-sample. Shader-off load sets the flag when `m_textureCount > 1` (`M2Shared.cpp:1240-1250`). `SetBatchVertices` then takes `SetVerticesMultiSample` (`CM2SceneRender.cpp:187-188`), which writes both UV sets (`M2Model.cpp:3094-3095`). The doodad fallback does not. Binding `GxVBF_PNT2` would feed stage 1 a real `texcoord[1]` where today's `GxVBF_PNT` stream leaves the dummy attribute. Fall through.

Sphere-map is the other bit (`CM2SceneRender.cpp:118-125`), already applied by `SetupTextures` before the rigid return (`CM2SceneRender.cpp:708`). Positions stay in model space. Leave that state alone. Those batches may bind.

Do not add these two tests inside the shader-on branch.

## Steady call

Shaders off, gates passed, buffers valid:

1. `SetIndices` and `SetVertices(0)`. No lock when `m_hasData` and `m_valid` are already set.
2. Write `rigidView` and `GxPrimVertexPtr` it as `GxVBF_PNT2`.
3. Fill `worlds` with `total` matrices.
4. `DrawInstanced` of copy 0.
5. Return 1. No `BufStream`, no `BufLock`, no write to `m_vertexBuf` or `m_indexBuf`.

## Out of scope

- Shader-on `DrawBatchDoodad` (`CM2SceneRender.cpp:730-768`), shader-on `SetBatchVertices` (`CM2SceneRender.cpp:171-176`), shader-on `DrawBatch` (`CM2SceneRender.cpp:431-474`), and `CM2Shared::SetVertices` from `M2Shared.cpp:430` down.
- `RenderModelBatchesForProjectedTexture` and `RenderBatchShadowMap` (`M2Model.cpp:2889`, `:2943-2945`). They already call `SetVertices(0)`.
- `AllocInstances` growth and `DestroyBuffers` on that step. The other Campaign 1 slice owns that. This slice only has to tolerate a null buf after the call that already happened.
- Frame skin cache, `CM2Model::SetVertices`, `FillDoodadSkin`. They remain the return-0 path.
- `VkDrawUniforms`, `GpuDrawItem` field order, sort, geoset visibility, `m_maxInstances`.
