# Campaign 6 edges

Host suballocation, an embedded stream view, device-local static upload, and a `VkHeap` freelist rewrite all cross these five cases. `StreamRingSize` stays 64 MiB. The old wrap that allocated a fresh 2.75 MiB pool (`s_streamVertexPoolSize` is `0x2C0000` in `Engine/Source/Gx/CGxDevice/CGxDevice.cpp:62`) stays closed.

## Lock bigger than the stream chunk

`BumpChunk` takes the dedicated path when `bytes > chunkSize` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:1023`). `StreamLock` passes `StreamRingSize` (`VkContext.cpp:1086`, `VkContext.h:135`). The same branch serves `Stage` with `StagingRingSize` (16 MiB, `VkContext.cpp:1147`, `VkContext.h:133`).

The dedicated `VkBufferAlloc` leaves `borrowed` at 0 (`VkContext.h:25`). `IBufLock` then `new`s a wrapper and stores it on the pool (`CGxDeviceVulkan.cpp:1634`). A borrowed lock reuses one wrapper (`CGxDeviceVulkan.cpp:1611`). The first borrowed lock, and every dedicated lock, still calls `operator new`. Embedding the view in `CGxPool` / `CGxBuf` removes that heap alloc for slices that fit. A lock larger than the chunk still needs its own `VkBuffer`, its own `mapped` pointer, and `index == 0`.

`Draw` copies `pool->m_apiSpecific` into the command buffer (`CGxDeviceVulkan.cpp:1256`). The next lock may overwrite that view. The dedicated buffer stays alive until the recording slot’s fence: `RetireBuffer` queues `VkGarbage` (`VkContext.cpp:1165`), and `Collect` runs only after `vkWaitForFences` succeeds (`VkContext.cpp:709`).

`Stage` retires a non-borrowed chunk before the caller writes `staging.data` (`VkContext.cpp:1151`). The map stays valid because `Collect` has not run. An embed that frees a non-borrowed buffer inside `BumpChunk` drops that upload.

`BufStream` grows the logical pool before the lock when `itemCount * itemSize` exceeds `pool->m_size` (`CGxDevice.cpp:1066`). `PoolSizeSet` releases the current view (`CGxDeviceVulkan.cpp:1576`). A borrowed view is deleted and the ring chunk stays (`CGxDeviceVulkan.cpp:1560`). A previous dedicated buffer is retired. The ring cursor is unchanged. A lock between the 2.75 MiB stream pool and 64 MiB is an ordinary bump, including a tail wrap onto the next chunk.

`CreateBuffer` failure returns 0 (`VkContext.cpp:1003`). `IBufLock` then returns emergency memory and leaves `m_index` and `m_apiSpecific` alone (`CGxDeviceVulkan.cpp:1605`). `IBufUnlock` `memcpy`s `buf->m_size` bytes into `IPoolBuffer` at that stale `m_index` (`CGxDeviceVulkan.cpp:1660`). If the pool still holds a borrowed ring view, the copy runs through the live chunk. If `PoolSizeSet` already cleared `m_apiSpecific`, `IPoolBuffer` allocates a buffer of the new pool size (`CGxDeviceVulkan.cpp:1542`) and the copy starts at the previous lock’s index. A failed oversized lock keeps `m_valid == 0` and does not write the live ring. `m_intF5C` already drops the frame when the emergency copy has nowhere to land (`CGxDeviceVulkan.cpp:1662`, `Draw` at `CGxDeviceVulkan.cpp:1194`).

Teardown releases pools before `DestroyDevice` (`CGxDeviceVulkan.cpp:378`, `CGxDeviceVulkan.cpp:407`). `DestroyDevice` collects garbage, then destroys `streamChunks` (`VkContext.cpp:649`). A dedicated buffer still owned by the pool is retired there. A borrowed view is not retired, or the chunk is freed twice.

## Ring wrap

When the aligned tail cannot hold the lock, `BumpChunk` increments `chunkIndex` and zeros the cursor (`VkContext.cpp:1050`). It appends one `chunkSize` buffer only when the frame’s vector is short (`VkContext.cpp:1032`). Those chunks stay on the `VkFrame` (`VkContext.h:116`). `PrepareFrame` sets `streamChunk` and `streamOffset` back to 0 (`VkContext.cpp:722`). It does not destroy them and it does not call `IReleasePool`.

`IBufLock` overwrites a borrowed wrapper in place (`CGxDeviceVulkan.cpp:1623`). The wrap path does not `new` a `VkBufferAlloc` once the pool already holds a borrowed view. The abandoned tail of a chunk is unused until the slot’s cursor resets. A lock of exactly `chunkSize` fits at cursor 0 because the dedicated test is strict `>`.

`streamChunks` is the high-water mark for that slot. A frame that needs a second 64 MiB chunk keeps it. Both slots have their own vectors (`VkContext.h:171`). Wrapping slot 0 does not allocate into slot 1’s vector.

Item-ring and indirect-ring exhaustion (`VkContext.cpp:1061`, `VkContext.cpp:1076`) falls back to streaming (`CGxDeviceVulkan.cpp:1486`). That traffic is what fills the stream ring. The wrap still appends a chunk. It does not allocate a 2.75 MiB pool per lock.

## Two frames in flight

`FramesInFlight` is 2 (`VkContext.h:131`). `EndFrame` and `FlushUploads` advance `m_frameIndex` (`VkContext.cpp:926`, `VkContext.cpp:786`). `AbandonFrame` does not (`VkContext.cpp:932`). Each slot has its own uniform ring, item ring, indirect ring, staging chunks, stream chunks, descriptor pools, and garbage (`VkContext.h:99`).

`PrepareFrame` waits on that slot’s fence, then `Collect`s, then resets cursors (`VkContext.cpp:707`). Slot 1’s `PrepareFrame` does not wait on slot 0. Slot 0’s chunks stay published to the GPU until slot 0 is prepared again. A shared host allocation tags ranges by slot and recycles a range on that slot’s successful wait. The timeout path (`fenceUnsignaled`, 100 ms) skips `Collect` and still zeros the cursors (`VkContext.cpp:708`). Ranges and garbage for that slot stay put across that timeout.

`Draw` reads the `VkBuffer` and `m_index` (`CGxDeviceVulkan.cpp:1262`) and records them with `vkCmdBindVertexBuffers` (`CGxDeviceVulkan.cpp:1445`). The one stream pool per target (`CGxDevice.cpp:1063`) may point at the other slot after the next lock. The recorded buffer is the chunk or the dedicated allocation, still alive via `streamChunks` or `VkGarbage`.

The uniform overflow guard refuses a draw that would reuse another draw’s matrices (`CGxDeviceVulkan.cpp:1381`). The ring is 32 MiB per slot (`VkContext.h:132`). `ISceneBegin` clears `m_uniformsValid` after `BeginFrame` (`CGxDeviceVulkan.cpp:561`, `CGxDeviceVulkan.cpp:535`). The new slot writes its own ring. A clean `memcmp` on a later draw binds `m_uniformOffset` into the set created for this slot (`CGxDeviceVulkan.cpp:1377`, `CGxDeviceVulkan.cpp:1441`). That offset belongs to this slot’s `uniforms` buffer.

Static pools are one buffer shared by both slots. Today `CreateBuffer` maps host-visible coherent memory for every buffer (`VkContext.cpp:1001`). A device-local static upload writes through a staging copy, retires that copy on the uploading slot’s garbage, and leaves the device-local `VkBuffer` in place while either slot’s fence is pending. `GxPoolUsage_Dynamic` still locks the host map (`CGxDeviceVulkan.cpp:1643`). It stays on that map.

`IReleasePool` skips `RetireBuffer` when `borrowed` is set (`CGxDeviceVulkan.cpp:1560`). `RetireBuffer` drops a borrowed view without queueing it (`VkContext.cpp:1166`). The spare list and `vkFreeMemory` see whole buffers only.

## Spare buffer cap

`Collect` parks a retired buffer on `m_spareBuffers` when it is mapped and its usage is exactly `VERTEX | INDEX` (`VkContext.cpp:1213`). The caps are 512 buffers and 32 MiB (`VkContext.h:174`). The loop drops from the front until the newcomer fits, then pushes (`VkContext.cpp:1219`). A newcomer larger than 32 MiB empties the list and is destroyed (`VkContext.cpp:1224`). `AcquireBuffer` hands out an exact `size` and `usage` match (`VkContext.cpp:970`).

Ring chunks are 64 MiB and live on the frame, so they miss this list. One retired `VERTEX | INDEX` buffer above 32 MiB, or a ring chunk routed through `RetireBuffer` by mistake, wipes every spare and then `vkFreeMemory`s itself. The next static `IPoolBuffer` calls `vkAllocateMemory` again (`CGxDeviceVulkan.cpp:1546`, `VkContext.cpp:979`).

Staging temps use `TRANSFER_SRC` (`VkContext.cpp:1147`), so they are destroyed after the fence (`VkContext.cpp:1229`). Device-local static buffers have a null map; they take that same destroy path. The spare list is the host-visible exact-size cache. Slices of a suballocation are not entries in it.

Both slots push into one list. A spare is taken only after the retiring slot’s fence, because `Collect` sits behind that wait (`VkContext.cpp:712`). `DestroyDevice` collects each slot, then destroys whatever remains (`VkContext.cpp:662`).

## Image heap full

`VkHeap` blocks are 64 MiB, or the request when the request is larger (`VkContext.cpp:41`, `VkContext.h:56`). There is no software cap. Full means `vkAllocateMemory` failed (`VkContext.cpp:51`) or `FindMemoryType` found no `DEVICE_LOCAL` type (`VkContext.cpp:1112`). The failed attempt pushes no block. Existing freelists stay as they were.

`CreateImage` destroys the unpublished `VkImage` and returns 0 (`VkContext.cpp:1114`). A bind or view failure frees the span immediately, before any frame samples it (`VkContext.cpp:1130`). `ITexCreate` leaves `m_apiSpecificData` null (`CGxDeviceVulkan.cpp:1762`), so `m_needsCreation` stays set and `Draw` uses the default texture or skips the draw (`CGxDeviceVulkan.cpp:1337`). Swapchain depth failure fails `CreateSwapchain` (`VkContext.cpp:560`).

`ITexFramebuffer` retires the old offscreen depth, then allocates the new one (`CGxDeviceVulkan.cpp:1979`). `RetireImage` only queues garbage (`VkContext.cpp:1186`). `m_heap.Free` runs in `Collect` (`VkContext.cpp:1239`). The old span is still reserved during the grow, and it stays reserved until this slot’s fence. The other slot may still sample the old view. Block indices inside `VkGarbage.alloc` (`VkContext.h:44`, `VkContext.h:15`) stay valid for that wait. `m_blocks` is append-only today (`VkContext.cpp:59`).

`Free` merges both neighbors and `emplace` keeps an existing offset (`VkContext.cpp:64`). `Alloc` returns the alignment gap `[freeStart, start)` and the tail after the request (`VkContext.cpp:28`). A size-class or intrusive list keeps those spans: the stored `VkAllocation.size` is the span removed, the gap and the tail are free, and a later `Free` of either neighbor joins them. Two slots free into the same block in any order.
