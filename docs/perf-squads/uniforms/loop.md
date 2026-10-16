# Campaign 2 loop — uniforms

**NEXT SLICE.** Not closed.

No outdoor log line is in the tree. This call is from the draw path. The budget is uploads and binds following state changes rather than draw count, with `VkDrawUniforms` staying 768 bytes.

| Check | Result | Where |
| --- | --- | --- |
| Uploads follow record changes | Holds | `CGxDeviceVulkan.cpp:1377` |
| Binds follow set or offset changes | Fails | `CGxDeviceVulkan.cpp:1442` |
| `VkDrawUniforms` stays 768 | Holds | `CGxDeviceVulkan.h:46` |

A repeated draw with the same 768 bytes does not `memcpy` (`CGxDeviceVulkan.cpp:1377-1389`). `IInvalidateCmdState` clears `m_uniformsValid` (`CGxDeviceVulkan.cpp:535`) from `ISceneBegin` (`CGxDeviceVulkan.cpp:589`) and from the readback flush (`CGxDeviceVulkan.cpp:2229`). The next draw uploads once into that frame’s ring (`VkContext.cpp:290`, offset reset `VkContext.cpp:717`). That is a ring reset, not a per-draw upload.

`vkCmdBindDescriptorSets` runs for every draw that reaches it (`CGxDeviceVulkan.cpp:1441-1442`). The set cache at `CGxDeviceVulkan.cpp:1399-1438` does not gate the bind. Binds follow draw count. The budget is not met.

`static_assert(sizeof(VkDrawUniforms) == 768)` is in place (`CGxDeviceVulkan.h:46`). `Tools/spirv-pin-compare.py:75` checks the same size against `FfpUniforms.glsl`. Leave both alone.

## Next writer task

**Dirty-bit pack.** One concern. Files: `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h` and `CGxDeviceVulkan.cpp` only.

Skip `IBuildUniforms` (`CGxDeviceVulkan.cpp:1358-1359`) when every input of that pack is clean. A clean draw reuses `m_uniforms` (`CGxDeviceVulkan.h:116`). Leave the upload `if` at `CGxDeviceVulkan.cpp:1377` as it is, so a new frame ring still copies the CPU cache when `m_uniformsValid` is 0. Do not `memcmp` 768 bytes to decide whether to pack. The `memset` at `CGxDeviceVulkan.cpp:972` is the cost this slice removes.

Do not edit `vkCmdBindDescriptorSets` (`CGxDeviceVulkan.cpp:1442`). Conditional bind is the slice after this one.

Do not change `VkDrawUniforms` field order or size, `FfpUniforms.glsl`, or `Tools/spirv-pin-compare.py`. Do not edit `// OFFSET:` functions in `CGxDevice.cpp` or `Gx.cpp`. Missed dirty state is a wrong frame.

### Latch

`Draw` calls `IRsSync` before the pack (`CGxDeviceVulkan.cpp:1198`). `IRsSync` clears `CGxAppRenderState::m_dirty` (`CGxDevice.cpp:1262`). Those bits are already 0 at `CGxDeviceVulkan.cpp:1358`. Latch a `m_uniformDirty` on `CGxDeviceVulkan` from `IRsSendToHw` (`CGxDeviceVulkan.cpp:1048`). `IRsSync` calls that only when the app value differs from the hardware shadow. Keep the `GxRs_TexGen*` → `ISetTexGen` call. Also set `m_uniformDirty` for the states the pack reads:

- `GxRs_Texture0`, `GxRs_Texture1`
- `GxRs_ColorOp0`, `GxRs_ColorOp1`
- `GxRs_AlphaOp0`, `GxRs_AlphaOp1`
- `GxRs_TexGen0`, `GxRs_TexGen1`
- `GxRs_TextureCoord0`, `GxRs_TextureCoord1`
- `GxRs_TextureShader0`, `GxRs_TextureShader1`
- `GxRs_MatDiffuse`, `GxRs_MatEmissive`, `GxRs_ColorMaterial`
- `GxRs_Lighting`, `GxRs_Fog`, `GxRs_AlphaRef`
- `GxRs_FogColor`, `GxRs_FogStart`, `GxRs_FogEnd`
- `GxRs_PointScale`

`MasterEnableSet` force-updates `GxRs_Lighting` and `GxRs_Fog` (`CGxDevice.cpp:1313-1318`), and `IRsForceUpdate` inverts the hardware shadow (`CGxDevice.cpp:1220-1223`), so a lighting or fog master toggle still enters `IRsSendToHw`. The pack reads `m_appMasterEnables` bits 0 and 1 (`CGxDeviceVulkan.cpp:1003`, `CGxDeviceVulkan.cpp:1036`).

Start `m_uniformDirty` at 1. Do not set it in `IInvalidateCmdState`.

### Other inputs, checked at the pack decision

Consume these. Clear them only after a pack that read them.

- Matrix stacks, already set by `GxXformSet` (`Gx.cpp:1021`), `GxXformPop` (`Gx.cpp:389`), `GxXformTranslate` (`Gx.cpp:400`), `GxXformScale` (`Gx.cpp:409`), `XformPush` (`CGxDevice.cpp:651`), `XformPushAndSet` (`CGxDevice.cpp:708`), `XformSetView` (`CGxDevice.cpp:1448`), `CGxMatrixStack::Identity` (`CGxMatrixStack.cpp:29`), `XformSetProjection` (`CGxDeviceVulkan.cpp:810`), and `ISetTexGen` (`CGxDeviceVulkan.cpp:924`). Read `m_dirty` on world, view, projection, tex0, tex1, and `m_texGen[0]` / `m_texGen[1]`.
- `IXformTex` currently clears tex-stack dirty on every call (`CGxDeviceVulkan.cpp:967-968`), including `ResolveTexMatrix` (`CGxDeviceVulkan.cpp:929`). Move those clears to the pack-consumed path. A resolve must not retire the bit.
- Lights. `CGxApiLight::operator=` and `LightEnable` set `m_flags` (`CGxLight.cpp:19`, `CGxDevice.cpp:802`). The pack never reads `m_flags`. Test and clear only the bits it does read: `0x1` enable, `0x2` direction, `0x4` ambient, `0x8` diffuse, `0x20` / `0x40` / `0x80` attenuation. Leave `0x10` (specular). `InitLights` sets `m_flags` to `0xFF` (`CGxDevice.cpp:717`), which forces the first pack.
- Vertex color. `hasColor` is `m_primVertexMask & (1 << GxVA_Color0)` (`CGxDeviceVulkan.cpp:996`). `IStateSetDefaults` sets `m_primVertexDirty` to `0xFFFFFFFF` (`CGxDeviceVulkan.cpp:519`) and nothing on this device clears it, so a sticky test never goes clean. Honor bit `GxVA_Color0` and clear that bit after the pack. `PrimVertexMask` ORs only the changed attribs (`CGxDevice.cpp:774`).
- `m_gpuDrawMode`. Instanced draws set 1 (`CGxDeviceVulkan.cpp:1496`), terrain sets 2 (`CGxDeviceVulkan.cpp:1532`), and `ResetGpuDraw` stores 0 on the way out (`CGxDeviceVulkan.cpp:1187`). Compare `float(m_gpuDrawMode)` to `m_uniforms.misc[1]` (`CGxDeviceVulkan.cpp:1045`). Do not dirty on the stores themselves, or the reset-to-zero makes every following draw rebuild.
- Texture-not-ready. After `IBuildUniforms`, a failed texture forces `stageColorOp` / `stageAlphaOp` to -1 (`CGxDeviceVulkan.cpp:1361-1366`). Remember the last `disableTexStage[2]`. If this draw’s flags differ, pack again. A texture that becomes ready is not an `RsSet`.

### Still open after this slice

Conditional descriptor bind. Record the bound dynamic offset next to `m_boundSet` (`CGxDeviceVulkan.h:120`). Call `vkCmdBindDescriptorSets` only when the set or `m_uniformOffset` changes. The first draw of a scene still binds: `IInvalidateCmdState` nulls `m_boundSet` (`CGxDeviceVulkan.cpp:529`) and `PrepareFrame` resets descriptor pools (`VkContext.cpp:714-716`). Pin-compare stays a check, not a code change. The loop after the bind slice can close if that bind is gated and `VkDrawUniforms` is still 768.
