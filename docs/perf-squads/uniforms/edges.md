# Campaign 2 edges — per-draw uniform pack

Dirty bits may skip `IBuildUniforms` only after a successful copy of those same 768 bytes into **this frame's** ring. `vkCmdBindDescriptorSets` may be skipped only when **this command buffer** already has that descriptor set and that dynamic offset. `VkDrawUniforms` stays 768 bytes (`CGxDeviceVulkan.h:46`, `Shaders/FfpUniforms.glsl:1-22`).

`Draw` calls `IRsSync` before the pack (`CGxDeviceVulkan.cpp:1198`). That clears every render-state `m_dirty` (`CGxDevice.cpp:1245-1265`). Vulkan `IRsSendToHw` writes only texgen (`CGxDeviceVulkan.cpp:1048-1051`). Fog, lights, materials, and texture stage ops are read afterwards from `m_appRenderStates` and `m_lights` (`CGxDeviceVulkan.cpp:971-1045`). `IXformTex` clears the tex-stack dirty bits during the pack (`CGxDeviceVulkan.cpp:967-968`), before the ring `memcpy`. World and projection `m_dirty` are not cleared there, so they are not a generation count.

The packed record also carries draw mode and the vertex-color mask. `misc[1]` is `m_gpuDrawMode` (`CGxDeviceVulkan.cpp:1045`), set by `DrawInstanced` (`CGxDeviceVulkan.cpp:1496`) and `DrawTerrainBatch` (`CGxDeviceVulkan.cpp:1532`) and consumed as `u.misc.y` (`Shaders/Ffp.vert:57-77`). `flags[0]` and `flags[1]` follow `m_primVertexMask` (`CGxDevice.cpp:773-776`, `CGxDeviceVulkan.cpp:996-1035`). Neither call is an `Xform*`, an `RsSet`, or a light update.

| Case | Upload | Bind |
| --- | --- | --- |
| First draw of a frame | Required. Copy into the rewound ring. | Required. The command buffer has no set. |
| Ring cannot hold the next record | Do not copy, bind, or draw. Leave the latch dirty. | |
| Texture change, same 768 bytes | Keep the current offset. | Required. New set, same dynamic offset. |
| Light enable toggle | Required while `GxRs_Lighting` is on. Disabled slots stay zero. | Required when the offset changes. |
| Fog-only change | Required. Matrices and the image key can match. | Required when the offset changes. The set may be the same. |
| `PrepareFrame` cleared the set cache | Required while `m_uniformsValid` is clear. | Required. Allocate a set after the clear. |

## First draw of a frame

`ScenePresent` ends the frame, then `ISceneBegin` (`CGxDeviceVulkan.cpp:710-715`). `EndFrame` clears `prepared` and advances `m_frameIndex` (`VkContext.cpp:923-926`, `FramesInFlight` is 2 at `VkContext.h:131`). `BeginFrame` calls `PrepareFrame` (`VkContext.cpp:789-790`), which sets `frame.uniformOffset = 0` and drops the set cache (`VkContext.cpp:714-717`). It then begins a new command buffer (`VkContext.cpp:812-820`).

`ISceneBegin` then calls `IInvalidateCmdState` (`CGxDeviceVulkan.cpp:586-589`). That nulls `m_boundSet` and `m_uniformsValid` (`CGxDeviceVulkan.cpp:527-535`). It does not clear `m_uniformOffset` or `m_uniforms` (`CGxDeviceVulkan.h:116-118`). Those still name the previous slot.

The first `Draw` uploads because `m_uniformsValid` is clear, before `memcmp` (`CGxDeviceVulkan.cpp:1377-1389`). A static camera often issues no `Xform*`, `RsSet`, or light call between frames. The CPU record matches `m_uniforms` and the upload is still required: the live bytes belong to the other `VkFrame::uniforms` buffer. Binding the surviving `m_uniformOffset` reads this slot's leftover from two frames ago.

`DeviceReadPixels` is a second start inside the scene. `FlushFrame` abandons the command buffer, calls `PrepareFrame` (cursor 0, empty set map, new `vkBeginCommandBuffer`), then `IInvalidateCmdState` runs (`VkContext.cpp:944-958`, `CGxDeviceVulkan.cpp:2228-2229`). The next draw uploads and binds again. An early return before the `memcpy` (`CGxDeviceVulkan.cpp:1194`, `1305-1307`, `1317-1318`, `1346-1354`, `1370-1371`) leaves `m_uniformsValid` clear, so the later successful draw still fills the ring.

## Uniform ring wrap

The ring is 32 MiB (`VkContext.h:132`). Each record is 768 bytes. `minUniformBufferOffsetAlignment` is a power of two and at most 256, and 768 is a multiple of 256, so a successful copy advances the cursor by exactly 768 (`CGxDeviceVulkan.cpp:1378-1386`).

When the aligned offset plus 768 does not fit, `Draw` returns immediately (`CGxDeviceVulkan.cpp:1381-1383`). That return is the whole overflow path: the cursor stays put, `m_uniforms` and `m_uniformOffset` stay on the last successful record, and there is no bind and no draw. The comment on that return is the guard Campaign 6 keeps. A later draw in this command buffer still reads the earlier records. Wrapping the cursor to 0 would overwrite them.

Identical records already share one slot (`CGxDeviceVulkan.cpp:1377`). Wrap is a count of distinct uploads, not of draws. A dirty-bit skip that still fails the fit check has to leave the latch dirty. Marking the refused record clean makes the next draw with that state take the skip path and bind the previous record's offset. That is the reuse the guard refuses.

The return stays before `vkCmdBindDescriptorSets` (`CGxDeviceVulkan.cpp:1442`).

## Texture change with the same matrices

Stage color and alpha ops become `-1` when the `GxRs_Texture0/1` pointer is null (`CGxDeviceVulkan.cpp:983-985`). Swapping one non-null texture for another leaves those ops, the texgen matrices, and world/view/proj unchanged, so `memcmp` of the 768 bytes skips the upload and `m_uniformOffset` stays put (`CGxDeviceVulkan.cpp:1377-1389`).

The set key is the `VkImageView` and the `VkSampler` (`CGxDeviceVulkan.cpp:1350-1351`, `CGxDeviceVulkan.cpp:1399-1400`). The fragment shader samples `tex0`/`tex1` (`Shaders/Ffp.frag:36-41`). The bind still runs: new set, same dynamic offset. A bind that waits for a new upload draws the new batch with the previous images.

`disableTexStage` runs after `IBuildUniforms` (`CGxDeviceVulkan.cpp:1361-1365`). A non-null pointer whose `VkTexture*` is missing packs real stage ops, then forces them to `-1`, and the view falls back to `m_defaultTexture` (`CGxDeviceVulkan.cpp:1329-1343`). The upload decision is the packed record, after that patch. The bind decision is the views and samplers.

`TexSetFlags` / `TexSetWrap` keep the same pointer and force the texture render state (`CGxDevice.cpp:1270-1304`). The sampler key changes (`CGxDeviceVulkan.cpp:1149-1156`). The 768 bytes can match. The set does not.

A recreated `VkImageView` on the same `CGxTex*` does not pass through `RsSet` (`CGxDevice.cpp:1344-1356`). Today's bind notices it by comparing `m_setViews` (`CGxDeviceVulkan.cpp:1399`). The view compare stays; a render-state dirty bit does not see it.

Binding 0 is a dynamic uniform buffer (`CGxDeviceVulkan.cpp:280`). One set covers every offset in the frame. Same images and a new matrix is the opposite case: same set, new offset, bind still required.

## Light enable toggle

`LightEnable` writes `m_enable` and `m_flags |= 0x1` only (`CGxDevice.cpp:798-804`). `LightSet` assigns the api light and can patch the direction (`CGxDevice.cpp:1048-1057`, `CGxLight.cpp:19-71`). Neither is an `RsSet` or an `Xform*`.

`IBuildUniforms` ignores `m_flags`. With `GxRs_Lighting` set it copies each enabled light and `continue`s past a disabled one (`CGxDeviceVulkan.cpp:1002-1026`). The slot stays zero because of the `memset` at `CGxDeviceVulkan.cpp:972`. The shader skips a light when `lightDiffuse[i].w == 0` and otherwise adds it (`Shaders/Ffp.vert:91-116`). Enabling a light must publish position, diffuse, ambient, and attenuation from the current `m_lights` entry, including a `LightSet` that happened while it was disabled. Disabling it must zero that slot, including `lightDiffuse[i].w`.

Turning `GxRs_Lighting` off takes the other branch (`CGxDeviceVulkan.cpp:1027-1033`). Slots are not written. `memset` zeros them, and `flags[0]` is either 0 or 1 with `ambient = (1,1,1,1)` when the batch has no vertex color and `matDiffuse != 0xFFFFFFFF`. An incremental record that only patches `flags[0]` leaves `lightDiffuse[i].w == 1`, and that unlit tint path still adds the old lights.

While `GxRs_Lighting` is already off, the pack leaves the light slots zero, so a pure enable toggle leaves the 768 bytes unchanged. Changing `GxRs_Lighting` does change them. `MasterEnableSet(GxMasterEnable_Lighting)` force-dirties `GxRs_Lighting` (`CGxDevice.cpp:1309-1315`) with the light array untouched. `flags[0]` on the lit path is `m_appMasterEnables & 0x1` (`CGxDeviceVulkan.cpp:1003`).

`PrimVertexMask` changes `hasColor` with no light call (`CGxDevice.cpp:773-776`). That selects `flags[1]` and the unlit ambient branch (`CGxDeviceVulkan.cpp:996-1035`).

D3D still consumes `m_flags` and then clears them (`CGxDeviceD3d.cpp:1401-1448`). Vulkan does not read `m_flags`. The latch stays on `CGxDeviceVulkan`. Clearing `m_flags` from the pack changes the D3D frame.

## Fog-only change

The pack writes `flags[2]`, `fogColor`, and `fogParams[0..1]` on every build (`CGxDeviceVulkan.cpp:1036-1041`). Inputs are `GxRs_FogStart`, `GxRs_FogEnd`, `GxRs_FogColor`, `GxRs_Fog` (`Engine/Source/Gx/Types.h:269-273`), and master-enable bit `0x2`. `MasterEnableSet(GxMasterEnable_Fog)` force-dirties `GxRs_Fog` even when that state's value is unchanged (`CGxDevice.cpp:1317-1318`). `flags[2]` is the master bit and `GxRs_Fog` (`CGxDeviceVulkan.cpp:1036`).

The vertex shader writes `outFog` from `flags.z` and `fogParams`, and the fragment shader mixes `fogColor` by that factor (`Shaders/Ffp.vert:124`, `Shaders/Ffp.frag:48`). With fog off, `outFog` is 1 and the mix keeps the shaded color. With fog on, a start, end, or color change is the whole shaded result. Interior draws (Ironforge) use this same record.

A fog-only edit does not change matrices, light slots, or the image/sampler key. `IRsSync` has already cleared the fog `m_dirty` bits before `IBuildUniforms`, and `IRsSendToHw` ignores them (`CGxDeviceVulkan.cpp:1048-1051`). The latch is taken where `RsSet` / `MasterEnableSet` run, and it survives `IRsSync`. The upload still happens. The descriptor set can stay. The dynamic offset changes, so the bind still happens.

## Descriptor set cache cleared in `PrepareFrame`

`PrepareFrame` resets every pool on the slot, clears `frame.sets`, and zeroes `frame.uniformOffset` (`VkContext.cpp:714-717`). `CachedSet` misses after that and allocates (`VkContext.cpp:767-778`). The first hit in the frame has `created == 1` and writes binding 0 to **this** frame's `frame.uniforms.buffer` at range 768 (`CGxDeviceVulkan.cpp:1408-1433`). A set allocated last frame points at the other slot's buffer. Dynamic offsets into that set read the wrong ring.

`PrepareFrame` does not touch `m_boundSet`. `IInvalidateCmdState` nulls it (`CGxDeviceVulkan.cpp:529`) on the scene-begin path and after `FlushFrame`. The CPU image key is often unchanged across that boundary. The bind condition includes a null `m_boundSet` (`CGxDeviceVulkan.cpp:1399`), which is what forces the lookup. A key compare alone rebinds a handle `vkResetDescriptorPool` has freed.

Later draws in the same frame get `created == 0` and skip `vkUpdateDescriptorSets`. They still bind when the set or the dynamic offset differs. `AllocateSet` returns null once the slot has 16 pools (`VkContext.cpp:749-750`). That `Draw` returns before the bind (`CGxDeviceVulkan.cpp:1404-1405`) and leaves `m_boundSet` unchanged, so the failed key is not cached as current.

`PrepareFrame` returns immediately when `frame.prepared` is already set (`VkContext.cpp:703-705`). `UploadCmd` and `AllocDrawItems` can prepare the slot before `ISceneBegin` (`VkContext.cpp:825-826`, `VkContext.cpp:1057`). `IInvalidateCmdState` still runs before `m_frameActive` is set (`CGxDeviceVulkan.cpp:586-589`), and `Draw` bails while the frame is inactive (`CGxDeviceVulkan.cpp:1194`). The first real draw still sees a null `m_boundSet` and a clear `m_uniformsValid`.
