# Campaign 6 — Faith

**Verdict: PASS.** The approach stands.

Checked `CGxDeviceVulkan` and `VkContext` against the Campaign 6 must-stay list: frame garbage retirement, the uniform-ring overflow guard, the spare-buffer caps, and the 64 MiB stream ring.

| Invariant | Result | Lines |
| --- | --- | --- |
| Frame garbage retirement | **PASS** | `VkContext.cpp:709`, `VkContext.cpp:1160`, `VkContext.cpp:1198` |
| Uniform ring refuses another draw's matrices | **PASS** | `CGxDeviceVulkan.cpp:1381` |
| Spare-buffer caps | **PASS** | `VkContext.h:174`, `VkContext.cpp:1219` |
| 64 MiB stream ring stays | **PASS** | `VkContext.h:135`, `VkContext.cpp:1089` |

## Frame garbage retirement — PASS

`VkGarbage` lives on the frame (`VkContext.h:35`, `VkContext.h:119`). `Retire` appends to that list (`VkContext.cpp:1160-1162`). `PrepareFrame` calls `Collect` only after `vkWaitForFences` returns success (`VkContext.cpp:709-712`). `DestroyDevice` idles, then collects every frame (`VkContext.cpp:646-650`).

A borrowed stream view is cleared in `RetireBuffer` and is not queued (`VkContext.cpp:1166-1168`), so `Collect` cannot free the ring. `CreateSwapchain` idles at `VkContext.cpp:392` before `DestroySwapchain` destroys depth and framebuffers (`VkContext.cpp:609-627`).

A failed wait leaves `frame.garbage` in place (`VkContext.cpp:709`). Those buffers stay live until a later successful wait, or until `DestroyDevice`.

## Uniform ring — PASS

There is one store, `CGxDeviceVulkan.cpp:1385`. When the aligned cursor plus `sizeof(VkDrawUniforms)` does not fit, `Draw` returns at `CGxDeviceVulkan.cpp:1381-1383`. That return leaves `frame.uniformOffset` where it is, so the ranges already recorded in this command buffer stay intact.

A matching payload binds the existing `m_uniformOffset` (`CGxDeviceVulkan.cpp:1377`, `CGxDeviceVulkan.cpp:1441`). That slot was written earlier in the same frame. `IInvalidateCmdState` clears `m_uniformsValid` (`CGxDeviceVulkan.cpp:535`) from `ISceneBegin` (`CGxDeviceVulkan.cpp:589`), and `Draw` returns unless `m_frameActive` (`CGxDeviceVulkan.cpp:1194`).

Each in-flight slot has its own ring (`VkContext.h:131-132`, `VkContext.cpp:290`). `PrepareFrame` zeros that slot's cursor at `VkContext.cpp:717` after the fence wait, which is the reuse point between submissions on that slot.

## Spare-buffer caps — PASS

`MaxSpareBuffers` is 512 and `MaxSpareBytes` is 32 MiB (`VkContext.h:174-175`). The only `m_spareBuffers.push_back` is `VkContext.cpp:1225`. The loop above it destroys the oldest spare while the count is at the cap or the next buffer would pass the byte cap (`VkContext.cpp:1217-1223`). A buffer that still does not fit is destroyed at `VkContext.cpp:1229-1236`.

## 64 MiB stream ring — PASS

`StreamRingSize` is `64 << 20` (`VkContext.h:135`). `StreamLock` passes that size to `BumpChunk` (`VkContext.cpp:1086-1089`). A full chunk increments `chunkIndex` and sets the cursor to 0 (`VkContext.cpp:1049-1052`). The next chunk is another `StreamRingSize` allocation (`VkContext.cpp:1032-1037`).

`IBufLock` for `GxPoolUsage_Stream` calls `StreamLock` (`CGxDeviceVulkan.cpp:1596-1607`). Ring advance does not go through `PoolSizeSet`. `s_streamVertexPoolSize` remains `0x2C0000` (`CGxDevice.cpp:62`) as the shared `CGxPool` size. `BufStream` calls `PoolSizeSet` only when one lock is larger than that pool (`CGxDevice.cpp:1066-1067`).

A lock larger than one chunk uses `CreateBuffer(bytes)` (`VkContext.cpp:1023-1028`). That is the oversized lock, and the chunk size for locks that fit stays 64 MiB.
