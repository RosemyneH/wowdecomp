# Campaign 6 — Counter

Vulkan buffer lifetime. Counters and `OsGetAsyncTimeClocks` zones only. No behavior change. Samples fold into the Campaign 0 one-second line under that compile-out flag (default off). Names in the field list stay fixed.

`OsGetAsyncTimeClocks` is `CLOCK_MONOTONIC` on Linux. A zone adds `end - start` into a relaxed atomic. The hot path does not allocate, log, or change a return value. The one-second printer reads the atomics.

## Closed: 64 MiB stream ring

`VkContext::StreamRingSize` is `64 << 20` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:135`). `PrepareFrame` sets `streamChunk` and `streamOffset` back to 0 and leaves `streamChunks` resident (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:722-723`). `IBufLock` appends a borrowed view of that ring (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1596-1625`).

The note at `CGxDeviceVulkan.cpp:1597` records the old fault: releasing the pool on wrap allocated a fresh 2.75 MiB buffer. That size is `s_streamVertexPoolSize` (`0x2C0000`, `Engine/Source/Gx/CGxDevice/CGxDevice.cpp:62`). The stream path now calls `StreamLock` (`CGxDeviceVulkan.cpp:1605`) into the 64 MiB ring. `stream_wraps` measures cursor rolls on that ring. The 2.75 MiB wrap alloc stays closed.

## vkAllocateMemory calls

`vk_alloc` increments once per `vkAllocateMemory` that is actually issued.

| Bucket | Site | What the call buys |
| --- | --- | --- |
| `vk_alloc_heap` | `VkHeap::Alloc`, `VkContext.cpp:51` | A device-local image block. Size is `BlockSize` (`64 << 20`, `VkContext.h:56`) or the request when the request is larger (`VkContext.cpp:41`). A freelist hit returns at `VkContext.cpp:37` and does not increment. |
| `vk_alloc_buffer` | `VkContext::CreateBuffer`, `VkContext.cpp:1003` | One host-visible allocation for that buffer. `memoryTypeIndex == UINT32_MAX` short-circuits the same `if` and does not increment. |

`vk_alloc` is the sum. Image-block growth and buffer growth stay in separate columns. The budget is a flat `vk_alloc` while flying the outdoor scene.

`CreateBuffer` is the buffer allocator. Its callers:

- Startup rings, two frames: uniform `32 << 20`, items `8 << 20`, indirect `1 << 20` (`VkContext.cpp:290-300`).
- `AcquireBuffer` on a spare miss (`VkContext.cpp:979`). Static and dynamic pools enter here from `IPoolBuffer` (`CGxDeviceVulkan.cpp:1546`).
- `BumpChunk` for a new chunk (`VkContext.cpp:1034`) and for a lock larger than the chunk (`VkContext.cpp:1024`).

A spare hit (`VkContext.cpp:972-976`) returns an existing `VkBufferAlloc` and does not increment.

## VkBufferAlloc new/delete per lock

`bufalloc_new` and `bufalloc_delete` count `operator new` and `operator delete` of the `VkBufferAlloc` object inside `CGxDeviceVulkan::IBufLock` (`CGxDeviceVulkan.cpp:1589`). They count the C++ object around the pool pointer. GPU memory is `vk_alloc`.

The stream branch is `CGxDeviceVulkan.cpp:1596`. `slice` at `CGxDeviceVulkan.cpp:1603` is a stack value. `StreamLock` fills it (`CGxDeviceVulkan.cpp:1605`, `VkContext.cpp:1086-1089`).

| Path | Lines | Count |
| --- | --- | --- |
| Borrowed view, pool already holds a borrowed alloc | `CGxDeviceVulkan.cpp:1623` | 0. The slice is copied onto the existing object. |
| Borrowed view, pool holds a non-borrowed alloc | `CGxDeviceVulkan.cpp:1614` then `1619` | One `bufalloc_delete`, then one `bufalloc_new`. One transition off the oversized object. |
| Borrowed view, `m_apiSpecific` is null | `CGxDeviceVulkan.cpp:1619` | One `bufalloc_new`. Later borrowed locks on that pool stay at `1623`. |
| Oversized slice (`borrowed == 0`) | `CGxDeviceVulkan.cpp:1631` and `1634` | One `bufalloc_delete` when an alloc exists, and one `bufalloc_new`, on every successful lock. |

`borrowed == 0` is `BumpChunk` when `bytes > chunkSize` (`VkContext.cpp:1023-1028`). `StreamLock` passes `StreamRingSize`, so that slice is a lock larger than 64 MiB. `delete` at `1631` runs for a borrowed or non-borrowed previous object. `RetireBuffer` at `1629` runs only when the previous object is not borrowed.

`IPoolBuffer` (`CGxDeviceVulkan.cpp:1544`, failure `1549`) and `IReleasePool` (`CGxDeviceVulkan.cpp:1563`) new and delete on pool create, `PoolSizeSet` (`1576-1577`), and `PoolDestroy` (`1581-1583`). Those increments are `bufalloc_pool_new` and `bufalloc_pool_delete`. They stay out of the per-lock pair.

The budget is `bufalloc_new == 0` on stream locks, including the first lock on a pool, once the borrowed view lives inside `CGxPool` / `CGxBuf`.

## Stream ring wraps

`stream_wraps` increments when `BumpChunk` advances the stream cursor (`VkContext.cpp:1050-1052`) for a chunk of `StreamRingSize` (`VkContext.cpp:1089`).

The aligned range did not fit the current chunk. `chunkIndex` increases and `cursor` returns to 0. The next iteration reuses `streamChunks[chunkIndex]` when the slot exists, or `CreateBuffer`s another 64 MiB chunk (`VkContext.cpp:1032-1037`). A reuse increments `stream_wraps` only. A new chunk increments `stream_wraps` and `vk_alloc_buffer`.

`PrepareFrame` (`VkContext.cpp:722-723`) resets the cursor for the frame. That reset is not a wrap.

Staging uses `BumpChunk` with `StagingRingSize` (`16 << 20`, `VkContext.cpp:1147`). Those advances are `staging_wraps`.

## Host-visible bytes

`host_visible_bytes` is the live sum of host-visible buffer sizes.

`CreateBuffer` selects `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` (`VkContext.cpp:1001`) and maps the allocation (`VkContext.cpp:1009-1010`). When `CreateBuffer` returns 1, add `VkBufferAlloc.size` (the size stored at `VkContext.cpp:984`). Driver rounding above that size is not stored on `VkBufferAlloc`, so the gauge uses the engine size.

Subtract that `size` when the memory is freed:

- `DestroyBuffer` (`VkContext.cpp:1092-1096`), including spare eviction (`VkContext.cpp:1221`).
- The direct `vkFreeMemory` in `Collect` when the buffer is not pushed onto the spare list (`VkContext.cpp:1229-1235`).

A spare push (`VkContext.cpp:1225`) and a spare hit (`VkContext.cpp:972-976`) keep the allocation. The gauge stays put. `RetireBuffer` (`VkContext.cpp:1165-1178`) queues garbage and does not free; the gauge drops later, in `Collect`, unless the buffer is spared. Borrowed views return at `VkContext.cpp:1166-1168` and do not change the gauge. Ring chunks live until `DestroyDevice` (`VkContext.cpp:655`).

`VkHeap` memory is device-local (`VkContext.cpp:1112-1114`). It stays on `vk_alloc_heap` and off this gauge.

Non-stream `IBufLock` returns the host mapping from `IPoolBuffer` (`CGxDeviceVulkan.cpp:1643-1649`). Bytes added from that `AcquireBuffer` when `pool->m_usage == GxPoolUsage_Static` also add to `static_host_bytes`. `IReleasePool` subtracts them when the alloc is not borrowed (`CGxDeviceVulkan.cpp:1560-1563`). Dynamic pools remain in `host_visible_bytes` only.

## Device-local static uploads

`devlocal_static_uploads` counts one finished upload of a `GxPoolUsage_Static` pool into device-local memory, after the staging copy is retired. `devlocal_static_bytes` is the live device-local size of those pools.

The upload site is absent. `CreateBuffer` always asks for host-visible coherent memory (`VkContext.cpp:1001`). A static lock returns `alloc->mapped` (`CGxDeviceVulkan.cpp:1649`). `IBufUnlock` copies into that mapping (`CGxDeviceVulkan.cpp:1657-1660`). Baseline is 0 uploads and 0 device-local static bytes. Image creates (`VkContext.cpp:1112-1114`) stay on `vk_alloc_heap`.

When the device-local static slice lands, the increment sits on the copy that retires staging, for `GxPoolUsage_Static` only. One upload per pool. `IReleasePool` of that pool subtracts `devlocal_static_bytes`.

## Zones

Inclusive. Sample `OsGetAsyncTimeClocks` on entry and on every return.

| Zone | Function | Line |
| --- | --- | --- |
| `zone_vk_heap_alloc` | `VkHeap::Alloc` | `VkContext.cpp:7` |
| `zone_create_buffer` | `VkContext::CreateBuffer` | `VkContext.cpp:982` |
| `zone_stream_lock` | `VkContext::StreamLock` | `VkContext.cpp:1086` |
| `zone_ibuf_lock` | `CGxDeviceVulkan::IBufLock` | `CGxDeviceVulkan.cpp:1589` |

`zone_ibuf_lock` contains `zone_stream_lock` on the stream branch, and `zone_create_buffer` when a pool or an oversized lock allocates. `zone_stream_lock` contains `BumpChunk`. Each zone is its own row.

## Leave in place

A counter may read these. The predicates stay as written.

- Frame garbage: `RetireBuffer` skips borrowed views (`VkContext.cpp:1165-1168`). `Collect` runs after the frame fence (`VkContext.cpp:712`, `1198`).
- Uniform ring overflow returns before a draw reuses another draw's matrices (`CGxDeviceVulkan.cpp:1381-1383`).
- Spare caps: `MaxSpareBuffers` 512 and `MaxSpareBytes` 32 MiB (`VkContext.h:174-175`), enforced in `Collect` (`VkContext.cpp:1216-1226`).
- `VkHeap`'s `std::map` freelist (`VkContext.h:63`) is the later freelist slice. This note adds no counter for map nodes.

## Field list

One second, this order. Rates are the delta since the previous line. `host_visible_bytes`, `static_host_bytes`, and `devlocal_static_bytes` are levels.

```
vk_alloc vk_alloc_heap vk_alloc_buffer bufalloc_new bufalloc_delete bufalloc_pool_new bufalloc_pool_delete stream_wraps staging_wraps host_visible_bytes static_host_bytes devlocal_static_uploads devlocal_static_bytes zone_vk_heap_alloc zone_create_buffer zone_stream_lock zone_ibuf_lock
```

## Steady outdoor second

Before the lifetime slices: `vk_alloc_buffer` moves when a new host-visible buffer is created (pool, ring chunk, or oversized lock). `static_host_bytes` holds the static pools. `devlocal_static_uploads` is 0. A borrowed stream lock after the pool's first `VkBufferAlloc` adds 0 to `bufalloc_new`. An oversized stream lock adds one new and one delete. `stream_wraps` counts cursor rolls on the 64 MiB ring.

After the budget: `vk_alloc` is flat while the camera flies. Static mesh pools sit in `devlocal_static_bytes`. `bufalloc_new` on stream locks is 0.
