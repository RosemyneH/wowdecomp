# Campaign 1 layout — frame skin cache and instance capacity

Shader-off CPU skin writes a section once per `(model, section, frame)` and later batches bind that buffer again. `CM2Shared::AllocInstances` doubles capacity and calls `DestroyBuffers` only on that doubling step.

Two writer slices, one concern each. Either order. Do not land the rigid static-bind slice from this doc.

| Slice | Lands | Does not touch |
| --- | --- | --- |
| A. Frame cache | `CM2Model::SetVertices`, `SetVerticesMultiSample`, shader-off doodad skin | Shader-on branches, `DrawRigidDoodads`, `CM2Shared::SetVertices` |
| B. Instance capacity | `CM2Shared::AllocInstances` only | Pool create/fill in `SetIndices` / `SetVertices` |

`// OFFSET:` entry points stay. Pixel results, batch order, env-map UVs, and multi-sample sections stay. New comments use the repo TextEmoji form.

## Why the stream buffer is not the cache

`CGxDevice::BufStream` is one `CGxBuf` per target, `m_streamBufs[]` (`Engine/Source/Gx/CGxDevice/CGxDevice.h:147`). Each call rewrites `m_itemSize`, `m_size`, `m_itemCount`, and clears `m_hasData` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1062-1074`). On Vulkan a stream lock replaces the pool allocation with a ring slice and stores that slice on the same `CGxBuf` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1596-1640`). The next lock orphans the previous skin. Holding the pointer across batches is wrong.

Callers that do this today:

- `CM2Model::SetVertices` streams `section->m_vertexCount` PNT verts and skins immediately (`Engine/Source/Model2/M2Model.cpp:2700-2728`).
- `CM2Model::SetVerticesMultiSample` does the same for PNT2 (`Engine/Source/Model2/M2Model.cpp:4519-4547`).
- Shader-off `DrawBatchDoodad` streams `instances * vertexCount` and `FillDoodadSkin` skins every instance into that one buffer (`Engine/Source/Model2/CM2SceneRender.cpp:598-605`, `770-819`).

`SetBatchVertices` already skips a consecutive repeat of the same model, section, and env bit (`Engine/Source/Model2/CM2SceneRender.cpp:186-191`). It cannot see a section that was skinned earlier in the frame, or in another pass. `CM2Scene::Draw` stack-allocates a new `CM2SceneRender` per pass (`Engine/Source/Model2/M2Scene.cpp:2221-2225`), so the cache cannot live on the renderer.

## Frame cache

### Key

`(CM2Model*, M2SkinSection*, frame)`.

- **Model** is `element->m_model`. Bones are `model->m_boneMatrices`, written by animate before draw.
- **Section** is the pointer the draw already uses: `element->m_skinSection`. Non-opt models point at `shared->m_skinSections[]`. Opt models point at `optGeo->m_skinSections[]` (`Engine/Source/Model2/M2Scene.cpp:1803-1812`). The pointer is the identity. Do not key by a bare section index; the two arrays share indexes and do not share pointers.
- **Frame** is `CM2Scene::m_frameStamp` (`Engine/Source/Model2/M2Scene.h:61`). `Animate` increments it before any model runs (`Engine/Source/Model2/M2Scene.cpp:1702`). Hash `(model, section)` only. Compare `m_frame` to the scene stamp; do not put the stamp in the hash.

`texCoord`, multi-sample, and optimized-vs-single select a buffer **inside** the slot. They are not extra key fields. One slot, several fills:

| Request | Stored in | Bytes | Fill |
| --- | --- | --- | --- |
| PNT, one section | `m_pnt[texCoord]` | `32 * section->m_vertexCount` | `s_transformVertices[section->m_boneInfluences](model, section, ptr, texCoord)` |
| PNT, opt concatenation | `m_pntOpt[texCoord]` | `32 *` sum of `vertexCount` over the batch range | The loop in `SetVertices` (`M2Model.cpp:2708-2719`) |
| PNT2, one section | `m_pnt2` | `40 * section->m_vertexCount` | `TransformVerticesNoUVSelect_cpp` |
| PNT2, opt concatenation | `m_pnt2Opt` | `40 *` that same sum | The loop in `SetVerticesMultiSample` (`M2Model.cpp:4527-4538`) |

`32` and `40` are `s_vertexSize[GxVBF_PNT]` and `s_vertexSize[GxVBF_PNT2]` (`Engine/Source/Gx/Gx.cpp:53`).

`texCoord` is `0` or `1`. Draw-batch env is `(m_curBatch->m_shader & 0x4000) != 0`, already passed as the third argument of `SetVertices` (`CM2SceneRender.cpp:179-190`). `TransformVertices_*` writes `vertex->m_texcoord[texCoord]` into the single UV slot (`M2Model.cpp:3156-3157`). A later batch with the other env bit uses the other `m_pnt[]` entry. It must not overwrite the first: that draw may already be recorded.

Doodad CPU skin always passes texCoord `0` (`CM2SceneRender.cpp:603`). It fills `m_pnt[0]` only.

Multi-sample is `m_curBatch->m_flags & 0x40` (`CM2SceneRender.cpp:187-188`). It writes both UV sets (`M2Model.cpp:3094-3095`) and binds `GxVBF_PNT2`. No texCoord index.

Optimized is `m_curElement->m_flags & 4`, set when `model->m_optGeo` is set (`M2Scene.cpp:1864-1866`). `DrawBatchDoodad` does not read that flag; it always skins the one section. The same section pointer can therefore be a concatenation in `DrawBatch` and a single section in the doodad fallback. Those are `m_pntOpt` and `m_pnt`, not one buffer.

### Slot

```cpp
struct M2SkinCacheSlot {
    CM2Model* m_model;
    M2SkinSection* m_section;
    uint32_t m_frame;
    CGxBuf* m_pnt[2];
    CGxBuf* m_pntOpt[2];
    CGxBuf* m_pnt2;
    CGxBuf* m_pnt2Opt;
};
```

Empty slot: `m_model == nullptr`. Tombstone: `m_model == nullptr` and `m_section == (M2SkinSection*)1`, so a probe does not stop on a hole left by `Drop`. A hit is the same model, same section, `m_frame == scene->m_frameStamp`, and the requested `CGxBuf*` non-null. A hit binds that buf and does not lock it.

### Owner

`CM2Scene` owns the cache. Append one pointer after the last existing member `m_passMask` (`M2Scene.h:87`). Do not insert it above that member and do not add fields to `CM2Model` or `CM2Shared`. Those objects are OFFSET-laid-out; the lookup table is a side structure.

```cpp
struct M2SkinFrameGen {
    CGxPool** m_pools;         // grow-only array of dynamic vertex pools
    uint32_t m_poolCount;
    uint32_t m_poolCap;
    uint32_t m_bumpPool;       // index of the pool currently being filled
    uint32_t m_bump;           // byte offset in m_pools[m_bumpPool]
    CGxBuf** m_bufs;           // wrappers in acquire order, grow-only
    uint32_t m_bufCount;
    uint32_t m_bufCap;
    uint32_t m_nextBuf;        // rewind cursor
    M2SkinCacheSlot* m_slots;  // power-of-two open address
    uint32_t m_slotCap;
    uint32_t m_slotLive;
};

struct M2SkinFrameCache {
    uint32_t m_liveFrame;
    M2SkinFrameGen m_gen[2];
};
```

`CM2SceneRender` calls `model->m_scene`. It does not own pools.

`Acquire` is a method on `CM2Scene`:

`CGxBuf* AcquireSkin(CM2Model* model, M2SkinSection* section, int32_t texCoord, int32_t optimized, int32_t multiSample)`

`SetVertices` and `SetVerticesMultiSample` call it, then `GxPrimVertexPtr` and return `1`. `DrawBatchDoodad`'s shader-off loop calls it once per instance.

### Pool and capacity

`GxPoolUsage_Dynamic`, `GxPoolTarget_Vertex`, name `"M2SkinFrame"`. Not `GxPoolUsage_Stream`. A stream pool's lock ignores a byte bump and replaces `m_apiSpecific` (`CGxDeviceVulkan.cpp:1596-1636`).

`BufCreate(pool, stride, vertexCount, byteOffset)` stores the offset in `CGxBuf::m_index` (`Engine/Source/Gx/CGxBuf.cpp:5`, `Engine/Source/Gx/CGxDevice/CGxDevice.cpp:1667-1671`). Vulkan binds that offset (`CGxDeviceVulkan.cpp:1262-1263`). Non-stream lock returns `mapped + m_index` (`CGxDeviceVulkan.cpp:1643-1649`).

Bump rules:

- Round each allocation up to 16 bytes. `TransformVertices_sse` falls back to scalar code when the destination is not 16-aligned (`M2Model.cpp:3184-3186`).
- First pool in a generation is 256 KiB, or the request if that is larger.
- When `m_bump + bytes` does not fit in `m_pools[m_bumpPool]`, append a pool of `max(request, previousSize * 2)`, set `m_bumpPool` to it, and set `m_bump` to 0. Leave the full pool allocated.
- Do not call `PoolSizeSet` on a pool that already has bufs. Vulkan's `PoolSizeSet` releases the allocation (`CGxDeviceVulkan.cpp:1576-1578`) and would drop every earlier section in that pool.
- Never shrink `m_pools` or `m_bufs`.

Wrapper reuse, on a generation reset: `m_bumpPool = 0`, `m_bump = 0`, `m_nextBuf = 0`. Do not `BufDestroy`. The next acquire takes `m_bufs[m_nextBuf]` when `m_nextBuf < m_bufCount` and writes `m_index`, `m_itemSize`, `m_itemCount`, and `m_size` for the new bump. Section sizes change with the camera, so the old offset is stale. If that wrapper's `m_pool` is not the pool the bump lands in, `UnlinkBuf` (`Engine/Source/Gx/CGxPool.h:29`), point `m_pool` at the bump pool, and link it onto that pool's `m_bufList`. Otherwise `BufCreate` and append. Then lock and fill. `m_nextBuf` advances by one.

Lookup table: power of two, start at 256 slots, double at load above one half, never shrink. Reset clears slots to empty. It does not free the array.

### Lifetime

Two generations, index `m_frameStamp & 1`.

1. `Animate` bumps `m_frameStamp` (`M2Scene.cpp:1702`), then writes bones. `AnimateMTSimple` and `AnimateSM` return without writing when `model->m_frameStamp == scene->m_frameStamp` (`M2Model.cpp:4700-4701`, `6203-6204`). Draw runs after `Animate` returns. There is no second bone write in the frame, so the cache does not carry a bone serial.
2. The first `AcquireSkin` of a new stamp sets `m_liveFrame` and resets **that** generation's bump, `m_nextBuf`, and hash. The other generation stays bound to the frame the GPU may still be reading. Do not `PoolDestroy` it and do not `vkDeviceWaitIdle`.
3. A miss locks once, runs the existing transform, `BufUnlock`, sets `m_hasData = 1`. A hit only `GxPrimVertexPtr`s.
4. `~CM2Scene` releases every model first (`M2Scene.cpp:1644-1648`). Model teardown `Drop`s while the cache is still alive. Then `PoolDestroy` both generations and free the cache.

`Drop(CM2Model*)` tombstones every live slot in the **current** generation whose `m_model` matches. Call it from `~CM2Model` (`M2Model.cpp:6592`) and from `UnoptimizeVisibleGeometry` before `SMemFree(m_optGeo)` (`M2Model.cpp:1029-1049`). Opt-section pointers die with that free. The model heap recycles addresses in the same frame; a tombstone stops the new model from binding the old skin. GPU bufs stay with the generation until its reset. `Drop` does not `BufDestroy`.

Hit-test skin (`TransformHitTestVertices*`) and `GetRegionBounds` stay on their own scratch. They are not draws and they fold in `m_viewInv` (`M2Model.cpp:2767`). Do not route them through `AcquireSkin`.

### Invalidation

| Event | What happens |
| --- | --- |
| `m_frameStamp` changes | Next acquire resets that parity's hash and bump. Old slots miss because the hash is empty. |
| Same frame, same section, other `texCoord` / format / opt flag | Other pointer in the same slot. Null means miss. Non-null means hit. Never replace a non-null pointer until the generation resets. |
| Model destroy, or `UnoptimizeVisibleGeometry` | `Drop`. Tombstone. Buf stays until generation reset. |
| `DestroyBuffers` on the shared mesh | Does not free cache pools. Cache pools are not `m_vertexPool` / `m_indexPool`. |
| Bone animate inside the frame | Does not happen after the stamp matches. No extra invalidation. |

### Shader-off call path

`SetBatchVertices` keeps its consecutive-batch predicate (`CM2SceneRender.cpp:186-191`). The cache is what covers a non-adjacent repeat and the next pass.

`CM2Model::SetVertices`: if `CShaderEffect::s_enableShaders`, keep today's `BufStream` body unchanged. The shader-on draw path does not call this function; the guard is so it stays that way. Otherwise `AcquireSkin(..., texCoord, optimized, 0)`, bind `GxVBF_PNT`, return `0` when acquire or lock fails. Same failure shape as `BufLock` returning null (`M2Model.cpp:2704-2706`).

`SetVerticesMultiSample`: same, with `multiSample = 1`, bind `GxVBF_PNT2`. No shader-on caller.

Shader-off doodad loop (`CM2SceneRender.cpp:770-819`):

- Do not `BufStream` the packed `instances * vertexCount` buffer.
- For each instance in the existing `indices[done + n]` order, `AcquireSkin(model, section, 0, 0, 0)`.
- Main thread probes, inserts, and locks misses **before** any worker runs. Workers may only call `s_transformVertices` into pointers already reserved. The bump and the hash stay on the main thread. Keep the `instances >= 8` split (`CM2SceneRender.cpp:794-798`) only as that worker fill.
- Then, still in instance order, `GxPrimVertexPtr(buf, GxVBF_PNT)` and draw **one** copy: `m_start = section->m_indexStart`, `m_count = section->m_indexCount`, `m_minIndex = 0`, `m_maxIndex = vertexCount - 1`.
- `SetIndices` has already biased those indices by `-vertexStart` (`M2Shared.cpp:361-365`), which matches a buffer that contains only that section. Do not rebuild indices.
- Lighting, material, textures, and the normalize-normal scan stay where they are. Sort order inside the doodad group stays `indices[]` order.

`DrawBatch`'s non-shared shader-off draw (`CM2SceneRender.cpp:489-492`) stays: index range from the section, verts from whatever `SetVertices` bound.

### Shader-on stays

Do not edit these. They already bind the shared mesh and upload bones as constants.

- `SetBatchVertices` shader branch: `m_curShared->SetVertices(0)` and return (`CM2SceneRender.cpp:171-176`).
- `DrawBatch` constant upload and `Draw` (`CM2SceneRender.cpp:431-474`).
- `DrawBatchDoodad` shader branch: `SetVertices(0)`, per-instance constants, one draw of `instances * indexCount` (`CM2SceneRender.cpp:730-768`).
- `CM2Shared::SetVertices` shader branch, which copies bind-pose verts and bone indices once into the static pool (`M2Shared.cpp:430-458`).
- `RenderModelBatchesForProjectedTexture` and `RenderBatchShadowMap`, which call `shared->SetVertices(0)` (`M2Model.cpp:2889`, `2943-2945`).

Single-bone shared vertices (`m_bones.m_count == 1` and cache flag `0x40`) also stay on `CM2Shared::SetVertices` plus a world matrix (`CM2SceneRender.cpp:182-185`, `475-488`). That is not a CPU skin.

`DrawRigidDoodads` (`CM2SceneRender.cpp:608-692`) is the other slice. It still stream-locks the mesh. This cache does not replace it.

## AllocInstances

### Current step

`m_instances` starts at `1` (`M2Shared.cpp:1150`) and is `0` only in the constructor before the skin profile exists (`M2Shared.cpp:1525`). `m_maxInstances` is `min(0x10000 / indexCount, boneCountMax / sectionBoneCount)`, at least `1` (`M2Shared.cpp:1136-1147`). The `uint16` index cursor in `SetIndices` is why the cap exists (`M2Shared.cpp:371-372`).

```771:782:Engine/Source/Model2/M2Shared.cpp
uint32_t CM2Shared::AllocInstances(uint32_t count) {
    if (this->m_instances < count && this->m_instances < this->m_maxInstances) {
        this->m_instances = (count + 15) & ~15u;
        if (this->m_instances > this->m_maxInstances) {
            this->m_instances = this->m_maxInstances;
        }
        this->DestroyBuffers();
    }
    return this->m_instances;
}
```

Every time `count` crosses the current capacity, capacity rounds up by 16 and `DestroyBuffers` drops `m_indexPool`, `m_indexBuf`, `m_vertexPool`, and `m_vertexBuf` (`M2Shared.cpp:602-631`). The next `SetIndices` / `SetVertices` allocates them at `m_instances *` the mesh size (`M2Shared.cpp:340-341`, `390-391`). A dense climb `16, 32, 48, 64, …` rebuilds the shared pools on each step. Callers then clamp the return value down to the live total (`CM2SceneRender.cpp:711-715`, `M2Model.cpp:2937-2941`). The spare slots are not drawn.

`m_instances` **is** the allocated slot count. Do not add a second capacity field. `SetIndices` and both `CM2Shared::SetVertices` branches already replicate `m_instances` copies. Leave that formula alone.

### Growth rule

`m_instances` is a high-water mark. It never decreases.

```text
if count <= m_instances or m_instances >= m_maxInstances:
    return m_instances          // no DestroyBuffers

grown = m_instances if m_instances else 1
while grown < count and grown < m_maxInstances:
    if grown > m_maxInstances / 2:
        grown = m_maxInstances
        break
    grown *= 2
if grown > m_maxInstances:
    grown = m_maxInstances
if grown <= m_instances:
    return m_instances          // already at the cap

m_instances = grown
DestroyBuffers()                // once for this call
return m_instances
```

- A request that fits returns the existing capacity. Steady camera: no `DestroyBuffers`.
- A request that skips several powers of two still doubles inside this one call and destroys **once**, then the next `SetIndices` / `SetVertices` builds pools at the new `m_instances`.
- Drop the `(count + 15) & ~15` mask. Doubling replaces it. From `1`, the sequence is `2, 4, 8, 16, 32, …` until the cap. The cap may be a non-power-of-two; clamping to `m_maxInstances` is the only non-power-of-two value.
- Do not let `grown` pass `m_maxInstances`. Past that, `section->m_indexStart` truncates to `uint16` (`M2Shared.cpp:371`).
- Zero capacity doubles from `1`, not from `0`.
- Callers keep their `instances > total` clamp. A doubled return larger than `total` is the spare tail. Shader-on draws still upload constants and index counts for the clamped total (`CM2SceneRender.cpp:733-768`). The extra replicated verts sit past `m_maxIndex` and are not drawn.

`DestroyBuffers` on a doubling step is required. `SetIndices` and `SetVertices` allocate only when the pool pointer is null (`M2Shared.cpp:339`, `389`). Leaving the old pool in place would keep a buffer sized for the previous `m_instances`. "In place" means the existing pools keep serving every count between doubles. They are not rebuilt on each increment of 16.

### Other DestroyBuffers callers

Leave them. They are not the per-step grow.

| Site | When |
| --- | --- |
| `CM2Shared::~CM2Shared` | `M2Shared.cpp:1882` |
| `CM2Cache::UpdateShared` | Shared unused for 10s (`M2Cache.cpp:409-413`). A drawn shared refreshes `m_lastUsedTime` from `LinkToSharedUpdateList` (`CM2SceneRender.cpp:1115`). A steady camera does not take this path. |

After warmup, `AllocInstances` does not call `DestroyBuffers` while every doodad and shadow batch's `count` stays at or under the high-water mark. Jitter under that mark does not shrink and does not rebuild.

## Out of scope

- Rigid one-bone bind (`DrawRigidDoodads`). It still runs, and it still calls `AllocInstances` first (`CM2SceneRender.cpp:711-718`), so slice B covers that call. Slice A does not rewrite its stream locks.
- Hit-test and projected-region vertex walks.
- `VkDrawUniforms`, sort comparators, material flags, geoset visibility, `m_maxInstances` math.
- Moving these pools to device-local memory. That is Campaign 6. These pools stay host-visible dynamic.
