# Campaign 6 — Accountant

Structural bytes and `vkAllocateMemory` counts for Vulkan buffer lifetime. There is no Campaign 0 log in tree, so this is not a captured outdoor second. Draw count per frame is unknown. What follows is the reservation each call site makes, and what that reservation becomes if host-visible buffers share a few large allocations the way images already share `VkHeap`.

The 2.75 MiB stream-wrap allocation is closed. It is not in the budget below.

## Verdict

Rings are already large. Pools are not.

A warm device that has streamed and uploaded on both frames holds **242 MiB** of host-visible ring memory in **10** `vkAllocateMemory` calls, plus one 32-byte dummy. Those ten calls do not grow with the scene. Collapsing them into one host-visible allocation per in-flight frame removes 8 driver allocations and **no bytes**.

The count that moves while flying is one driver allocation per locked `CGxPool`. Terrain and grass are one pair of pools for the whole view. Each M2 skin, each WMO group, and each liquid buffer is its own pair. `AcquireBuffer` only reuses a retired buffer when size and usage match exactly, so a newly loaded mesh misses.

Put those pools in the same kind of block `VkHeap` already uses and the closed outdoor set below goes from **21** host-visible driver allocations to **5** blocks of 64 MiB (53 MiB of tail left in the last blocks if nothing else shares them). Further M2 indices, WMO groups, and liquids add a driver allocation only when no free span fits. Static vertex pools should leave this host pile entirely.

`VkHeap` itself is images only, device-local, 64 MiB, and it does not give blocks back. Its `std::map` freelist is a host CPU cost: one heap node per free span and a linear walk of every span on every image alloc. It is not a GPU-byte cost next to a 64 MiB block.

## What one buffer costs today

`VkContext::CreateBuffer` is the only buffer path. Every call:

1. `vkCreateBuffer` of the requested size
2. `vkAllocateMemory` of `requirements.size` (at least the requested size)
3. binds at offset 0 and `vkMapMemory`s the whole allocation

The memory type must have both `HOST_VISIBLE` and `HOST_COHERENT`. Device-local is not requested, so this pile is not the image heap.

`VkContext.cpp:982-1015`, `VkContext.cpp:960-967`.

`PoolCreate` does not allocate device memory. The driver allocation happens on the first lock, in `IPoolBuffer` → `AcquireBuffer` → `CreateBuffer` (`CGxDeviceVulkan.cpp:1542-1553`, `VkContext.cpp:970-979`).

Images never take this path. `CreateImage` asks for `DEVICE_LOCAL` and suballocates from `VkHeap` (`VkContext.cpp:1101-1117`).

| Call | When it adds a driver allocation |
| --- | --- |
| Frame rings | Device setup. One allocation each. `VkContext.cpp:290-300` |
| Stream / staging chunk | First use, and again when the cursor passes the chunks already kept. `VkContext.cpp:1031-1037` |
| `CGxPool` static or dynamic | First lock, unless the spare list has the same size and `VERTEX \| INDEX` usage |
| Lock larger than the ring | That lock alone. `VkContext.cpp:1023-1028` |
| `DeviceReadPixels` | That call, freed before return. `CGxDeviceVulkan.cpp:2209-2254` |
| Dummy vertices | Once, 32 bytes. `CGxDeviceVulkan.cpp:343` |

`out.size` stays the requested size. The driver object is `requirements.size`, so any driver round-up is paid once per buffer instead of once per block.

## Host rings, already large

`FramesInFlight` is 2. Both frames stay mapped for the life of the device. `PrepareFrame` zeros cursors after the fence; it does not free the buffers (`VkContext.cpp:717-723`). Chunks are destroyed only in `DestroyDevice` (`VkContext.cpp:651-655`).

| Ring | One frame | Both frames | Allocations |
| --- | --- | --- | --- |
| Uniforms | 32 MiB | 64 MiB | 2 |
| Draw items | 8 MiB | 16 MiB | 2 |
| Indirect | 1 MiB | 2 MiB | 2 |
| Stream, first chunk | 64 MiB | 128 MiB | 2 |
| Staging, first chunk | 16 MiB | 32 MiB | 2 |
| Dummy vertices | 32 B | 32 B | 1 |

Constants: `VkContext.h:131-137`, dummy at `CGxDeviceVulkan.cpp:343`.

Warm total, one stream chunk and one staging chunk per frame: **242 MiB + 32 bytes, 11 allocations**. The 32-byte dummy is the eleventh.

A frame that writes one byte past 64 MiB of stream data keeps a second 64 MiB chunk on that frame forever. Staging does the same at 16 MiB. The resident size is the high-water mark, not the bytes used this frame.

Cursors inside a chunk are already suballocated (`borrowed = 1`, `VkContext.cpp:1043-1048`). The ten ring allocations are ten `VkDeviceMemory` objects, not ten locks.

### Ring ceilings, not outdoor draw counts

| Ring | Record | Records that fit | Bytes stranded |
| --- | --- | --- | --- |
| Uniform | 768 | 43,690 if `minUniformBufferOffsetAlignment` divides 768 | 512 |
| Items | 448 | 18,724 | 256 |
| Indirect | 20 | 52,428 | 16 |

`32 MiB / 768 = 43690` remainder 512. `8 MiB / 448 = 18724` remainder 256. `1 MiB / 20 = 52428` remainder 16. Item and indirect cursors are not re-aligned (`VkContext.cpp:1056-1083`). The uniform cursor is (`CGxDeviceVulkan.cpp:1378-1386`).

768 is `256 × 3`, so a power-of-two alignment of 256 or less inserts no gap. A larger alignment does, and the 43,690 figure drops. The overflow guard returns without drawing rather than reuse another draw’s matrices (`CGxDeviceVulkan.cpp:1381-1384`). Do not shrink the 32 MiB ring to the stranded 512 bytes.

Campaign 2 may cut how full the uniform cursor gets. The allocation stays 32 MiB either way.

## Per-pool host memory

Usage values: `GxPoolUsage_Static = 0`, `Dynamic = 1`, `Stream = 2` (`Engine/Source/Gx/Types.h:253-256`).

Stream pools are not in this table. `IBufLock` does not call `IPoolBuffer` for them.

Vertex strides from `s_vertexSize` (`Engine/Source/Gx/Gx.cpp:53`): PN 24, PNC 28, PNCT 36, PNT2 40, PBNT2 48, PNC2T2 48.

### Closed outdoor set

These exist once for the view, not once per chunk.

Terrain (`MapRenderChunk.cpp:821-835`). `radius = 1 - (int)(farClip * -0.03f)`, capped at 160. `count = (2 * radius * radius + 1) & ~1`. Vertex bytes `145 * count * stride`, index bytes `768 * count * 2`. Default format is PN (`MapRenderChunk.cpp:361`). Two dynamic pools.

| Far clip (yards) | Radius | Slots | Vertex (PN) | Index | Both pools |
| --- | --- | --- | --- | --- | --- |
| 200 | 7 | 98 | 341,040 | 150,528 | 0.47 MiB |
| 277 | 9 | 162 | 563,760 | 248,832 | 0.77 MiB |
| 300 | 10 | 200 | 696,000 | 307,200 | 0.96 MiB |
| 350 | 11 | 242 | 842,160 | 371,712 | 1.16 MiB |
| 400 | 13 | 338 | 1,176,240 | 519,168 | 1.62 MiB |
| 450 | 14 | 392 | 1,364,160 | 602,112 | 1.88 MiB |
| 550 | 17 | 578 | 2,011,440 | 887,808 | 2.76 MiB |
| 777 | 24 | 1,152 | 4,008,960 | 1,769,472 | 5.51 MiB |
| cap 160 | 160 | 51,200 | 178,176,000 | 78,643,200 | 244.92 MiB |

The cap is why a 10,000-yard clip does not build the uncapped radius-301 pool. That pool would be 181,202 slots, about 601 MiB of PN vertices plus 265 MiB of indices. The comment at `MapRenderChunk.cpp:823` calls that a gigabyte. PNC multiplies only the vertex column by `28/24`.

Grass (`DetailDoodad.cpp:550-570`). `vertexItems = min(density << 6, 4096)`. Vertex pool `512 * 36 * vertexItems`, index pool `512 * 2 * vertexItems`. Two dynamic pools. 512 is the slot count, not a byte size.

| Density | Items / slot | Vertex pool | Index pool |
| --- | --- | --- | --- |
| 8 | 512 | 9 MiB | 0.50 MiB |
| 12 | 768 | 13.5 MiB | 0.75 MiB |
| 16 | 1,024 | 18 MiB | 1 MiB |
| 24 | 1,536 | 27 MiB | 1.5 MiB |

Small dynamic or static pools that come up with the world, one driver allocation each once locked:

| Pool | Bytes | Usage |
| --- | --- | --- |
| `CMap::lowDetailIndexPool` | 6,144 | Dynamic. `Map.cpp:764` |
| `CMap::s_vbbList` | 209,280 (`16 * 0x221 * 24`) | Dynamic. `Map.cpp:766`, `FVBBList.cpp:21-23` |
| Font indices | 6,144 | Static. `GxuFontMiscClasses.cpp:222` |
| Particle indices | 262,128 (`2 * 0x1FFF8`) | Static. `ParticleSystem2.cpp:393` |
| `PassFogCombine` | 34,848 + 12,288 | Static. `WoW/Source/Game/GameClient/FFXEffects.cpp:214-215` |

### One allocation per loaded object

These are the flying spikes. Each is a separate host-visible `VkDeviceMemory` of that size.

**M2 skin** (`M2Shared.cpp:338-391`). Two pools:

- Vertex, **static**: `48 * instances * vertexCount`. Field layout of `M2Vertex` ends at `0x30` (`M2Data.h:119-126`).
- Index, **dynamic**: `2 * instances * indexCount`.

`m_instances` starts at 1 (`M2Shared.cpp:1150`). `AllocInstances` rounds a growth up to a multiple of 16, then `DestroyBuffers` (`M2Shared.cpp:771-779`). The next lock is a new driver allocation of the rounded size. A step from 1 to 2 instances reserves 16 slots when `m_maxInstances` allows it. The spare list misses, because the size changed.

The fixed-function path (`s_enableShaders == 0`) binds `GxVBF_PNT2` (40 bytes) and writes `M2VertexPNT2` into that 48-byte reservation (`M2Shared.cpp:398-425`). Eight bytes per vertex of the allocation are unused on that path. The shader-on path binds `GxVBF_PBNT2` (48) and fills the reservation (`M2Shared.cpp:430-457`). The pool stays 48. Campaign 6 does not shrink it.

Optional per-model index pool: `2 * indexCount`, static (`M2Model.cpp:4290`).

**WMO group.** `s_worldPoolUsage` is 1, so dynamic (`World.cpp:1717`, `MapObjGroup.cpp:1129-1135`). `m_singlePool` starts at 0, so groups do not share a pool. Each block is `itemSize * (itemCount + 1) + 4096` (`MapObjGroup.cpp:1431-1434`).

- Vertices: 36 bytes (`CGxVertexPNCT`) or 48 (`CGxVertexPNC2T2` when `m_unkLoadedFlag & 0x8`). `MapObjGroup.cpp:1446-1447`.
- Indices: 2 bytes times index count, plus the same `+ 1` vertex-slot and `+ 4096`. `MapObjGroup.cpp:1455-1456`.
- A second vertex pool when shaders are off, the parent flag `0x2` is set, and a transparency batch exists (`MapObjGroup.cpp:1450-1452`).
- Liquid blocks on the same lists (`MapObjGroup.cpp:1484-1489`).

The `+ 4096` is pool capacity, not driver padding. A suballocator must reserve it too. On a small group it is most of the allocation, and it is still its own `vkAllocateMemory` today.

**Liquid chunk.** Two dynamic pools, `vertexSize * vertexCount` and `2 * indexCount` (`MapChunkLiquid.cpp:376-377`). No `+ 4096`.

Weather is another dynamic vertex pool, `s_vertexSize * 6144 * s_verticesPerParticle` (`Rain.h:102`). Size depends on the packet type.

## 21 allocations versus a few blocks

Closed set used for the comparison: both frames’ rings (one stream chunk and one staging chunk each), the 32-byte dummy, terrain at the 777-yard row (PN), grass at density 16, and the five small pools. No M2, no WMO, no liquid. That is the outdoor shell before props.

| Piece | Bytes | Driver allocs today |
| --- | --- | --- |
| Rings + dummy | 253,755,424 | 11 |
| Terrain, 777 yd, PN | 5,778,432 | 2 |
| Grass, density 16 | 19,922,944 | 2 |
| Small pools (fog-combine is two) | 530,832 | 6 |
| **Total** | **279,987,632 (267.0 MiB)** | **21** |

`5 * 64 MiB = 335,544,320`. Packed into 64 MiB blocks with no alignment gaps, this set is **5** `vkAllocateMemory` calls and **55,556,688** bytes (53.0 MiB) of unused tail. Alignment holes from `requirements.alignment` come out of that tail or force a sixth block.

Tighter, if the rings stay exact and only the pools share a heap:

| Allocation | Bytes | Role |
| --- | --- | --- |
| Frame 0 host block | 121 MiB | Uniform 32 + items 8 + indirect 1 + stream 64 + staging 16 |
| Frame 1 host block | 121 MiB | Same. The other frame is still in flight |
| Dynamic heap | 24.72 MiB and up | Closed set is terrain + grass + low-detail + vbb (25,916,800). M2 indices, WMO, and liquids add to it |
| Device-local buffer heap | static mesh bytes | M2 vertices, font, particles, fog-combine, opt-geo indices |
| Image `VkHeap` | unchanged | Textures and depth. Different memory type. Do not mix |

That is **3** host-visible driver allocations for the closed set, plus a device-local buffer heap that grows in 64 MiB blocks, plus the image heap that already does.

Bytes that move off the host pile if static pools upload once: the M2 vertex term `48 * instances * vertexCount`, font 6,144, particles 262,128, fog-combine 47,136, and each opt-geo index pool. They come back as device-local bytes of the same size. The staging copy is a cursor bump in the 16 MiB ring when the mesh fits, and is reused next frame after the fence. A static mesh larger than 16 MiB still takes the oversized `CreateBuffer(bytes)` inside `Stage` (`VkContext.cpp:1147-1154`) unless the host heap can place it.

Dynamic bytes stay host-visible. WMO, terrain, grass, liquids, and M2 indices are dynamic. A slice that only repacks the rings, and then calls `vkAllocateMemory` once per static mesh on the device-local heap, misses the flying budget twice: group and index locks still allocate, and each static mesh still allocates.

### Per frame while flying

| Event | Today | Few large blocks, spans still fit |
| --- | --- | --- |
| Steady camera, rings warm | 0 | 0 |
| Stream cursor wraps the chunk | 0. The 2.75 MiB wrap is gone | 0 |
| New texture | 0 until every device-local free span misses, then one 64 MiB block | Same |
| New M2 skin | 2 host-visible allocations | 0, or 1 host block and/or 1 device-local block on a miss |
| `AllocInstances` step-up | 1 free + 1 alloc of the rounded size | Span free + span alloc. Driver alloc only on a miss |
| New WMO group | 2, or 3 with the transparency pool | 0, or 1 host block on a miss |
| New liquid buffer | 2 | 0, or 1 host block on a miss |
| Far clip or grass density change | 2 free + 2 alloc. A pool bigger than the spare cap is `vkFreeMemory` | One new pair of spans |

Budget line to test once Campaign 0 is logging: `vkAllocateMemory` per frame stays flat across an outdoor flight that does not exhaust a block. A miss is one block, not one mesh.

## Spare list is not a suballocator

`MaxSpareBuffers` is 512. `MaxSpareBytes` is 32 MiB (`VkContext.h:174-175`).

`Collect` keeps a retired buffer only when it is mapped and its usage is exactly `VERTEX_BUFFER | INDEX_BUFFER` (`VkContext.cpp:1212-1227`). The comment on that branch says staging temps would pin hundreds of MiB. Rings use other usage bits, so they never enter the list. Oversized staging is freed.

`AcquireBuffer` pops a spare only on equal size and equal usage (`VkContext.cpp:970-978`). Unique mesh sizes miss. A single buffer larger than 32 MiB cannot enter: the cap check fails and `DestroyBuffer` runs. The capped terrain vertex pool (170 MiB) is in that bucket. Grass at density 24 is 27 MiB and fits only while the rest of the list is under 5 MiB.

A host heap replaces this list. Free spans live inside the block. The 32 MiB cap is the current ceiling on a second copy of retired mesh memory, not a target size for the heap.

## `VkHeap` 64 MiB blocks

`BlockSize = 64 << 20` (`VkContext.h:55-57`). One block per memory type when nothing on that type has a free span large enough (`VkContext.cpp:41-61`).

- First fit, lowest offset first. The walk is `free.begin()` to `free.end()`, not a size lookup (`VkContext.cpp:17-24`).
- Fit at `align_up(spanStart, requirements.alignment)`. A head gap and a tail gap are written back as their own spans (`VkContext.cpp:26-34`).
- An image larger than 64 MiB becomes its own block of `requirements.size` and an empty freelist (`VkContext.cpp:41`, `55-57`).
- `Free` coalesces with the next span and the previous span. It never calls `vkFreeMemory` (`VkContext.cpp:64-82`).
- `vkFreeMemory` runs in `Destroy`, for every block (`VkContext.cpp:85-90`).

Device-local image memory is a high-water mark. A freed texture returns a span to the map. The 64 MiB `VkDeviceMemory` stays. Depth, offscreen depth, and every texture share these blocks (`VkContext.cpp:560`, `CGxDeviceVulkan.cpp:1762`, `CGxDeviceVulkan.cpp:1993`). Swapchain images belong to the swapchain, not the heap.

A fragmented block can refuse an image that would fit in the sum of its holes. The miss allocates another 64 MiB even if older blocks are almost empty. That is the image-side `vkAllocateMemory` spike while flying, and it already matches the campaign budget of “one block, not one texture,” until fragmentation forces the extra block.

Buffer memory must not be placed in these blocks. `FindMemoryType` for buffers requires host-visible and host-coherent. `FindMemoryType` for images requires device-local. Different type bits mean different block lists even if both used `VkHeap`.

## `std::map` freelist cost

`Block::free` is `std::map<VkDeviceSize, VkDeviceSize>`: offset to length (`VkContext.h:63`).

One span is one red-black node. The value is 16 bytes. On this machine’s 64-bit libstdc++ the node is **48 bytes** (tree links plus that pair), and glibc’s allocator reserves **56** bytes for it. The map object is 48 bytes and embeds its header, so an empty freelist does not call `operator new`. The first tail span does.

| Event | Heap traffic | Nodes after |
| --- | --- | --- |
| Alloc, exact fit | 1 delete | −1 |
| Alloc, head or tail left | 1 delete, 1 new | 0 |
| Alloc, head and tail left | 1 delete, 2 new | +1 |
| Free, no neighbor | 1 new | +1 |
| Free, one neighbor | 1 new, 1 delete | 0 |
| Free, both neighbors | 1 new, 2 deletes | −1 |

A fresh block that places the first image at offset 0 keeps **one** node, the tail, 48 bytes against 64 MiB. That is noise.

The cost that is not noise:

- Every image alloc walks every free span in every block of that type, pointer to pointer, including spans smaller than the request (`VkContext.cpp:10-24`). A miss pays that walk and then a 64 MiB `vkAllocateMemory`.
- Each split and merge calls `operator new` or `operator delete` for a node. The map is ordered by offset, so it cannot answer “smallest span that fits” without the walk.
- Head gaps from alignment stay as spans until a neighbor is freed. They are visited forever after.

A size-class or an intrusive list keeps the same spans: same split, same merge with the adjacent offset, same bytes on the GPU. The host difference is the walk and the per-span malloc, not the 64 MiB blocks. An intrusive node still has to live in host memory. Device-local image blocks are not mapped, so the node cannot sit inside the GPU allocation.

Ten thousand holes would be about 480 KB of 48-byte nodes, under one percent of one block. The campaign is buying off the malloc and the linear scan, not a measurable share of VRAM.

## Stream wrap, closed

`s_streamVertexPoolSize` is `0x2C0000` (2,883,584, **2.75 MiB**). `s_streamIndexPoolSize` is `0x40000` (256 KiB). `CGxDevice.cpp:62-65`, created as stream pools at `CGxDevice.cpp:1701-1702`.

Those pools are no longer turned into `VkDeviceMemory` on lock. `IBufLock` appends into the frame ring (`CGxDeviceVulkan.cpp:1596-1640`). The comment at `CGxDeviceVulkan.cpp:1597` records the old behavior: releasing the pool allocated a new 2.75 MiB buffer every wrap. `StreamRingSize` is 64 MiB (`VkContext.h:134-135`). `PrepareFrame` sets `streamChunk` and `streamOffset` back to 0 (`VkContext.cpp:722-723`).

One 64 MiB chunk holds **23** of the old vertex pools (`23 * 0x2C0000 = 66,322,432`) with **786,432** bytes left. Do not reopen the wrap, and do not size a per-frame 2.75 MiB allocation. It is not there.

What is still there, and is not the wrap:

- The first borrowed lock of a stream pool does `new VkBufferAlloc` and then reuses that object (`CGxDeviceVulkan.cpp:1618-1625`). The wrapper is 40 bytes on 64-bit (three pointers, one `VkDeviceSize`, two 32-bit fields). Two stream pools, two objects, not per frame. The campaign budget is that later locks do not call `operator new`. The bytes are not the point.
- A lock larger than 64 MiB takes `CreateBuffer(bytes)` with `borrowed` left clear, then `new VkBufferAlloc` and a later `delete` (`VkContext.cpp:1023-1028`, `CGxDeviceVulkan.cpp:1626-1635`). That is one host-visible allocation of the lock’s full size. Ordinary vertex and index locks are item-sized and stay inside the chunk.

## Bounds the next slice does not get to change

- Retired GPU memory stays until `Collect`, which runs after that frame’s fence (`VkContext.cpp:707-712`, `VkContext.cpp:1198`). A span from frame N is free on frame N+2, not on the next lock. Both ring copies are live together. 242 MiB, not 121.
- The uniform ring refuses to wrap onto another draw’s matrices (`CGxDeviceVulkan.cpp:1381-1384`).
- Staging and readback are not spare-listed (`VkContext.cpp:1212-1236`). A host heap that retains every oversized upload will pin the hundreds of MiB that comment is there to avoid.
- `VkHeap::Free` coalesces only with the adjacent offsets. A new freelist has to produce the same spans for the same alloc and free order.
