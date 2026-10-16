# Campaign 1 — m2-rigid loop

**NEXT SLICE**

Cycle 1. No writer slice has landed. A successful `DrawRigidDoodads` still stream-locks the section mesh and then uploads matrices. No Campaign 0 frame log is in tree; this call is from the source.

## Budget

Rigid batches move matrix bytes only, or fall back to the stream path when static buffers are absent.

| Outcome | Required bytes | Code now |
| --- | --- | --- |
| Static section buffers present, `m_valid`, `m_hasData` | `64 * N` (`C44Matrix` per instance) | Open. The call also moves `32 * V + 2 * I` |
| Static buffers absent, or the one-time upload fails | No mesh write in `DrawRigidDoodads`. `return 0`. Caller uses the stream path | Open. The function still `BufStream`s the mesh and returns `1` |

`V` = `section->m_vertexCount`. `I` = `section->m_indexCount`. `N` = `total`.

`32 * V + 2 * I + 64 * N` is the current successful call (`docs/perf-squads/m2-rigid/accountant.md`). `Ffp.vert` already multiplies bind-pose position and normal by `GpuDrawItem.world` (`Engine/Source/Gx/CGxDeviceVulkan/Shaders/Ffp.vert:66-85`). `CGxDeviceVulkan::DrawInstanced` copies each matrix into that record (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1491-1493`). The matrix term is in place. The mesh term is the gap.

## What the success path does

`DrawRigidDoodads` (`Engine/Source/Model2/CM2SceneRender.cpp:608`) is only called from `DrawBatchDoodad` while shaders are off (`CM2SceneRender.cpp:717`). A return of `1` skips the rest of the caller (`CM2SceneRender.cpp:718`).

The one-bone proof stays, and it is not the gap:

- Empty or non-single-bone section returns `0` (`CM2SceneRender.cpp:609-610`).
- Cache flag `8`, or one bone with flag `0x40`, returns `0` (`CM2SceneRender.cpp:613-614`). That case already draws through `DrawBatch` (`CM2SceneRender.cpp:462-488`).
- Every vertex `m_indices.b[0]` matches `verts[0]`, bone in range, section index in the skin (`CM2SceneRender.cpp:617-633`).
- `worlds` is one `m_boneMatrices[bone]` per instance (`CM2SceneRender.cpp:636-642`).

After that proof the function still rewrites the mesh on every batch:

- Vertex `BufStream` / `BufLock` (`CM2SceneRender.cpp:644-645`), then 8 floats per vertex from `&data->m_vertices[section->m_vertexStart]` (`CM2SceneRender.cpp:651-660`), bound as `GxVBF_PNT` (`CM2SceneRender.cpp:665`). Stride 32 (`Engine/Source/Gx/Buffer.cpp:25-27`).
- Index `BufStream` / `BufLock` (`CM2SceneRender.cpp:667-668`), bias `-m_vertexStart` plus the file indices at `m_skinData->m_skinSections[sectionIndex].m_indexStart` (`CM2SceneRender.cpp:674-679`), then `PrimIndexPtr` (`CM2SceneRender.cpp:683`).
- `DrawInstanced` with `m_start = 0`, `m_count = I`, `m_minIndex = 0`, `m_maxIndex = V - 1` (`CM2SceneRender.cpp:685-692`).

A null lock returns `0` (`CM2SceneRender.cpp:647-648`, `CM2SceneRender.cpp:670-671`) and `DrawBatchDoodad` falls through to the stream skin loop (`CM2SceneRender.cpp:770-820`). That fallback is only the lock failure. A section that passes the proof always takes the rewrite.

The comment at `CM2SceneRender.cpp:607` describes a GPU mesh plus a matrix per copy. The body at `CM2SceneRender.cpp:644-683` still copies the mesh into the stream ring.

## Shared `m_vertexBuf` / `m_indexBuf` are a different record

`CM2Shared::m_vertexBuf` is a static pool (`Engine/Source/Model2/M2Shared.cpp:390`). With shaders off, `SetVertices` fills `M2VertexPNT2` through `m_skinData->m_vertices[j]` and binds `GxVBF_PNT2` (`M2Shared.cpp:398-425`). `SetIndices` fills an instance-expanded `uint16_t` list in a dynamic pool (`M2Shared.cpp:340-341`, `M2Shared.cpp:348-373`). `DrawRigidDoodads` returns before either call (`CM2SceneRender.cpp:717-721`).

Those buffers stay on that fill. The rigid mesh is the 32-byte `GxVBF_PNT` pack and the single rebased index range above. Binding `m_vertexBuf` / `m_indexBuf` for this draw addresses different vertices.

## Next writer task

**Static bind for the one-bone path.** One concern. Files this squad already owns: `DrawRigidDoodads`, the `DrawBatchDoodad` call at `CM2SceneRender.cpp:717-719`, and one new `CM2Shared` upload/bind. Fields go after `m_updateLink` (`Engine/Source/Model2/M2Shared.h:74`). The method is declared after `SubstituteSpecializedShaders` (`M2Shared.h:108`) and defined beside `SetVertices`. Teardown is appended after the existing `DestroyBuffers` body (`M2Shared.cpp:631`).

1. Upload each one-bone section once into `GxPoolUsage_Static` vertex and index buffers. Bytes are the ones the stream loops write today: `V` vertices of position, normal, `texcoord[0]` from `&data->m_vertices[section->m_vertexStart]` (`CM2SceneRender.cpp:617`, `CM2SceneRender.cpp:651-660`), and `I` `uint16_t` indices with the bias and file `m_indexStart` at `CM2SceneRender.cpp:674-679`. Leave `SetIndices` and `SetVertices` unchanged.
2. When those buffers are non-null, `m_valid`, and `m_hasData`: `GxPrimVertexPtr` as `GxVBF_PNT`, `PrimIndexPtr`, keep the `worlds` gather (`CM2SceneRender.cpp:636-642`) and the batch at `CM2SceneRender.cpp:685-690`, then `DrawInstanced` (`CM2SceneRender.cpp:691`). This call does not `BufStream` or `BufLock`.
3. When the buffers are absent or the upload fails: `return 0` before any mesh write in `DrawRigidDoodads`. `DrawBatchDoodad` (`CM2SceneRender.cpp:717-719`) then draws that batch on the stream path (`CM2SceneRender.cpp:770-820`). Keep the existing `return 0` gates (`CM2SceneRender.cpp:609-633`).
4. Leave `FillDoodadSkin`, `AllocInstances`, `DrawInstanced`, and the shader-on branch unchanged. Counters stay as named in `docs/perf-squads/m2-rigid/counter.md`; this slice adds no counter code.

The next loop closes this budget when a batch with `m_hasData` already set moves `64 * N` only, and a missing static buffer returns `0` with no mesh write in `DrawRigidDoodads`. Skin-once and `AllocInstances` hysteresis stay later Campaign 1 slices.
