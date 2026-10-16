# Campaign 2 — Layout

Skip `IBuildUniforms` when the 768-byte record already matches this draw, and call `vkCmdBindDescriptorSets` when the descriptor set or the dynamic offset on **this command buffer** changes. `VkDrawUniforms` stays **768** bytes (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h:46`, `Shaders/FfpUniforms.glsl:1-22`). `Tools/spirv-pin-compare.py` stays on that size and on `GpuDrawItem` at 448. No field is added to either shader record.

Two slices. Slice 1 is the pack latch and the upload rule. Slice 2 is the bind record. The structures below are the end state.

`IBuildUniforms` (`CGxDeviceVulkan.cpp:971-1045`) is one full rebuild. Any latched input rebuilds all 768 bytes, including the `memset` at `CGxDeviceVulkan.cpp:972`. A patch of `flags[0]` alone leaves `lightDiffuse[i].w` from the previous light.

## Why the existing flags are the wrong latch

| Flag | What happens to it | Consequence |
| --- | --- | --- |
| `CGxAppRenderState::m_dirty` | `Draw` calls `IRsSync` at `CGxDeviceVulkan.cpp:1198`, which clears it at `CGxDevice.cpp:1262` | The pack at `CGxDeviceVulkan.cpp:1358` never sees it. `IRsSendToHw` (`CGxDeviceVulkan.cpp:1048-1051`) writes texgen only. |
| `CGxMatrixStack::m_dirty` on Tex0/Tex1 and `m_texGen` | `IXformTex` clears it at `CGxDeviceVulkan.cpp:967-968` | `ResolveTexMatrix` (`CGxDeviceVulkan.cpp:929-931`) clears those bits before `Draw`. World and projection `m_dirty` are never cleared on this device, so they stay 1 after the first write. |
| `CGxApiLight::m_flags` | Set by `operator=` and `LightEnable`. Vulkan never reads them. D3D consumes them and then clears them (`CGxDeviceD3d.cpp:1401-1448`) | The pack leaves `m_flags` unchanged. |

The latch is a new word on the Vulkan device. Writers OR bits into it. `IXformTex`, `IRsSync`, and the light pack do not clear it.

## Device fields

Owner is `CGxDeviceVulkan`. Lifetime is the device. No heap, no per-draw array.

| Field | Type | Role |
| --- | --- | --- |
| `m_uniformDirty` | `uint32_t` | Content latch. Initializer `~0u`, so the first draw builds. |
| `m_packedDisable` | `int32_t[2]` | `disableTexStage` from the last record that landed in **this** frame's ring. Initializer `{-2, -2}`. |
| `m_cmdSet` | `VkDescriptorSet` | Set bound on the current command buffer. Initializer `VK_NULL_HANDLE`. |
| `m_cmdOffset` | `VkDeviceSize` | Dynamic offset of that bind. Initializer `~VkDeviceSize{0}`. |

They sit beside `m_uniforms`, `m_uniformOffset`, and `m_uniformsValid` (`CGxDeviceVulkan.h:116-118`). Those three stay. `m_uniforms` is the last 768 bytes that were committed to a ring. `m_uniformsValid` means that copy lives in **this** frame's `VkFrame::uniforms` at `m_uniformOffset`.

`m_cmdSet` / `m_cmdOffset` are slice 2. Slice 1 leaves the bind at `CGxDeviceVulkan.cpp:1442` unconditional.

### Bit values

Declared on `CGxDevice` so `Gx.cpp` and the game stores can name them without including the Vulkan header.

| Bit | Value | Packed fields |
| --- | ---: | --- |
| `UniformDirty_World` | `1 << 0` | `uniforms.world` from `m_xforms[GxXform_World]` |
| `UniformDirty_View` | `1 << 1` | `uniforms.view` |
| `UniformDirty_Proj` | `1 << 2` | `uniforms.proj` from `m_projNative` |
| `UniformDirty_Tex0` | `1 << 3` | `uniforms.texMatrix[0]`, `stageTexMode[0]` |
| `UniformDirty_Tex1` | `1 << 4` | `uniforms.texMatrix[1]`, `stageTexMode[1]` |
| `UniformDirty_TexGen0` | `1 << 5` | `m_texGen[0]` consumed by `IXformTex` |
| `UniformDirty_TexGen1` | `1 << 6` | `m_texGen[1]` |
| `UniformDirty_Rs` | `1 << 7` | Stage ops, materials, fog, `flags`, `misc[0]` |
| `UniformDirty_Light` | `1 << 8` | `lightPos`, `lightDiffuse`, `lightAmbient`, `lightAtten`, and the lit `flags[0]` / `ambient` path |
| `UniformDirty_Vertex` | `1 << 9` | `m_primVertexMask` bit `GxVA_Color0` (`flags[0]` unlit branch, `flags[1]`) |
| `UniformDirty_GpuDraw` | `1 << 10` | `misc[1]` from `m_gpuDrawMode` |

Tex2–Tex7, the projection **stack** matrix, and `m_texGen[2..7]` are not packed (`IBuildUniforms` loops `tmu < 2`). Stores to those stacks do not OR a bit.

### Hook

`CGxDevice::INoteUniformDirty(uint32_t bits)` is a new virtual, appended after `DeviceUnk83` (`CGxDevice.h:261`). The base body is empty. `CGxDeviceVulkan` ORs `bits` into `m_uniformDirty`. D3D and GL keep the empty base. No existing vtable slot moves. No field is inserted into `CGxDevice` (that would shift the D3D member offsets).

`CGxMatrixStack::Identity` (`CGxMatrixStack.cpp:6-30`) has no device pointer and stays signature-compatible. It is not a latch writer. Each call site on a consumed stack is.

## Pack gate

In `CGxDeviceVulkan::Draw`, after the texture loop has filled `disableTexStage`, `views`, and `samplers` (`CGxDeviceVulkan.cpp:1321-1356`), and after the `IBeginPass` failure return (`CGxDeviceVulkan.cpp:1370-1372`).

```cpp
int32_t build = this->m_uniformDirty
    || disableTexStage[0] != this->m_packedDisable[0]
    || disableTexStage[1] != this->m_packedDisable[1];

VkDrawUniforms local;
if (build) {
    this->IBuildUniforms(local);
    // existing disableTexStage patch, CGxDeviceVulkan.cpp:1361-1365
} else {
    local = this->m_uniforms;
}

int32_t upload = !this->m_uniformsValid || memcmp(&local, &this->m_uniforms, sizeof(VkDrawUniforms));
if (upload) {
    // align, then the ring-full return at CGxDeviceVulkan.cpp:1381-1383
    // on success: memcpy, advance frame.uniformOffset, set m_uniformOffset
}
```

A successful commit is either the `memcpy` or a path where `upload` is 0 (`m_uniformsValid` and the 768 bytes match). The commit stores `local` into `m_uniforms` when `build` is set, sets `m_uniformsValid`, copies `disableTexStage` into `m_packedDisable`, and sets `m_uniformDirty` to 0.

The ring-full return (`CGxDeviceVulkan.cpp:1381-1383`) happens before that commit. It leaves `m_uniformDirty`, `m_uniforms`, `m_uniformOffset`, `m_uniformsValid`, `m_packedDisable`, `m_cmdSet`, and `m_cmdOffset` as they were, and it does not bind or draw. The next draw with that state still builds.

`IInvalidateCmdState` (`CGxDeviceVulkan.cpp:527-535`) keeps clearing `m_uniformsValid` and `m_boundSet`. It also sets `m_cmdSet` to null and `m_cmdOffset` to `~VkDeviceSize{0}`. It leaves `m_uniformDirty` and `m_uniforms` alone. Callers stay `ISceneBegin` (`CGxDeviceVulkan.cpp:589`) and the readback path (`CGxDeviceVulkan.cpp:2229`). The first draw of a frame whose latch is clear uploads `m_uniforms` into the rewound ring (`VkContext.cpp:717`) because `m_uniformsValid` is clear. A static camera that issues no writer between frames still uploads once.

`disableTexStage` is computed in `Draw` from a null `VkTexture*` (`CGxDeviceVulkan.cpp:1329-1339`). It is not an `RsSet`. The compare against `m_packedDisable` is what republishes stage ops when a bound texture's GPU object appears or disappears. `ITexCreate` (`CGxDeviceVulkan.cpp:1797`) and `ITexRelease` (`CGxDeviceVulkan.cpp:1952`) do not OR the latch.

Early returns before the commit (`CGxDeviceVulkan.cpp:1194`, `1305-1307`, `1317-1318`, `1346-1354`, `1370-1372`, `1404-1405`) leave the latch set.

## Bind gate

Slice 2. After the existing set update (`CGxDeviceVulkan.cpp:1399-1438`), including the `AllocateSet` failure return at `CGxDeviceVulkan.cpp:1404-1405` which leaves `m_boundSet` unchanged:

```cpp
if (this->m_boundSet != this->m_cmdSet || this->m_uniformOffset != this->m_cmdOffset) {
    uint32_t dynamicOffset = static_cast<uint32_t>(this->m_uniformOffset);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, this->m_pipelineLayout, 0, 1, &this->m_boundSet, 1, &dynamicOffset);
    this->m_cmdSet = this->m_boundSet;
    this->m_cmdOffset = this->m_uniformOffset;
}
```

The view and sampler `memcmp` (`CGxDeviceVulkan.cpp:1399`) stays. A new `VkImageView` on the same `CGxTex*` does not pass through `RsSet`. A sampler change from `TexSetFlags` / `TexSetWrap` (`CGxDevice.cpp:1270-1304`) keeps the 768 bytes and still changes the set. `IBeginPass` does not clear `m_cmdSet`: descriptor binds survive a render-pass boundary in the same command buffer.

`PrepareFrame` resets the pool and clears `frame.sets` (`VkContext.cpp:714-716`). The set handle from the previous use of this slot is dead. `IInvalidateCmdState` clearing `m_cmdSet` is what forces the lookup. A key compare alone would rebind a freed handle.

## Render states that latch `UniformDirty_Rs`

`IBuildUniforms` reads these and no other `EGxRenderState`:

| State | Enum |
| --- | ---: |
| `GxRs_MatDiffuse`, `GxRs_MatEmissive` | 1, 2 |
| `GxRs_AlphaRef` | 7 |
| `GxRs_FogStart`, `GxRs_FogEnd`, `GxRs_FogColor` | 8, 9, 10 |
| `GxRs_Lighting`, `GxRs_Fog` | 11, 12 |
| `GxRs_Texture0`, `GxRs_Texture1` | 21, 22 |
| `GxRs_ColorOp0`, `GxRs_ColorOp1` | 37, 38 |
| `GxRs_AlphaOp0`, `GxRs_AlphaOp1` | 45, 46 |
| `GxRs_TexGen0`, `GxRs_TexGen1` | 53, 54 |
| `GxRs_TextureShader0`, `GxRs_TextureShader1` | 61, 62 |
| `GxRs_TextureCoord0`, `GxRs_TextureCoord1` | 69, 70 |
| `GxRs_PointScale` | 79 |
| `GxRs_ColorMaterial` | 85 |

Blend, depth, cull, color write, shaders, polygon offset, scissor, specular, point-scale attenuation, and every stage above 1 stay off this list. They still go through `IRsDirty` for the hardware sync list. They do not OR `m_uniformDirty`.

`GxRs_Texture0` and `GxRs_Texture1` latch only when null-ness changes (`old == nullptr` differs from `new == nullptr`). Swapping one non-null texture for another leaves stage ops alone (`CGxDeviceVulkan.cpp:983-985`). The bind gate still sees the new view.

`MasterEnableSet` for `GxMasterEnable_Lighting` (0) and `GxMasterEnable_Fog` (1) changes `flags[0]` and `flags[2]` while the render-state value stays put (`CGxDeviceVulkan.cpp:1003`, `CGxDeviceVulkan.cpp:1036`). Those two enables latch when the stored bit changes. `GxMasterEnable_NormalProjection` does not rewrite `m_projNative`; the next `XformSetProjection` does.

`IRsForceUpdate` (`CGxDevice.cpp:1207`, `CGxDevice.cpp:1228`) is not a latch writer. `TexSetFlags` / `TexSetWrap` keep calling it so the sampler sync still runs. `XformSetView`'s force-update of texgen (`CGxDevice.cpp:1452-1455`) is covered by the view bit plus `ISetTexGen` during `IRsSync`, which runs before the pack gate.

## Light rule

`LightEnable` and `LightSet` OR `UniformDirty_Light` only when `m_appRenderStates[GxRs_Lighting]` is already on **and** a packed field changes. Packed fields are `m_enable`, `m_dir` (xyzw), `m_dirColor`, `m_ambColor`, `m_constantAttenuation`, `m_linearAttenuation`, `m_quadraticAttenuation`. Specular (`CGxLight.cpp:64-68`, flag `0x10`) is not packed.

While `GxRs_Lighting` is off, the pack leaves the light slots zero (`CGxDeviceVulkan.cpp:1027-1033`). An enable or `LightSet` in that state does not change the 768 bytes. Turning `GxRs_Lighting` on is the `UniformDirty_Rs` writer, and the rebuild reads the current `m_lights`.

`m_flags` is not cleared.

## Writers

Every row ORs its bit through `INoteUniformDirty`. A store that is inside one of these functions is not its own writer. `GxXformSet` / `GxXformPop` / `GxXformTranslate` / `GxXformScale` / `XformSetView` / `XformPushAndSet` / `XformSetProjection` / `RsSet` / `GxRsSet` / `LightSet` / `LightEnable` cover their callers.

`XformPush` (`CGxDevice.cpp:641-651`) copies the current top. The visible matrix does not change. It does not OR a bit. The matching pop does, when the level actually drops.

`CGxMatrixStack::Identity` (`CGxMatrixStack.cpp:29`) sets `m_dirty` only. The call sites below are the latch writers.

### Xform functions

| Writer | Line | Bit |
| --- | --- | --- |
| `CGxDevice::DrawInstanced` store `m_mtx = worlds[i]` | `CGxDevice.cpp:138` | `UniformDirty_World` |
| `CGxDevice::DrawInstanced` restore `m_mtx = saved` | `CGxDevice.cpp:143` | `UniformDirty_World` |
| `CGxDevice::XformPushAndSet` store | `CGxDevice.cpp:710` | bit for `xf` when `xf` is Tex0, Tex1, World, or View |
| `CGxDevice::XformSetView` store | `CGxDevice.cpp:1450` | `UniformDirty_View` |
| `GxXformPop` inside `if (stack.m_level)` | `Gx.cpp:392` | bit for `xf` when `xf` is Tex0, Tex1, World, or View. Level 0 skips the OR |
| `GxXformTranslate` | `Gx.cpp:402` | same xf filter |
| `GxXformScale` | `Gx.cpp:411` | same xf filter |
| `GxXformSet` store | `Gx.cpp:1023` | same xf filter. `GxXform_Projection` is not this bit |
| `CGxDeviceVulkan::XformSetProjection` store of `m_projNative` | `CGxDeviceVulkan.cpp:811` | `UniformDirty_Proj`. This is the only store of `m_projNative` |
| `CGxDeviceVulkan::ISetTexGen` `Identity()` when `tmu < 2` | `CGxDeviceVulkan.cpp:919` | `UniformDirty_TexGen0` or `UniformDirty_TexGen1` |
| `CGxDeviceVulkan::ISetTexGen` store when `tmu < 2` | `CGxDeviceVulkan.cpp:926` | same |

A 16-float compare before the OR is allowed on `GxXformSet`, `XformSetView`, `XformPushAndSet`, and `XformSetProjection`. When all 16 floats match, that call may skip the OR. When any float differs, the OR is required.

### Direct matrix stores

These assign `m_mtx` or edit it in place. They already set `m_dirty` on a nearby line. That flag is not the latch.

| Writer | Line | Bit |
| --- | --- | --- |
| `CM2SceneRender::SetupBillboardView` | `CM2SceneRender.cpp:153` | `UniformDirty_World` |
| `CM2SceneRender::EndFixed` | `CM2SceneRender.cpp:165` | `UniformDirty_World` |
| `CM2SceneRender::DrawBatch` bone matrix | `CM2SceneRender.cpp:479` | `UniformDirty_World` |
| `CM2SceneRender::DrawBatch` restore | `CM2SceneRender.cpp:488` | `UniformDirty_World` |
| `CM2SceneRender::DrawBatchProj` restore | `CM2SceneRender.cpp:585` | `UniformDirty_World` |
| `CM2SceneRender::DrawRibbon` restore | `CM2SceneRender.cpp:842` | `UniformDirty_World` |
| `CM2SceneRender::DrawParticle` restore | `CM2SceneRender.cpp:1022` | `UniformDirty_World` |
| `CM2SceneRender::Draw` identity | `CM2SceneRender.cpp:1071` | `UniformDirty_World` |
| `CM2SceneRender::Draw` pop, inside `if (stack.m_level)` | `CM2SceneRender.cpp:1157` | Tex0, Tex1, or World when `xf` is that stack. Tex2–Tex7 in this loop do not OR |
| `CWorldScene` detail doodad world | `World.cpp:3604` | `UniformDirty_World` |
| `CWorldScene::SetWorldProjection` | `World.cpp:4310` | `UniformDirty_World` |
| `CDetailDoodad` tex1 | `DetailDoodad.cpp:785` | `UniformDirty_Tex1` |
| `DayNight::DNSky::Render` `Scale` | `DayNightRender.cpp:170` | `UniformDirty_World` |
| `CMapObj::RenderMapObjGroupsShadowMap` | `MapObj.cpp:3158` | `UniformDirty_World` |
| `CMapObj::RenderMapObjGroupsColor` | `MapObj.cpp:3243` | `UniformDirty_World` |
| `CGMinimapFrame::RenderInsideTexture` `*=` | `MinimapFrame.cpp:3150` | `UniformDirty_World` |
| `UnitMissileTrajectory` tex0 | `UnitMissileTrajectory_C.cpp:650` | `UniformDirty_Tex0` |
| `WorldText` store | `WorldText.cpp:545` | `UniformDirty_World` |
| `WorldText` pop, inside the `m_level` decrement | `WorldText.cpp:561` | `UniformDirty_World` |
| `PlayerName` store | `PlayerName.cpp:460` | `UniformDirty_World` |
| `PlayerName` pop, inside the `m_level` decrement | `PlayerName.cpp:473` | `UniformDirty_World` |
| `CLightning::Render` tex0 | `Lightning.cpp:859` | `UniformDirty_Tex0` |
| `CLightningManager::Render` `Translate` | `Lightning.cpp:938` | `UniformDirty_World` |
| `CLightningManager::Render` world pop, inside the decrement | `Lightning.cpp:970` | `UniformDirty_World` |
| `CLightningManager::Render` tex0 pop, inside the decrement | `Lightning.cpp:977` | `UniformDirty_Tex0` |
| `CShaderEffect::SetTexMtx` store when `stage < 2` | `ShaderEffect.cpp:406` | Tex0 or Tex1 |
| `CShaderEffect::SetTexMtx_TexGen` store when `stage < 2` | `ShaderEffect.cpp:442` | Tex0 or Tex1 |

### `Identity()` call sites

| Writer | Line | Bit |
| --- | --- | --- |
| `CWorldScene` loop `m_xforms[i].Identity()`, `i < 5` | `World.cpp:3812` | `UniformDirty_Tex0` when `i == 0`, `UniformDirty_Tex1` when `i == 1`. `i >= 2` does not OR |
| `CWorldScene` tex0 identity | `World.cpp:5074` | `UniformDirty_Tex0` |
| `DayNight` billboard world identity | `DayNightRender.cpp:130` | `UniformDirty_World` |
| `DayNight::DNSky::Render` | `DayNightRender.cpp:164` | `UniformDirty_World` |
| `DayNight::DNClouds::Render` | `DayNightRender.cpp:214` | `UniformDirty_World` |
| `DayNight` sky world identity | `DayNight.cpp:3126` | `UniformDirty_World` |
| `CShaderEffect::SetTexMtx_Identity` when `stage < 2` | `ShaderEffect.cpp:374` | Tex0 or Tex1 |
| `CShaderEffect::SetTexMtx_SphereMap` when `stage < 2` | `ShaderEffect.cpp:388` | Tex0 or Tex1 |
| `FFX::SetupGx` | `FFX.cpp:615` | `UniformDirty_World` |
| `CGMinimapFrame::RenderInsideTexture` | `MinimapFrame.cpp:3147` | `UniformDirty_World` |

`MinimapFrame.cpp:3148` sets stack `m_dirty` between the identity and the `*=` at `3150`. Both `3147` and `3150` OR `UniformDirty_World`. `DayNightRender.cpp:168` sets stack `m_dirty` before the `Scale` at `170`; the OR is on `170`.

### Render-state writers

`IRsDirty` (`CGxDevice.cpp:1188`) ORs `UniformDirty_Rs` when `which` is in the consumed table and is not `GxRs_Texture0` or `GxRs_Texture1`. These callers already reach it and do not OR a second time:

| Caller | Line |
| --- | --- |
| `CGxDevice::RsSet(void*)` | `CGxDevice.cpp:1355` |
| `CGxDevice::RsSet(int32_t)` | `CGxDevice.cpp:1961` |
| `CGxDevice::RsSet(float)` | `CGxDevice.cpp:1975` |
| `CGxDevice::RsSet(C3Vector)` | `CGxDevice.cpp:1989` |
| `GxRsSet(int32_t)` | `Gx.cpp:902` |
| `GxUpdateAlphaRef` | `Gx.cpp:918` |
| `GxRsSet(float)` | `Gx.cpp:933` |

Texture null-ness is a separate OR of `UniformDirty_Rs` at the store, because `IRsDirty` runs before the new pointer is visible and a non-null swap must not rebuild:

| Store | Lines | Rule |
| --- | --- | --- |
| `CGxDevice::RsSet(void*)` | `CGxDevice.cpp:1351` old, `1356` new | `GxRs_Texture0` or `GxRs_Texture1`, null-ness differs |
| `CGxDevice::RsSet(int32_t)` | `CGxDevice.cpp:1960` old, `1962` new | same |
| `GxRsSet(int32_t)` | `Gx.cpp:901` old, `903` new | same. This path does not call `CGxDevice::RsSet` |

`RsPop` (`CGxDevice.cpp:1381`) does not call `IRsDirty`. For each popped state in the consumed table, OR `UniformDirty_Rs` when the restored value differs. For `GxRs_Texture0` and `GxRs_Texture1`, OR only when null-ness differs.

`MasterEnableSet` (`CGxDevice.cpp:1310`) ORs `UniformDirty_Rs` when `state` is `GxMasterEnable_Lighting` or `GxMasterEnable_Fog` and the new bit differs from the old one. The force-update at `CGxDevice.cpp:1314` and `CGxDevice.cpp:1318` stays for the hardware list.

### Light writers

| Writer | Line | When |
| --- | --- | --- |
| `CGxDevice::LightEnable` | `CGxDevice.cpp:803` | The existing `m_enable != enable` branch, and `GxRs_Lighting` is on |
| `CGxDevice::LightSet` | `CGxDevice.cpp:1050` through `1056` | After `operator=` and the position subtract, a packed field differs from the pre-call light, and `GxRs_Lighting` is on. `operator=` (`CGxLight.cpp:19-71`) is the compare for attenuation (`21`, `26`, `31`), direction (`36`), diffuse (`51`), and ambient (`58`). Specular at `CGxLight.cpp:65` does not latch. The position subtract at `CGxDevice.cpp:1054-1056` is part of the same compare |
| `CGxDevice::InitLights` | `CGxDevice.cpp:719` | Always. During `CGxDevice::CGxDevice` the virtual still hits the empty base; `m_uniformDirty`'s initializer `~0u` covers that. `IStateSetDefaults` (`CGxDeviceVulkan.cpp:521`) runs on the finished Vulkan object and does latch |

`CGxDevice::Light` (`CGxDevice.cpp:1998`) reads `m_lights`. It is not a writer.

### Vertex color and draw mode

| Writer | Line | When |
| --- | --- | --- |
| `CGxDevice::PrimVertexMask` | `CGxDevice.cpp:775` | `(mask ^ previous) & (1 << GxVA_Color0)` |
| `CGxDeviceVulkan::DrawInstanced` | `CGxDeviceVulkan.cpp:1496` | `m_gpuDrawMode` becomes 1 |
| `CGxDeviceVulkan::DrawTerrainBatch` | `CGxDeviceVulkan.cpp:1532` | `m_gpuDrawMode` becomes 2 |
| `Draw`'s `ResetGpuDraw` destructor | `CGxDeviceVulkan.cpp:1187` | `m_gpuDrawMode` was not 0. This runs after the pack, so it latches the **next** draw. The draw that just committed already packed the non-zero mode |

`misc[1]` is `m_gpuDrawMode` (`CGxDeviceVulkan.cpp:1045`). `DrawInstanced` and `DrawTerrainBatch` are not `Xform*` or `RsSet` calls. The destructor assigns 0 on every `Draw`; the OR happens only when the mode was non-zero, otherwise every draw would latch the next one.

## Closed set

Searches behind this list: every `m_mtx[` store, every `m_xforms[].Identity` and `m_texGen[].Identity`, every matrix-stack `m_level--`, the only `m_projNative` store, `LightEnable` / `LightSet` / `InitLights` / `operator=`, and the `RsSet` / `GxRsSet` / `RsPop` / `MasterEnableSet` choke points. A later store of world, view, `m_projNative`, tex0, tex1, or `m_texGen[0..1]` that is not in the tables above is a missed latch.

`IXformTex` may keep clearing stack `m_dirty` at `CGxDeviceVulkan.cpp:967-968`. That clear is not the content latch.

New comments in the slice use the repo TextEmoji form.
