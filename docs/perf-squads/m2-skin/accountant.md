# Campaign 1 — Accountant

Static products from the doodad draw path. Campaign 0 has no log line yet, so every figure below is `file:line` arithmetic, not a sampled frame.

Vulkan is on the fixed-function path (`CShaderEffect::s_enableShaders == 0`). `CM2SceneRender::DrawBatchDoodad` is the batch (`Engine/Source/Model2/CM2SceneRender.cpp:697`).

## Bytes skinned per doodad batch

One CPU-skin draw (one `BufStream`, one `Draw`) writes:

**`instances × section.m_vertexCount × 32`**

| Factor | Value | Where |
| --- | --- | --- |
| `instances` | Draw width after the capacity clamp and the tail shrink | `CM2SceneRender.cpp:711-715`, then `771-774` |
| `section.m_vertexCount` | `uint16` on `M2SkinSection` | `M2Data.h:304`, used at `CM2SceneRender.cpp:776` |
| `32` | Stream item stride. `GxVBF_PNT` position + normal + uv0 | `Buffer.cpp:25-27` |

`BufStream` is called as `BufStream(GxPoolTarget_Vertex, 32, instances * m_vertexCount)` (`CM2SceneRender.cpp:776`). `CGxDevice::BufStream` stores `m_size = itemCount × itemSize` (`CGxDevice.cpp:1062-1072`), so the lock size is the same product:

```
m_size = (instances × section.m_vertexCount) × 32
```

`FillDoodadSkin` (`CM2SceneRender.cpp:598-604`) runs once per instance. `TransformVertices_*` walks `section->m_vertexCount` vertices and steps `out += 8` floats (`M2Model.cpp:3110` and `3157`; one-bone path `3168` and `3178`; AVX2 pair `out += 16` at `M2SkinAvx2.cpp:129-130`). **8 × 4 = 32** bytes per vertex. The loop fills the lock. No gap.

`DoodadSkin.stride` is the instance pitch, already scaled:

```
skin.stride = 32 × section.m_vertexCount    // CM2SceneRender.cpp:791
bytes        = instances × skin.stride       // same product
```

The third factor in the headline formula is the **32-byte stream stride**, not `skin.stride`. Multiplying `instances × vertexCount × skin.stride` counts `vertexCount` twice.

### How `instances` is chosen

```
total     = m_curElement->m_count                         // :710
capacity  = m_curShared->AllocInstances(total)            // :711, returns m_instances
instances = min(capacity, total)                          // :713-715
```

The `for` at `:771` slices `[0, total)`. On the last slice, `instances` becomes `total - done` (`:772-774`). The slices partition `total`, so one `DrawBatchDoodad` call that stays on this path writes:

**`total × section.m_vertexCount × 32`**

when every `BufLock` returns a pointer (`:779` returns early on a miss and drops the rest of the call).

Worked slice. `total = 40`, `m_maxInstances = 16`, so `AllocInstances` returns 16 (see the cap below). Three draws: 16, 16, 8.

```
(16 + 16 + 8) × V × 32 = 40 × V × 32
```

`V = 100` is **128,000** bytes for the call, and **51,200 / 51,200 / 25,600** per draw.

`ParallelFor` at `:794` (`instances >= 8`) covers the same range. It does not change the product.

### Where the lock sits

`IBufLock` passes `buf->m_size` into `VkContext::StreamLock(itemSize, bytes, …)` (`CGxDeviceVulkan.cpp:1605`, `VkContext.cpp:1086-1089`). `itemSize` is the alignment. `BumpChunk` aligns the cursor, then advances by `bytes` (`VkContext.cpp:1041-1044`). The skinned payload is the product. Padding in front of it is at most **31** bytes, and only when the shared vertex/index ring cursor is not already 32-aligned.

`StreamRingSize` is `64 << 20` (**67,108,864** bytes, `VkContext.h:135`). A single lock with `bytes >` that ring takes `CreateBuffer(bytes)` for exactly the product (`VkContext.cpp:1023-1028`).

The legacy stream pool starts at `s_streamVertexPoolSize = 0x2C0000` (**2,883,584** bytes, `CGxDevice.cpp:62`). `BufStream` calls `PoolSizeSet` when `pool->m_size < itemCount × itemSize` (`CGxDevice.cpp:1066-1067`). On Vulkan that releases the pool and stores the new size (`CGxDeviceVulkan.cpp:1576-1578`). Crossing **2,883,584** skinned bytes in one lock is a stream-pool release. Later locks at or under the new `m_size` do not release it again. This is the device stream pool, separate from `CM2Shared`’s vertex and index pools.

### Shader-on branch of the same function

`s_enableShaders` binds shared vertices and writes bone constants (`CM2SceneRender.cpp:730-768`). CPU skin bytes on that branch are **0**. Constant traffic is `instances × section.m_boneCount × 12` floats = **`instances × boneCount × 48`** bytes (`:744-763`). Campaign 1’s fixed-function budget does not use this branch.

### Rigid return, same call

`DrawRigidDoodads` (`CM2SceneRender.cpp:608`) returns 1 when the guards at `:609-634` pass: `m_boneCount == 1`, every vertex names that bone, the bone index is in range, and the section sits inside the skin profile. Cache flag `8`, and flag `0x40` on a one-bone model, return 0 at `:613-615` and are billed as the CPU-skin product above. `DrawBatchDoodad` returns at `:717-718` on success, before `FillDoodadSkin`.

That path still stream-locks, with a different product. `BufStream(Vertex, 32, section.m_vertexCount)` (`:644`) and a loop of 8 floats (`:651-660`):

**vertex bytes = `section.m_vertexCount × 32`**

Indices: `BufStream(Index, 2, section.m_indexCount)` (`:667`):

**index bytes = `section.m_indexCount × 2`**

`total` does not multiply either lock. `DrawInstanced(&batch, worlds, total)` (`:691`) carries the copies. The CPU matrix array is one `C44Matrix` per element (`:640-641`). `C44Matrix` ends at `0x40` (`C44Matrix.h:21-36`), so **`total × 64`** bytes. Vulkan then value-inits `total` `GpuDrawItem`s (448 bytes each, `VkContext.h:88-97`) and copies `world[16]` (**64** bytes) into each (`CGxDeviceVulkan.cpp:1491-1493`).

A rigid batch of `total = 40`, `V = 100`, `I = 300`:

| Record | Bytes |
| --- | --- |
| Vertex lock | `100 × 32 = 3,200` |
| Index lock | `300 × 2 = 600` |
| `worlds` | `40 × 64 = 2,560` |
| `GpuDrawItem` records | `40 × 448 = 17,920` (the 2,560 sit inside this) |

The same shape on the CPU-skin path would write `40 × 100 × 32 = 128,000` vertex bytes.

## When `AllocInstances` destroys and rebuilds

`CM2Shared::AllocInstances` (`M2Shared.cpp:771-782`) only destroys. The replacement pools are created later, on the next `SetIndices` / `SetVertices` that sees a null pool.

### Grow predicate

```
if (m_instances < count && m_instances < m_maxInstances) {
    m_instances = (count + 15) & ~15u;          // :773
    if (m_instances > m_maxInstances)
        m_instances = m_maxInstances;           // :775-776
    DestroyBuffers();                           // :779
}
return m_instances;                             // :782
```

`(count + 15) & ~15u` rounds **up** to a multiple of 16. The clamp runs before the destroy, so a step that lands on `m_maxInstances` still destroys once.

After `InitializeSkinProfile`, `m_instances` is **1** (`M2Shared.cpp:1150`). The ceiling is:

```
m_maxInstances = m_indices.m_count ? 0x10000 / m_indices.m_count : 1    // :1136
```

then the min of `m_boneCountMax / section.m_boneCount` over every skin section (`:1138-1144`), and at least 1 (`:1146-1147`). Because `N × indexCount ≤ 65536`, a rebuilt index pool is at most **`2 × 65536 = 131,072`** bytes.

| `m_instances` before | `count` | `m_maxInstances` | Predicate | `m_instances` after | `DestroyBuffers` |
| --- | --- | --- | --- | --- | --- |
| 1 | 1 | ≥ 1 | `1 < 1` is false | 1 | no |
| 1 | 2 | 64 | true | `(2 + 15) & ~15 = 16` | yes |
| 16 | 16 | 64 | `16 < 16` is false | 16 | no |
| 16 | 17 | 64 | true | `(17 + 15) & ~15 = 32` | yes |
| 16 | 17 | 24 | true | `min(32, 24) = 24` | yes |
| 24 | 40 | 24 | `24 < 24` is false | 24 | no |

Each true row is one destroy of both shared pools. A climb `1 → 16 → 32 → …` up to the cap is one destroy per step. Once `m_instances >= count` or `m_instances == m_maxInstances`, further batches with that count do not enter the `if`. A steady camera that stays under the peak does not destroy from this function.

The returned capacity is what the pools will be sized to. The skin product above uses `min(capacity, remaining)`, which is smaller whenever the round-up exceeded `total`. First multi-instance batch (`count = 2`) skins **2** copies and, on the later rebuild, allocates for **16**.

### What `DestroyBuffers` drops

`CM2Shared::DestroyBuffers` (`M2Shared.cpp:602-631`):

1. `BufDestroy(m_indexBuf)` when it is not the stream sentinel (`:603-611`), then `m_indexBuf = nullptr`.
2. `PoolDestroy(m_indexPool)` (`:613-616`). Vulkan `PoolDestroy` calls `IReleasePool` first (`CGxDeviceVulkan.cpp:1581-1586`).
3. Same pair for `m_vertexBuf` / `m_vertexPool` (`:618-631`).

No replacement allocation happens here.

### What the rebuild allocates

`SetIndices` when `m_indexPool` is null (`M2Shared.cpp:338-341`):

```
index pool bytes = sizeof(uint16_t) × m_instances × m_skinData->m_indices.m_count
                 = 2 × N × indexCount
```

`GxPoolUsage_Dynamic`. The following lock fills every section × `N` indices (`:363-373`). That fill runs when `m_hasData` is clear or the buf is invalid (`:348`), so the first use after a destroy rewrites the whole index pool, not one section.

`SetVertices` when `m_vertexPool` is null (`M2Shared.cpp:388-391`):

```
vertex pool bytes = sizeof(M2Vertex) × N × m_skinData->m_vertices.m_count
```

`M2Vertex` is position, weights, indices, normal, two texcoords, ending at offset `0x30` (`M2Data.h:119-126`). **`sizeof(M2Vertex) = 48`.**

```
vertex pool bytes = 48 × N × skin.m_vertices.m_count
```

`GxPoolUsage_Static`. `skin.m_vertices.m_count` is the profile vertex array. It is not `section.m_vertexCount`.

Fill of that buffer, once, on the rebuild:

| `s_enableShaders` | Bytes written into the new vertex buf | Cite |
| --- | --- | --- |
| on | `48 × N × skin.m_vertices.m_count` (one `memcpy` of `M2Vertex` per instance) | `M2Shared.cpp:437-449` |
| off | `40 ×` vertices touched, one pass, not × `N`. `M2VertexPNT2` ends at `0x28` (`M2Data.h:112-117`) | `M2Shared.cpp:406-417` |

The shader-off fill still sits in a buffer **allocated** at `48 × N × vertexCount`.

Worked rebuild at the first step (`N` goes from 1 to 16), profile `indexCount = 1,000`, `vertexCount = 400`:

| Pool | Before (`N = 1`) | After (`N = 16`) |
| --- | --- | --- |
| Index | `2 × 1 × 1000 = 2,000` | `2 × 16 × 1000 = 32,000` |
| Vertex | `48 × 1 × 400 = 19,200` | `48 × 16 × 400 = 307,200` |

Index cap check: `0x10000 / 1000 = 65`, so `N` stops at 65 and the index pool stops at `2 × 65 × 1000 = 130,000`.

### Which caller rebuilds after the destroy

`AllocInstances` has two callers.

`DrawBatchDoodad` (`CM2SceneRender.cpp:711`) destroys first, then branches:

| Branch | Rebuild in this call |
| --- | --- |
| Shaders off and `DrawRigidDoodads` returns 1 | None. Return at `:718` is before `SetIndices`. Both pools stay null. |
| Shaders off and rigid returns 0 | `SetIndices` at `:721` rebuilds the index pool (`2 × N × indexCount`). `SetVertices` is not called. The vertex pool stays null. Skin bytes go to the stream lock, not `m_vertexPool`. |
| Shaders on | `SetIndices` at `:721` and `SetVertices` at `:731`. Both pools come back at `N`. |

`CM2Model::RenderBatchShadowMap` (`M2Model.cpp:2937`) is the other caller. When `total > 1` it calls `SetIndices` and `SetVertices` immediately (`:2943-2945`), so a grow destroys and both pools return in that call. `total == 1` does not call them. `AllocInstances(1)` also fails the predicate while `m_instances` is already ≥ 1.

A dense rigid-only doodad area therefore destroys on the way up to the peak (`1 → 16 → 32 → …`) and does not rebuild inside `DrawBatchDoodad`. The next batch that reaches `SetIndices` or `SetVertices` pays the table above. A mixed frame that misses the rigid test rebuilds the index pool on that batch and leaves the vertex pool null until a shader or shadow call.

### Other `DestroyBuffers` sites

These are not the capacity climb:

| Site | When |
| --- | --- |
| `CM2Cache::UpdateShared` | Shared unused for `10000` ms (`M2Cache.cpp:409-413`) |
| `CM2Shared` destructor | Release (`M2Shared.cpp:1882`) |

## Budget check against Campaign 1

| Budget line | Current bytes |
| --- | --- |
| Rigid batch moves matrix bytes only | Vertex lock `V × 32` and index lock `I × 2` still run, plus `total` draw items of 448 bytes (`world` is 64 of those). |
| Skinned section skins once per model per frame | Each CPU-skin draw rewrites `instances × V × 32`. The next `BufStream` sets `m_hasData = 0` (`CGxDevice.cpp:1073`) and the following lock replaces it. |
| `DestroyBuffers` stays quiet on a steady camera | True for `AllocInstances` once `m_instances` has stopped growing. The climb to that peak destroys once per step of 16 (or the final clamp to `m_maxInstances`). |
