# Campaign 6 — Layout

Four slices, one concern each. Structures below are the end state. Slice 1 only moves the `VkBufferAlloc` into the pool. Slice 2 places the rings and the dynamic pools. Slice 3 uploads static pools. Slice 4 replaces the `std::map` freelist. `Draw` keeps reading `CGxPool::m_apiSpecific` as a `VkBufferAlloc*` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1256`, `CGxDeviceVulkan.cpp:1455`).

| Slice | Lands | Leaves alone |
| --- | --- | --- |
| 1. Embedded view | `CGxPool::m_view`, `m_apiSpecific = &m_view`, no `new` / `delete` of `VkBufferAlloc` | `CreateBuffer`, ring sizes, `VkHeap` |
| 2. Host blocks | One host block per frame for the five rings. Dynamic pools on a host `VkHeap` | Static pools, image heap, spare caps |
| 3. Static upload | `GxPoolUsage_Static` device-local, one staging copy, then the copy is retired | Dynamic host map, stream ring |
| 4. Size-class freelist | Intrusive size classes inside every `VkHeap` | Which heap a buffer uses, split and merge results |

`StreamRingSize` stays `64 << 20` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:135`). `s_streamVertexPoolSize` stays `0x2C0000` (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:62`). New comments use the repo TextEmoji form.

## One VkBufferAlloc on the pool

Owner is `CGxPool`. The record is a tail member, after `m_name` (`Engine/Source/Gx/CGxPool.h:14-23`). Existing members stay in that order. `PoolCreate` already allocates `sizeof(CGxPool)` (`CGxDevice.cpp:1676-1678`), so the tail rides along. D3D and GL keep writing their own pointer through `m_apiSpecific` and do not read `m_view`.

```cpp
struct CGxPool : public TSLinkedNode<CGxPool> {
    // existing members through m_name
    VkBufferAlloc m_view;
};
```

`VkBufferAlloc` moves to `Engine/Source/Gx/CGxDeviceVulkan/VkAlloc.h` (handles only). `VkContext.h` and `CGxPool.h` both include it. The object stays trivially destructible. No destructor frees `buffer` or `memory`. `CGxPool::~CGxPool` stays empty (`Engine/Source/Gx/CGxPool.cpp:10-11`). `BufDestroy` unlinks and `delete`s the `CGxBuf` (`CGxDevice.cpp:1157-1168`) and does not release the GPU buffer.

`CGxBuf` keeps `m_index` as the byte offset inside that one buffer (`Engine/Source/Gx/CGxBuf.h:9-18`). Several bufs share one pool (`CGxDevice.cpp:1667-1671`). A second `VkBufferAlloc` on the buf would retire the same `VkBuffer` the pool retires.

While `m_view.buffer` is set, `m_apiSpecific` points at `&m_view`. When the view is clear, `m_apiSpecific` is null. It never points at `frame.streamChunks[]`. That element owns the chunk. The pool holds a copy.

Fields, LP64. Sentinels are written by `ClearView`. A zero `hostBlock` is frame block 0, which is live.

| Field | Role | Empty |
| --- | --- | --- |
| `buffer` | `VkBuffer` Draw binds | null |
| `memory` | Dedicated `VkDeviceMemory` from `CreateBuffer` | null |
| `mapped` | Host pointer for dynamic pools and borrowed ring views | null |
| `size` | Requested bytes (`VkContext.cpp:984`), not driver rounding | 0 |
| `usage` | Usage the buffer was created with | 0 |
| `borrowed` | 1 = alias of a frame chunk (`VkContext.h:24-25`) | 0 |
| `hostBlock` | Index of this frame’s host block. Set on the chunk object in `streamChunks` / `stagingChunks` / the permanent rings. Cleared on the pool’s copy | -1 |
| `span` | `VkAllocation` when the bytes live in a `VkHeap`. `span.memory` null means no heap span | zero |

`VkAllocation` gains `int32_t heap` (`VkContext.h:11-16`). `0` is the image heap, `1` the static-buffer heap, `2` the dynamic host heap. `Alloc` writes it. Empty is `span.memory == null`, not `span.block == 0`. Block 0 is a real block.

`ClearView` zeroes the handles, sets `borrowed = 0` and `hostBlock = -1`, and zeroes `span`. `IReleasePool` does not `delete` the member (`CGxDeviceVulkan.cpp:1556-1565`). `PoolDestroy` `delete`s the pool after that (`CGxDeviceVulkan.cpp:1581-1586`, `CGxDevice.cpp:1684-1688`).

### Lock order

`IBufLock` for `GxPoolUsage_Stream` (`CGxDeviceVulkan.cpp:1596-1640`):

1. `StreamLock` fills a stack `VkBufferAlloc` (`CGxDeviceVulkan.cpp:1603-1605`, `VkContext.cpp:1086-1089`). Failure returns emergency memory and does not write `m_index`, `m_view`, or `m_apiSpecific` (`CGxDeviceVulkan.cpp:1605-1606`).
2. Predecessor with `buffer` set and `borrowed == 0`: `RetireBuffer` that record, then assign (`CGxDeviceVulkan.cpp:1612-1613`, `CGxDeviceVulkan.cpp:1628-1629`).
3. Predecessor with `borrowed == 1`: leave the ring in `streamChunks`. Do not retire.
4. Borrowed slice: copy it, then set `memory` null, `borrowed = 1`, `hostBlock = -1`, `span` empty (`CGxDeviceVulkan.cpp:1623-1625`). The chunk keeps `hostBlock` and its `memory`.
5. Owned slice (`bytes > chunkSize`, `VkContext.cpp:1023-1028`): that dedicated buffer is the only owner. `borrowed` stays 0. `memory` stays the `CreateBuffer` allocation.

Assigning first drops an owned handle that never reaches `VkGarbage`. Retiring after the assign retires the ring.

`m_apiSpecific = &m_view`. `buf->m_index` is the chunk offset `StreamLock` returned. `pool->m_offset` stays `index + buf->m_size` (`CGxDeviceVulkan.cpp:1638-1639`). The returned pointer is `mapped + index`.

A failed oversized lock does not `memcpy` into a borrowed ring on unlock. `IBufUnlock` (`CGxDeviceVulkan.cpp:1652-1671`) copies emergency bytes only into an owned `mapped` of this pool. A borrowed view or a null buffer sets `m_valid = 0` and `m_intF5C = 1` (`CGxDeviceVulkan.cpp:1662-1664`), which makes `Draw` return (`CGxDeviceVulkan.cpp:1194`).

`PoolSizeSet` still releases, then stores the new size (`CGxDeviceVulkan.cpp:1576-1578`). A borrowed view is cleared. The chunk stays. An owned buffer is retired. The ring cursor is not moved. `BufStream` still calls `PoolSizeSet` only when the lock is larger than `pool->m_size` (`CGxDevice.cpp:1062-1067`).

`IPoolBuffer` (`CGxDeviceVulkan.cpp:1542-1553`) writes into `m_view` and sets `m_apiSpecific`. No heap `VkBufferAlloc`. After slice 2 it is the dynamic path. After slice 3 the static path does not call it.

## Where the bytes live

| Kind | Memory | Owner | Lifetime |
| --- | --- | --- | --- |
| Uniform, items, indirect, first stream chunk, first staging chunk | One host-visible coherent block per `VkFrame` | `VkFrame` | Device. Cursor resets in `PrepareFrame` |
| Further stream or staging chunk | Another host block on that same frame | `VkFrame` | Device. High water |
| `GxPoolUsage_Dynamic` | Host `VkHeap`, 64 MiB blocks | Pool’s `m_view.span` | Pool, freed in `Collect` |
| `GxPoolUsage_Static` | Device-local `VkHeap`, 64 MiB blocks | Pool’s `m_view.span` | Pool, freed in `Collect` |
| `GxPoolUsage_Stream` that fits | Borrowed view of the frame chunk | Chunk on the frame, copy on the pool | Chunk until `DestroyDevice`. Copy overwritten by the next lock |
| Lock or staging larger than its chunk | Dedicated `CreateBuffer` | Pool view, or `VkGarbage` after `Stage` | Spare list or destroy after the fence |
| Images, depth | Existing image `VkHeap` | `VkImageAlloc` | Unchanged |
| Dummy 32 bytes, readback | Dedicated `CreateBuffer` | Device member / stack | Unchanged (`CGxDeviceVulkan.cpp:343`, `CGxDeviceVulkan.cpp:2209-2254`) |

Dynamic pools stay host-mapped. `IBufLock` still returns `mapped + buf->m_index` (`CGxDeviceVulkan.cpp:1643-1649`). Terrain, grass, WMO (`s_worldPoolUsage` is 1, `WoW/Source/WorldClient/World.cpp:1717`), liquids (`MapChunkLiquid.cpp:376-377`), and M2 index pools are this row. Static pools leave the host pile. Image blocks and buffer blocks do not share a `VkHeap`.

## Host rings

One `VkDeviceMemory` per in-flight frame, `HOST_VISIBLE | HOST_COHERENT` (`VkContext.cpp:1001`). `FramesInFlight` is 2 (`VkContext.h:131`). Slot 1 does not allocate into slot 0.

The block holds the five buffers that already exist on `VkFrame` (`VkContext.h:107-118`), at fixed offsets, each its current size:

| Buffer | Size | Constant |
| --- | ---: | --- |
| `uniforms` | 32 MiB | `UniformRingSize` (`VkContext.h:132`) |
| `items` | 8 MiB | `ItemRingSize` (`VkContext.h:136`) |
| `indirect` | 1 MiB | `IndirectRingSize` (`VkContext.h:137`) |
| `streamChunks[0]` | 64 MiB | `StreamRingSize` |
| `stagingChunks[0]` | 16 MiB | `StagingRingSize` (`VkContext.h:133`) |

Sum **121 MiB** before `requirements.alignment`. Create the five `VkBuffer`s, read requirements, align each offset, then one `vkAllocateMemory` of that sum. Map the block once. `mapped` for each buffer is `block.mapped + offset`. `vkBindBufferMemory` at that offset. `out.size` stays the ring size, so cursor tests (`VkContext.cpp:1043`, `CGxDeviceVulkan.cpp:1381`, `VkContext.cpp:1059-1062`, `VkContext.cpp:1076`) see the same capacities.

`CreateDeviceResources` (`VkContext.cpp:290-300`) binds all five, including the first stream chunk and the first staging chunk, so the steady device pays one driver allocation per frame instead of five. `BumpChunk` (`VkContext.cpp:1031-1037`) finds `chunks[0]` already there.

A chunk that does not fit the block gets another host block on that frame, size `max(chunkSize, requirements.size)`, same map-once bind. `streamChunks` / `stagingChunks` stay the high-water mark (`VkContext.h:116-118`). `PrepareFrame` sets the cursors back to 0 (`VkContext.cpp:717-723`) and does not destroy chunks and does not call `IReleasePool`. A second 64 MiB stream chunk stays for the life of the device. That is the same high water as today.

`DestroyBuffer` (`VkContext.cpp:1092-1098`) `vkDestroyBuffer`s. It `vkFreeMemory`s only when `hostBlock < 0` and `span.memory` is null and `memory` is set. `DestroyDevice` destroys the buffers (`VkContext.cpp:649-655`), then frees each frame’s host blocks once.

Cursors are the existing fields: `uniformOffset`, `itemCursor`, `indirectCursor`, `streamOffset`, `stagingOffset`. The uniform overflow guard stays (`CGxDeviceVulkan.cpp:1381-1383`): the draw returns, `uniformOffset` does not move, another draw’s 768 bytes stay. Item and indirect exhaustion still return 0 (`VkContext.cpp:1061`, `VkContext.cpp:1076`). Do not grow those three rings.

`borrowed` on a `BumpChunk` view stays 1 (`VkContext.cpp:1045-1047`). The chunk object in the vector stays `borrowed == 0` and keeps `hostBlock`.

Oversized (`bytes > chunkSize`) stays a dedicated buffer. That path calls `AcquireBuffer` (`VkContext.cpp:970-979`) so an exact `size` and `usage` spare is reused, then `CreateBuffer` on a miss. `Stage` still retires a non-borrowed chunk before the caller writes (`VkContext.cpp:1151-1154`). The map stays valid until `Collect`. Do not free it inside `BumpChunk`. A borrowed `Stage` returns with no retire (`VkContext.cpp:1157`). Meshes larger than 16 MiB keep that dedicated staging path. They are not retained in the frame block.

## Dynamic host heap

`VkContext::m_dynamicHeap`, a `VkHeap` with `BlockSize` `64 << 20` (`VkContext.h:56`). Memory type is host-visible coherent. Each block is mapped once for its whole size. A span’s `mapped` is `block.mapped + span.offset`. Unmap in `Destroy` only.

`IPoolBuffer` for `GxPoolUsage_Dynamic`: `vkCreateBuffer` of `pool->m_size` with `VERTEX | INDEX` (`CGxDeviceVulkan.cpp:1546`), `Alloc` on this heap, bind at `span.offset`. Failure frees the span immediately when nothing has recorded the buffer, destroys the unpublished `VkBuffer`, and leaves `m_view` clear. Same shape as a failed image bind (`VkContext.cpp:1127-1131`).

The span size is `pool->m_size`, including the WMO `+ 4096` (`MapObjGroup.cpp:1434`). A request larger than 64 MiB is its own block (`VkContext.cpp:41`). The capped terrain vertex pool is that case. A fit takes a free span and does not call `vkAllocateMemory`.

Release goes through `IReleasePool` into `VkGarbage`, not `vkFreeMemory` of the block and not the spare list. `garbage.memory` and `garbage.mapped` are null. `garbage.buffer` is the `VkBuffer`. `garbage.alloc` is the span. `Collect` destroys the buffer object and `Free`s the span (`VkContext.cpp:1239-1240`). The block stays mapped.

`AcquireBuffer` is no longer how dynamic pools are born. The spare list remains the cache of dedicated `VERTEX | INDEX` buffers (`VkContext.cpp:1212-1227`). Caps stay 512 and 32 MiB (`VkContext.h:174-175`). A heap span is not an entry. Slices are not pushed.

## Static upload

`VkContext::m_staticHeap`, another `VkHeap`, `DEVICE_LOCAL`, not mapped. Not `m_heap`. Images stay on `m_heap` (`VkContext.cpp:1112-1114`).

First successful lock of a `GxPoolUsage_Static` pool creates one `VkBuffer` of `pool->m_size`, usage `VERTEX | INDEX | TRANSFER_DST`, allocated from `m_staticHeap`, bound, not mapped. `m_view.mapped` is null, so `Collect` will not spare it (`VkContext.cpp:1213-1214`). `usage` includes `TRANSFER_DST`, so it also misses the exact `VERTEX | INDEX` compare.

CPU bytes go through `Stage` on the uploading frame (`VkContext.cpp:1138-1157`), after `PrepareFrame`. Callers that `BufLock` and fill the buf write `buf->m_size` bytes at the returned pointer:

- M2 vertices (`Engine/Source/Model2/M2Shared.cpp:398-422`)
- M2 opt indices (`Engine/Source/Model2/M2Model.cpp:2644-2648`)
- Particle indices, the whole `0x1FFF8` (`Engine/Source/Services/ParticleSystem2.cpp:748-765`)
- Font indices, 3072 (`Engine/Source/Gxu/GxuFontMiscClasses.cpp:228-244`)
- Fog-combine grid (`WoW/Source/Game/GameClient/FFXEffects.cpp:659-701`)

`IBufUnlock` records `vkCmdCopyBuffer` on `UploadCmd()` (`VkContext.cpp:825-840`) from the staging offset to `buf->m_index`, size `buf->m_size`. `Submit` already runs `uploadCmd` before `frame.cmd` (`VkContext.cpp:849-858`). After the copy, a buffer barrier on that command buffer: `TRANSFER` / `TRANSFER_WRITE` to `VERTEX_INPUT` / `VERTEX_ATTRIBUTE_READ | INDEX_READ`.

`BufData` (`CGxDeviceVulkan.cpp:1685-1701`) has its own range. It stages `size` bytes and copies those to `buf->m_index + offset`. It does not upload the rest of the buf.

A borrowed staging slice is not retired. The frame cursor reclaims it. A non-borrowed staging slice was already `RetireBuffer`’d inside `Stage`. Do not retire it again. `m_view` keeps the device buffer, with `mapped` null. The next lock of the same pool copies into that buffer. `PoolSizeSet`, `PoolDestroy`, and `IReleasePools` retire the device buffer first (`CGxDeviceVulkan.cpp:1568-1578`). `IReleasePools` still `Discard`s (`CGxDeviceVulkan.cpp:1571-1572`) so `m_hasData` clears and the next lock publishes again.

`vkCmdCopyBuffer` offsets and the byte count are multiples of 4. These bufs are item-aligned and `Stage` aligns the source to 16 (`VkContext.cpp:1147`). A failed `UploadCmd` or a failed heap alloc sets `m_valid = 0` and `m_intF5C = 1`. A device buffer that never received a copy is retired with the span. A later failed re-copy leaves the published buffer in place and still invalidates the buf for the frame.

Both frame slots bind the same device `VkBuffer`. The span stays reserved until the retiring slot’s `Collect`. The other slot may still have that buffer in a submitted command buffer.

## Size-class freelist

Same class on `m_heap`, `m_staticHeap`, and `m_dynamicHeap`. `Block::free` (`VkContext.h:63`) becomes an intrusive list plus 64 size classes. Results match `VkHeap::Alloc` (`VkContext.cpp:7-61`) and `VkHeap::Free` (`VkContext.cpp:64-82`).

```cpp
struct VkSpan {
    VkDeviceSize offset;
    VkDeviceSize size;
    VkSpan* addrPrev;
    VkSpan* addrNext;
    VkSpan* classPrev;
    VkSpan* classNext;
    int32_t klass;
};
```

`Block` keeps `memory`, `size`, `type`. It drops the `std::map`. It holds an address-ordered list (`addr` links) and `VkSpan* classes[64]`. Class of a span is `floor(log2(size))` clamped to `0..63`. A span of size 1 is class 0. The class is the bucket. The stored size is the exact byte count. Alloc does not round the request up to a class.

Search, per block of the requested `type`, blocks in creation order:

```
best = null
for class from floor(log2(requirements.size)) through 63:
    for span in that class, offset ascending:
        if best and span.offset >= best.offset: break
        start = align_up(span.offset, alignment)
        if start + requirements.size <= span.offset + span.size:
            best = span
            break
```

`alignment` is `requirements.alignment`, or 1 when that is 0 (`VkContext.cpp:8`). The winner is the lowest offset that fits. A span smaller than `requirements.size` cannot sit in an earlier class. The first fit in each class is that class’s lowest fitting offset because the class list is offset-sorted. The minimum of those is the map’s first fit.

Split, unchanged (`VkContext.cpp:26-34`): remove the span; if `start > span.offset`, free `[span.offset, start)`; if `start + requirements.size < span.end`, free the tail. Both leftovers are their own spans, filed by their exact sizes.

Merge, unchanged (`VkContext.cpp:67-82`): insert `[offset, size)`; if the address-next starts where this ends, absorb it; if the address-prev ends where this starts, the prev absorbs this. No other coalescing. A size change moves the node to the new class. Address links stay on the surviving node.

A miss allocates `max(requirements.size, BlockSize)` (`VkContext.cpp:41-61`). The alloc is placed at offset 0 for `requirements.size`. The tail, when the request is smaller than the block, is the one free span. A failed `vkAllocateMemory` pushes no block (`VkContext.cpp:51`). `m_blocks` is append-only until `Destroy` (`VkContext.cpp:85-90`). `VkAllocation.block` in `VkGarbage` (`VkContext.h:44`) stays valid across the fence. Two slots may free into the same block in either order. `Free` does not call `vkFreeMemory`.

Span nodes come from a freelist on the heap. `Alloc` / `Free` recycle them. `Destroy` releases the node storage with the blocks. Device-local blocks are not mapped, so the node does not live inside the GPU allocation. Host dynamic blocks are mapped for CPU buffer bytes; span nodes still live on the heap object, not in that map.

`Collect` (`VkContext.cpp:1239`) frees `garbage.alloc` on the heap named by `garbage.alloc.heap`.

## Retire

Same queue as today.

`Retire` appends to the current frame (`VkContext.cpp:1160-1162`). `PrepareFrame` calls `Collect` only after `vkWaitForFences` succeeds (`VkContext.cpp:709-712`). The timeout path skips `Collect`, leaves `frame.garbage` in place, and still zeros the ring cursors (`VkContext.cpp:707-723`). Heap spans are not freed on that path. Ring cursors still reset.

`RetireBuffer` (`VkContext.cpp:1165-1183`):

- `borrowed`: clear the record it was passed and return. Nothing is queued.
- Otherwise, when `buffer` is set and `span.memory` is null: copy `buffer`, `memory`, `mapped`, `size`, `usage` into `VkGarbage` and queue it. Then null `buffer`, `memory`, and `mapped` on the source.

Heap-backed records do not use that copy. `IReleasePool` queues `buffer` plus `span`, with `garbage.memory` null and `garbage.mapped` null, then `ClearView`. `Collect` then cannot spare the slice and cannot `vkFreeMemory` the block (`VkContext.cpp:1213-1236`).

Dedicated `VERTEX | INDEX` buffers still take the spare path. The caps and the front-eviction loop stay (`VkContext.cpp:1216-1226`). A newcomer larger than 32 MiB still empties the list and is destroyed. Staging usage is `TRANSFER_SRC`, so those temps are destroyed after the fence. The comment at `VkContext.cpp:1212` stands: do not park oversized uploads on the spare list.

`IReleasePool` on a borrowed view `ClearView`s and returns. `DestroyDevice` is what `DestroyBuffer`s `streamChunks` (`VkContext.cpp:655`). Null `memory` on the pool copy does not stop `Collect` from destroying `garbage.buffer` (`VkContext.cpp:1229-1231`), which is why a borrowed view never enters `VkGarbage`.

`IDestroyVulkan` releases pools at `CGxDeviceVulkan.cpp:378` before `DestroyInstance` (`CGxDeviceVulkan.cpp:407`). Owned buffers are already on some frame’s garbage list. `DestroyDevice` collects every frame (`VkContext.cpp:649-650`), then destroys leftovers, spares, and ring buffers, then `m_heap.Destroy`. Static and dynamic heaps are destroyed in that same stretch, after `Collect`, so a span queued by `IReleasePools` is freed before the block is.

`VkGarbage` still has no borrowed flag (`VkContext.h:35-45`). The borrowed bit has to be honored before the push.

## Counters

Names in `docs/perf-squads/vulkan/counter.md` stay. Driver allocations stay on the two calls that already exist:

- `vk_alloc_heap` — `VkHeap::Alloc` (`VkContext.cpp:51`). Image blocks, static-buffer blocks, and dynamic host blocks.
- `vk_alloc_buffer` — the `vkAllocateMemory` inside `CreateBuffer` (`VkContext.cpp:1003`). Dedicated buffers, and the per-frame ring block (one increment per block, not per `VkBuffer`).

A suballocated `vkCreateBuffer` does not increment. A freelist hit does not increment (`VkContext.cpp:37`).

`bufalloc_new` on stream locks is 0 after slice 1, including the first lock. `bufalloc_pool_new` is 0. Both were the `new VkBufferAlloc` at `CGxDeviceVulkan.cpp:1544`, `1619`, and `1634`.

`host_visible_bytes` adds `VkBufferAlloc.size` when a host-visible buffer becomes live and subtracts it when that memory is freed (`DestroyBuffer`, or `Collect` when the buffer is not spared). Borrowed views do not change it. `Retire` does not change it. Dynamic spans stay in this gauge. Ring buffers add their ring size when the frame block is bound. Static device buffers stay off this gauge.

`static_host_bytes` stops growing once static pools stop calling `AcquireBuffer`. `devlocal_static_bytes` is the live `pool->m_size` of published static device buffers. `IReleasePool` subtracts it. `devlocal_static_uploads` increments when that device buffer is first published and the staging copy for that publish is done. A later copy into the same buffer does not allocate and does not increment. `PoolSizeSet` releases and the next publish increments again.

`stream_wraps` stays the cursor roll at `VkContext.cpp:1050-1052`. The `PrepareFrame` reset is not a wrap.
