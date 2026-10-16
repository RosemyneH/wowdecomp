# Campaign 1 counter — rigid doodad stream

`DrawRigidDoodads` (`Engine/Source/Model2/CM2SceneRender.cpp:608`) proves a skin section is one bone, then stream-locks and rewrites that section's vertices and indices on every batch. The mesh does not change. The per-instance data is `m_boneMatrices[bone]` (`CM2SceneRender.cpp:641`).

The only caller is `CM2SceneRender::DrawBatchDoodad` (`CM2SceneRender.cpp:717`), and only while `CShaderEffect::s_enableShaders` is 0. On success the caller returns at `CM2SceneRender.cpp:718`.

Counters only. No behavior change. Each increment sits inside the Campaign 0 compile-out flag (default off). Storage is `std::atomic<uint64_t>` with a relaxed add. Campaign 0 samples and clears these fields on the one-second line. The names below stay fixed.

Zone the function with `OsGetAsyncTimeClocks` (`Engine/Source/Os/Linux/TimeManager.cpp:59`, `CLOCK_MONOTONIC`) on entry at `CM2SceneRender.cpp:608` and on every return. The delta adds to `m2_rigid_clocks`. The zone includes calls that return before a rewrite.

## Counters

| Field | Unit | Add | Site |
| --- | --- | --- | --- |
| `m2_rigid_stream_bytes` | bytes | `section->m_vertexCount * 32`, then `section->m_indexCount * 2` | `CM2SceneRender.cpp:645`, `CM2SceneRender.cpp:668` |
| `m2_rigid_vertices` | vertices | `section->m_vertexCount` | `CM2SceneRender.cpp:645` |
| `m2_rigid_indices` | indices | `section->m_indexCount` | `CM2SceneRender.cpp:668` |
| `m2_rigid_instances` | instances | `total` | `CM2SceneRender.cpp:691` |
| `m2_rigid_static_binds` | batches | `1` | `CM2SceneRender.cpp:683` |
| `m2_rigid_clocks` | clocks | `OsGetAsyncTimeClocks` delta | `CM2SceneRender.cpp:608` through each return |

## Stream bytes, vertices, indices

Vertex stream at `CM2SceneRender.cpp:644`:

`BufStream(GxPoolTarget_Vertex, 32, section->m_vertexCount)`

`BufLock` is `CM2SceneRender.cpp:645`. The loop at `CM2SceneRender.cpp:651-660` writes 8 floats per vertex (`out += 8`). That is the `GxVBF_PNT` stride (`Engine/Source/Gx/Buffer.cpp:25-27`). `GxPrimVertexPtr` of the stream buffer is `CM2SceneRender.cpp:665`.

When `BufLock` at `CM2SceneRender.cpp:645` returns a pointer:

- `m2_rigid_vertices += section->m_vertexCount`
- `m2_rigid_stream_bytes += section->m_vertexCount * 32`

Index stream at `CM2SceneRender.cpp:667`:

`BufStream(GxPoolTarget_Index, 2, section->m_indexCount)`

`BufLock` is `CM2SceneRender.cpp:668`. The loop at `CM2SceneRender.cpp:677-679` writes `section->m_indexCount` `uint16_t` values. The bias at `CM2SceneRender.cpp:675` leaves that count unchanged. `PrimIndexPtr` of the stream buffer is `CM2SceneRender.cpp:683`.

When `BufLock` at `CM2SceneRender.cpp:668` returns a pointer:

- `m2_rigid_indices += section->m_indexCount`
- `m2_rigid_stream_bytes += section->m_indexCount * 2`

A null vertex lock (`CM2SceneRender.cpp:647`) adds nothing. A null index lock (`CM2SceneRender.cpp:670`) keeps the vertex add from `CM2SceneRender.cpp:645` and adds no indices.

## Instances

`DrawInstanced(&batch, worlds.data(), total)` is `CM2SceneRender.cpp:691`. `total` is `m_curElement->m_count` (`CM2SceneRender.cpp:710`), passed in at `CM2SceneRender.cpp:717`.

`m2_rigid_instances += total` at `CM2SceneRender.cpp:691`.

One rewrite serves every instance in the batch. Vertex and index totals stay the section size. They stay independent of `total`.

The `worlds` matrix vector (`CM2SceneRender.cpp:636-642`) is CPU instance data. It stays outside `m2_rigid_stream_bytes`.

## Shared static bind

`m2_rigid_static_binds += 1` at `CM2SceneRender.cpp:683`, after both stream binds (`CM2SceneRender.cpp:665` and `CM2SceneRender.cpp:683`).

That tick is one pair. Those two binds are the times this batch could have bound the shared skin buffers instead:

- Vertex: `CM2Shared::m_vertexBuf`, pool `GxPoolUsage_Static` at `Engine/Source/Model2/M2Shared.cpp:390`. The fixed-function bind is `GxPrimVertexPtr(this->m_vertexBuf, GxVBF_PNT2)` at `M2Shared.cpp:425`. This section occupies `[m_vertexStart, m_vertexStart + m_vertexCount)` in that buffer (`M2Shared.cpp:408-416`).
- Index: `CM2Shared::m_indexBuf`, created at `M2Shared.cpp:340-341`, bound by `PrimIndexPtr` at `M2Shared.cpp:381`. The pool usage flag is `GxPoolUsage_Dynamic`. The fill runs once, while `m_hasData` is clear (`M2Shared.cpp:348`).

`CM2SceneRender.cpp:683` runs only after the one-bone proof:

- `section->m_boneCount == 1` and a non-empty mesh (`CM2SceneRender.cpp:609`)
- cache flag `8` clear, and the single-bone model flag `0x40` clear (`CM2SceneRender.cpp:613`)
- every vertex `m_indices.b[0]` equals `verts[0]` (`CM2SceneRender.cpp:620-624`)
- bone in range (`CM2SceneRender.cpp:626`)
- section index inside the skin (`CM2SceneRender.cpp:632`)
- both locks returned a pointer (`CM2SceneRender.cpp:645`, `CM2SceneRender.cpp:668`)

Returns at `CM2SceneRender.cpp:609`, `:614`, `:622`, `:627`, `:633`, `:648`, and `:671` leave `m2_rigid_static_binds` unchanged.

## Check

With no failed index lock in the sample:

`m2_rigid_stream_bytes == m2_rigid_vertices * 32 + m2_rigid_indices * 2`

`m2_rigid_static_binds` is the number of rigid batches. `m2_rigid_instances` is the sum of `total` across those batches. `m2_rigid_vertices` and `m2_rigid_indices` are the sums of the section sizes, one section per batch.

## Later slices

This file names no counters for:

- the CPU-skin stream in `DrawBatchDoodad` (`CM2SceneRender.cpp:776`, `FillDoodadSkin` at `CM2SceneRender.cpp:598`)
- the shader-on shared bind (`CM2SceneRender.cpp:721`, `CM2SceneRender.cpp:731`)
- `CM2Shared::AllocInstances` / `DestroyBuffers`
