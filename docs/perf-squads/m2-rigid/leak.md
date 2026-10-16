# Leak — Campaign 1, rigid doodads

`DrawRigidDoodads` (`Engine/Source/Model2/CM2SceneRender.cpp:608`) returns after `DrawInstanced` has recorded the draw. The GPU runs that command at submit, so the bytes it samples have to stay unmodified until this frame slot’s fence signals.

## Still live after return

Three copies. The CPU objects that filled them are done.

**Instance matrices.** `CGxDeviceVulkan::DrawInstanced` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1491`) copies each `C44Matrix` into `GpuDrawItem.world` in `frame.items` and passes the slot as `firstInstance` (`CGxDeviceVulkan.cpp:1496`). `Ffp.vert:67` reads `gpu.items[gl_InstanceIndex]` while `u.misc.y != 0` (`CGxDeviceVulkan.cpp:1045`, `Shaders/Ffp.vert:66`). Those `total` records stay live until the fence.

If `AllocDrawItems` fails, the base loop (`Engine/Source/Gx/CGxDevice/CGxDevice.cpp:135`) writes each matrix onto the world stack and `Draw` packs it into the uniform ring (`CGxDeviceVulkan.cpp:978`, `CGxDeviceVulkan.cpp:1376`). That ring is the copy that stays live. `m_gpuDrawMode` stays 0 on this path, so the shader uses `u.world`.

**Vertex slice.** `BufStream` / `BufLock` (`CM2SceneRender.cpp:644`) append into the frame stream ring (`VkContext.cpp:1086`, `StreamRingSize` at `VkContext.h:135`). `Draw` captures `VkBuffer` plus `m_index` (`CGxDeviceVulkan.cpp:1256`, `CGxDeviceVulkan.cpp:1444`). Those bytes stay live until the fence.

**Index slice.** Same ring, second lock (`CM2SceneRender.cpp:667`). `Draw` binds that `VkBuffer` at offset 0 and puts the slice in `firstIndex` as `m_index >> 1` (`CGxDeviceVulkan.cpp:1461`, `CGxDeviceVulkan.cpp:1470`). Those `uint16`s stay live until the fence.

Vertex and index targets are different singletons (`CGxDevice.cpp:1694`, pools at `CGxDevice.cpp:1701`) and one ring. A later lock appends. It does not punch a hole in an earlier slice. `PrepareFrame` is what reclaims the bytes, by setting `itemCursor`, `streamChunk`, and `streamOffset` back to 0 (`VkContext.cpp:718`).

A lock bigger than `StreamRingSize` is a private `VkBuffer` with `borrowed == 0` (`VkContext.cpp:1023`). It is not in `streamChunks`. It sits on `pool->m_apiSpecific` until a later lock `RetireBuffer`s it (`CGxDeviceVulkan.cpp:1626`). `RetireBuffer` drops borrowed views and queues real buffers on `frame.garbage` (`VkContext.cpp:1165`). `Collect` destroys that garbage only after `vkWaitForFences` succeeds (`VkContext.cpp:709`). Device teardown waits idle, then `IReleasePools` (`CGxDeviceVulkan.cpp:356`, `CGxDeviceVulkan.cpp:1556`) before `DestroyDevice` collects.

## Free after return

**`static std::vector<C44Matrix> worlds`** (`CM2SceneRender.cpp:636`). Filled from `m_boneMatrices[bone]` (`CM2SceneRender.cpp:641`), passed as `worlds.data()` (`CM2SceneRender.cpp:691`), and copied before `DrawInstanced` returns. The next entry `clear`s it (`CM2SceneRender.cpp:637`). `reserve(total)` only grows. `clear` leaves capacity alone, so this vector does not shrink every frame. Leave it. No `shrink_to_fit` on the batch path.

The model’s `m_boneMatrices` slot is also done. The draw reads the item-ring or uniform-ring copy.

**The two stream `CGxBuf`s.** `BufStream` returns `m_streamBufs[target]` (`CGxDevice.cpp:1063`). The next lock of that target replaces `pool->m_apiSpecific` and `m_index` (`CGxDeviceVulkan.cpp:1611`). The recorded command already holds the handle and the numeric offset. The lock pointers from `BufLock` are done at `BufUnlock` (`CM2SceneRender.cpp:663`, `CM2SceneRender.cpp:681`).

**D3D9.** `DrawInstanced` is the base loop, so `worlds` is consumed before return. Stream locks use `D3DLOCK_NOOVERWRITE` and `D3DLOCK_DISCARD` (`Engine/Source/Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:141`). The pool object still has to outlive the GPU’s use of that region. `BufStream` grows a short pool and never shrinks it (`CGxDevice.cpp:1066`).

## Holes

**Fence timeout reclaims the copies.** `PrepareFrame` calls `Collect` only when the wait succeeds (`VkContext.cpp:709`). It resets `itemCursor` and the stream cursor either way (`VkContext.cpp:717`). `fenceUnsignaled` makes that wait 100 ms (`VkContext.cpp:708`, set at `VkContext.cpp:901`). A timeout lets the next rigid batch overwrite item records and stream slices a submitted draw is still sampling. Any new recycle has to wait for the same fence.

**Emergency unlock aliases the previous slice.** `StreamLock` failure returns `EmergencyMem` (`CGxDeviceVulkan.cpp:1605`). `BufStream` does not clear `m_index`. `IBufUnlock` then `memcpy`s into `pool->m_apiSpecific` at that stale index (`CGxDeviceVulkan.cpp:1655`). The borrowed view’s `mapped` pointer is the chunk base (`VkContext.cpp:1045`, `CGxDeviceVulkan.cpp:1623`), so the write lands on the previous slice of this frame. `DrawRigidDoodads` treats a non-null lock as success (`CM2SceneRender.cpp:647`). A failed stream lock must not store into the live chunk.

**Index-lock failure abandons the vertex tail.** A null index lock returns 0 (`CM2SceneRender.cpp:670`) after the vertex slice is unlocked and bound (`CM2SceneRender.cpp:663`). No draw references it. `BufLock` sets `m_bufLocked` before the Vulkan lock (`CGxDevice.cpp:233`, `CGxDeviceVulkan.cpp:1674`), and this return skips `BufUnlock`, so the index target stays marked locked. The ring is append-only until the fence. Do not rewind `streamOffset` to reclaim that tail.

## Static bind

The campaign slice that stops rewriting the mesh makes the shared static vertex and index buffers the memory that must outlive the call. `CM2Shared::DestroyBuffers` (`Engine/Source/Model2/M2Shared.cpp:602`) already retires them through `PoolDestroy` → `IReleasePool` → `RetireBuffer`. Keep that path. `DrawBatchDoodad` calls `AllocInstances` (`CM2SceneRender.cpp:711`), which may `DestroyBuffers` (`M2Shared.cpp:779`), and only then calls `DrawRigidDoodads` (`CM2SceneRender.cpp:717`). A bind inside the call does not alias the pools just destroyed. Destroying those buffers after the command is recorded belongs on `frame.garbage` for the frame that recorded the draw.
