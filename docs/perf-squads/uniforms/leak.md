# Campaign 2 — Leak

## Reset

Each of the two frame slots owns a 32 MiB uniform ring (`UniformRingSize` at `Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:132`, created in `VkContext::CreateDeviceResources`). `VkContext::PrepareFrame` runs at the start of every frame, after `vkWaitForFences` on **this** slot. It then resets that slot’s descriptor cache and ring cursor:

- `vkResetDescriptorPool` on every pool in `frame.descriptorPools` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:714`)
- `frame.descriptorPoolCursor = 0` (`VkContext.cpp:715`)
- `frame.sets.clear()` (`VkContext.cpp:716`)
- `frame.uniformOffset = 0` (`VkContext.cpp:717`)

The `VkBuffer` stays. The reset is the cursor and the descriptor-set cache, not a free of the 32 MiB allocation. The other slot’s ring and sets stay in flight until that slot’s fence signals. `FlushFrame` waits, then calls `PrepareFrame` again (`VkContext.cpp:944`), so the same reset happens mid-scene on readback.

`m_uniforms`, `m_uniformOffset`, and `m_uniformsValid` live on the device, not on the frame. `IInvalidateCmdState` clears `m_uniformsValid` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:535`) from `ISceneBegin` (`CGxDeviceVulkan.cpp:589`) and after `FlushFrame` (`CGxDeviceVulkan.cpp:2229`).

## Dirty-bit skip

A dirty-bit skip must not overwrite, rewind, wrap, or free in-flight uniform memory.

Recorded draws already hold a dynamic offset into the current ring (`vkCmdBindDescriptorSets` at `CGxDeviceVulkan.cpp:1442`). Those bytes stay live until this slot’s fence signals. A clean skip may bind the offset written into **this** frame’s ring after the last cursor reset. It must not:

- roll `frame.uniformOffset` backward or `memcpy` over a slot an earlier draw in this command buffer still addresses
- write the other frame’s `uniforms.mapped` while that fence is unsignaled
- keep `m_uniformsValid` or `m_uniformOffset` across `PrepareFrame` / `FlushFrame` and skip the first upload of the new ring
- destroy or shrink the 32 MiB ring when the cursor and descriptor cache reset

A set handle from before `vkResetDescriptorPool` is dead. A conditional bind must not reuse `m_boundSet` across that reset (`IInvalidateCmdState` clears it at `CGxDeviceVulkan.cpp:529`).

## Overflow guard

`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1381`

When the next aligned `VkDrawUniforms` would pass `frame.uniforms.size`, `Draw` returns. It does not wrap the ring onto another draw’s matrices. A dirty-bit path keeps that refusal.
