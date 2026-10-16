# Campaign 2 Faith — per-draw uniform pack

**Verdict.** The dirty-bit pack is acceptable. A clean bit may skip `IBuildUniforms` and the 768-byte upload. It may bind `m_uniformOffset` only for the record already copied into the current frame ring. Anything else reuses another draw's matrices. That fails this campaign.

`CGxMatrixStack::m_dirty` and `CGxAppRenderState::m_dirty` stay the D3D / GL sync flags. The uniform bit is a separate flag. D3D9, OpenGL, and the shader-on early-outs keep their current results. `GpuDrawItem` field order stays put.

## Invariants

`VkDrawUniforms` is 768 bytes. `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h:46` is the `static_assert`. `Tools/spirv-pin-compare.py:38` derives the same size from `FfpUniforms.glsl` and rejects a block that is not 768 (`Tools/spirv-pin-compare.py:75`).

Field order matches `Engine/Source/Gx/CGxDeviceVulkan/Shaders/FfpUniforms.glsl:2`. The C struct is `CGxDeviceVulkan.h:22`. std140, row-major, 16-byte aligned members, no padding between them:

| Offset | GLSL (`FfpUniforms.glsl`) | C (`VkDrawUniforms`) |
| --- | --- | --- |
| 0 | `mat4 world` | `world` |
| 64 | `mat4 view` | `view` |
| 128 | `mat4 proj` | `proj` |
| 192 | `mat4 texMatrix[2]` | `texMatrix[2]` |
| 320 | `vec4 matDiffuse` | `matDiffuse[4]` |
| 336 | `vec4 matEmissive` | `matEmissive[4]` |
| 352 | `vec4 ambient` | `ambient[4]` |
| 368 | `vec4 lightPos[4]` | `lightPos[4][4]` |
| 432 | `vec4 lightDiffuse[4]` | `lightDiffuse[4][4]` |
| 496 | `vec4 lightAmbient[4]` | `lightAmbient[4][4]` |
| 560 | `vec4 lightAtten[4]` | `lightAtten[4][4]` |
| 624 | `vec4 fogColor` | `fogColor[4]` |
| 640 | `vec4 fogParams` | `fogParams[4]` |
| 656 | `ivec4 stageColorOp` | `stageColorOp[4]` |
| 672 | `ivec4 stageAlphaOp` | `stageAlphaOp[4]` |
| 688 | `ivec4 stageTexGen` | `stageTexGen[4]` |
| 704 | `ivec4 stageTexMode` | `stageTexMode[4]` |
| 720 | `ivec4 stageCoord` | `stageCoord[4]` |
| 736 | `ivec4 flags` | `flags[4]` |
| 752 | `vec4 misc` | `misc[4]` |

`IBuildUniforms` (`CGxDeviceVulkan.cpp:971`) is the only packer. It reads the live stacks and render state, then `Draw` may still patch stage ops (`CGxDeviceVulkan.cpp:1361`).

## Clean bit and the ring

`Draw` uploads when `m_uniformsValid` is clear or the 768 bytes differ (`CGxDeviceVulkan.cpp:1377`). The overflow guard is `CGxDeviceVulkan.cpp:1381`: if the aligned slot does not fit in `frame.uniforms`, `Draw` returns at `CGxDeviceVulkan.cpp:1383` before `vkCmdBindDescriptorSets` (`CGxDeviceVulkan.cpp:1442`). The comment at `CGxDeviceVulkan.cpp:1382` is the rule. A draw that cannot upload its own record does not bind the previous record.

A clean bit replaces the `memcmp`. It does not replace `!m_uniformsValid`, and it does not replace the size return.

Reuse of `m_uniformOffset` is valid only when all of these hold:

- The uniform bit is clear, and every writer in the lists below has set it since the last successful upload.
- `m_uniformsValid` is set. `IInvalidateCmdState` clears it (`CGxDeviceVulkan.cpp:535`). `ISceneBegin` calls that after the frame begins (`CGxDeviceVulkan.cpp:589`). `DeviceReadPixels` calls it after `FlushFrame` (`CGxDeviceVulkan.cpp:2229`).
- `VkContext::PrepareFrame` has not zeroed `frame.uniformOffset` since that upload (`VkContext.cpp:717`). The device offset from the previous use of this frame slot is another draw's matrices in this frame's ring.

`m_uniforms` starts zero and `m_uniformsValid` starts clear (`CGxDeviceVulkan.h:116`). The first draw packs. `CGxDevice::IRsInit` (`CGxDevice.cpp:1391`) and the constructor's `InitLights` (`CGxDevice.cpp:1594`) plus `m_appMasterEnables = 0x1FF` (`CGxDevice.cpp:1595`) publish the initial fog, texture-stage, and light record before any draw. That first pack is what the clear valid flag is for.

`IXformTex` clears `m_dirty` on the texture xform and texgen stacks while packing (`CGxDeviceVulkan.cpp:967`). Vulkan never clears world or view `m_dirty`. Those flags are not the uniform bit. A clean skip that trusts them either rebuilds forever (world stays dirty) or treats a texture matrix written after the clear as clean.

`Ffp.vert:57` selects matrices from `u.misc.y` (`misc[1]`, written from `m_gpuDrawMode` at `CGxDeviceVulkan.cpp:1045`). Mode 0 uses `u.world` and `u.texMatrix`. Mode 1 replaces world from `GpuDrawItem`. Mode 2 also replaces tex matrices and lights (`Ffp.vert:66`). A clean record whose `misc.y` still names the previous mode uses the previous draw's matrices.

`Draw`'s `ResetGpuDraw` sets `m_gpuDrawMode` back to 0 when `Draw` returns (`CGxDeviceVulkan.cpp:1187`), after the pack. The next fixed-function draw has to see a dirty bit. `CGxDeviceVulkan::DrawInstanced` sets mode 1 before `Draw` (`CGxDeviceVulkan.cpp:1496`). `CGxDeviceVulkan::DrawTerrainBatch` sets mode 2 (`CGxDeviceVulkan.cpp:1532`). On alloc failure, `DrawInstanced` falls through to `CGxDevice::DrawInstanced` (`CGxDeviceVulkan.cpp:1487`), which writes the world stack per instance.

`Draw` also forces `stageColorOp` / `stageAlphaOp` to -1 when the bound texture has no `VkTexture` (`CGxDeviceVulkan.cpp:1323` and `CGxDeviceVulkan.cpp:1361`). That patch runs on the local record after `IBuildUniforms`. A clean path applies the same patch to the record it reuses, or sets the bit when a bound stage-0 or stage-1 texture loses `m_apiSpecificData`. The functions that drop it are `CGxDeviceVulkan::ITexRelease` (`CGxDeviceVulkan.cpp:1926`, clear at `CGxDeviceVulkan.cpp:1952`), `ITexForceRecreation` (`CGxDeviceVulkan.cpp:1955`), `TexDestroy` (`CGxDeviceVulkan.cpp:2049`), and `DeviceSetBaseMipLevel` (`CGxDeviceVulkan.cpp:2054`).

View and projection still come from the uniform when `misc.y` is non-zero (`Ffp.vert:81`). Instanced and terrain draws stay on the matrix, fog, and stage lists.

`XformSetView` force-updates texgen when the mode is object-linear or eye-linear (`CGxDevice.cpp:1452`). A world-stack write does not. `ISetTexGen` is not added to world stores. Object-linear texgen keeps the matrix from the last texgen sync.

Stages 2 and 3 are packed as -1 (`CGxDeviceVulkan.cpp:991`). Texture, op, texgen, shader, and coord state for TMU 2–15 does not change the 768 bytes. TMU 0 and 1 do.

## Matrices

`IBuildUniforms` reads `m_xforms[GxXform_World]`, `m_xforms[GxXform_View]`, and `m_projNative` (`CGxDeviceVulkan.cpp:975`). `IXformTex` (`CGxDeviceVulkan.cpp:934`) reads `m_texGen[tmu]`, `m_xforms[tmu]` for TMU 0 and 1 (`GxXform_Tex0`, `GxXform_Tex1`), `GxRs_TextureShader0/1`, and `GxRs_TexGen0/1`.

These functions write those bytes and must set the uniform bit. A wrapper that only forwards is covered by the callee. A direct `m_mtx` / `m_level` / in-place `Scale` / `Translate` / `*=` store is not covered by `GxXform*`.

| Function | Write |
| --- | --- |
| `GxXformPop` | `Gx.cpp:387`, level at `Gx.cpp:392` |
| `GxXformTranslate` | `Gx.cpp:398`, `Translate` at `Gx.cpp:402` |
| `GxXformScale` | `Gx.cpp:407`, `Scale` at `Gx.cpp:411` |
| `GxXformSet` | `Gx.cpp:1019`, assign at `Gx.cpp:1023` |
| `CGxDevice::XformPush` | `CGxDevice.cpp:641`, copy at `CGxDevice.cpp:649`. At level cap the visible matrix becomes the previous level. |
| `CGxDevice::XformPushAndSet` | `CGxDevice.cpp:704`, assign at `CGxDevice.cpp:710` |
| `CGxDevice::XformSetView` | `CGxDevice.cpp:1446`, assign at `CGxDevice.cpp:1450` |
| `CGxDeviceVulkan::XformSetProjection` | `CGxDeviceVulkan.cpp:780`, `m_projNative` at `CGxDeviceVulkan.cpp:811`. Base `CGxDevice::XformSetProjection` (`CGxDevice.cpp:635`) writes `m_projection` only. The pack reads `m_projNative`. |
| `CGxDevice::DrawInstanced` | `CGxDevice.cpp:127`, per-world assign at `CGxDevice.cpp:138`, restore at `CGxDevice.cpp:143` |
| `CGxDeviceVulkan::ISetTexGen` | `CGxDeviceVulkan.cpp:898`, `Identity` at `CGxDeviceVulkan.cpp:919` or assign at `CGxDeviceVulkan.cpp:926`. Runs from `IRsSendToHw` (`CGxDeviceVulkan.cpp:1048`) during `Draw`'s `IRsSync`, before the pack. The bit is still set when `IBuildUniforms` runs. |
| `CGxMatrixStack::Identity` | `CGxMatrixStack.cpp:6`, elements at `CGxMatrixStack.cpp:13`. The flag test at `CGxMatrixStack.cpp:7` returns without writing. |

`CGxMatrixStack::Identity` is the store for these callers. They have no other matrix write, so the bit has to be inside `Identity`:

| Caller | Call |
| --- | --- |
| `CWorldScene::RenderChunks` | `World.cpp:3812` |
| `CWorldScene::RenderHorizon` | `World.cpp:5074` |
| `DayNight::DNPlanet::Render` | `DayNightRender.cpp:130` |
| `DayNight::DNClouds::Render` | `DayNightRender.cpp:214` (then `GxXformScale`) |
| `DayNight::DNGlare::GetQueryOcclusion` | `DayNight.cpp:3126` |
| `FFX::SetupGx` | `FFX.cpp:615` |
| `CShaderEffect::SetTexMtx_Identity` | `ShaderEffect.cpp:374` |
| `CShaderEffect::SetTexMtx_SphereMap` | `ShaderEffect.cpp:388` |

Direct stores, each must set the bit on its own write:

| Function | Write |
| --- | --- |
| `CM2SceneRender::SetupBillboardView` | `CM2SceneRender.cpp:153` |
| `CM2SceneRender::EndFixed` | `CM2SceneRender.cpp:165` |
| `CM2SceneRender::DrawBatch` | `CM2SceneRender.cpp:479`, restore at `CM2SceneRender.cpp:488` |
| `CM2SceneRender::DrawBatchProj` | `CM2SceneRender.cpp:585` |
| `CM2SceneRender::DrawRibbon` | `CM2SceneRender.cpp:842` |
| `CM2SceneRender::DrawParticle` | `CM2SceneRender.cpp:1022` |
| `CM2SceneRender::Draw` | identity at `CM2SceneRender.cpp:1071`, level pop for Tex0 through World at `CM2SceneRender.cpp:1157` |
| `CWorldScene::RenderDetailDoodads` | `World.cpp:3604` |
| `CWorldScene::SetWorldProjection` | `World.cpp:4310` |
| `CDetailDoodad::InitializeRenderState` | `DetailDoodad.cpp:785` |
| `CMapObj::RenderMapObjGroupsShadowMap` | `MapObj.cpp:3158` |
| `CMapObj::RenderMapObjGroupsColor` | `MapObj.cpp:3243` |
| `CGUnit_C::RenderMissileTrajectoryTargetingTexture` | `UnitMissileTrajectory_C.cpp:650` |
| `WorldTextRenderBatch` | assign at `WorldText.cpp:545`, level pop at `WorldText.cpp:561` |
| `PLAYERNAMEDESC::Render` | assign at `PlayerName.cpp:460`, level pop at `PlayerName.cpp:473` |
| `CLightning::Render` | `Lightning.cpp:859` |
| `CLightningManager::Render` | `Translate` at `Lightning.cpp:938`, world pop at `Lightning.cpp:970`, tex0 pop at `Lightning.cpp:977` |
| `CShaderEffect::SetTexMtx` | `ShaderEffect.cpp:406` |
| `CShaderEffect::SetTexMtx_TexGen` | `ShaderEffect.cpp:442` |
| `DayNight::DNSky::Render` | `Identity` at `DayNightRender.cpp:164` can no-op; `Scale` at `DayNightRender.cpp:170` is the write that must mark |
| `CGMinimapFrame::RenderInsideTexture` | `Identity` can no-op; `*=` at `MinimapFrame.cpp:3150` must mark |

`GxXformSetProjection` (`Gx.cpp:292`), `GxXformSetView` (`Gx.cpp:298`), `GxXformPush` (`Gx.cpp:381` and `Gx.cpp:627`) only forward. Viewport (`Gx.cpp:568`, `CGxDevice::XformSetViewport` at `Gx.cpp:525`) is dynamic state, not this record.

`CGxDeviceVulkan::MasterEnableSet` is not overridden. `GxMasterEnable_NormalProjection` changes `m_projNative` only on the next `XformSetProjection` (`CGxDeviceVulkan.cpp:802`). The enable write alone does not mark for projection.

## Lights

`IBuildUniforms` reads `GxRs_Lighting`, `m_appMasterEnables` bit 0 (`GxMasterEnable_Lighting`), and `m_lights[0..3]` (`m_enable`, `m_dir`, `m_dirColor`, `m_ambColor`, the three attenuations) at `CGxDeviceVulkan.cpp:1002`. The unlit fallback and `flags[1]` also read `GxRs_MatDiffuse`, `GxRs_MatEmissive`, `GxRs_ColorMaterial`, and the color-0 bit of `m_primVertexMask` (`CGxDeviceVulkan.cpp:996` and `CGxDeviceVulkan.cpp:1035`).

| Function | Write |
| --- | --- |
| `CGxDevice::LightSet` | `CGxDevice.cpp:1048`, assign at `CGxDevice.cpp:1050` |
| `CGxDevice::LightEnable` | `CGxDevice.cpp:798`, `m_enable` at `CGxDevice.cpp:803` |
| `CGxDevice::InitLights` | `CGxDevice.cpp:715`, `m_enable = 0` at `CGxDevice.cpp:719`. Vulkan calls it from `IStateSetDefaults` (`CGxDeviceVulkan.cpp:521`) after the device can already have a valid record. |
| `CGxDevice::MasterEnableSet` | `CGxDevice.cpp:1309`. Lighting is case 0 at `CGxDevice.cpp:1313`. The pack reads `m_appMasterEnables`, not the `IRsForceUpdate` side effect. |
| `CGxDevice::PrimVertexMask` | `CGxDevice.cpp:773`. Changes `flags[0]`, `flags[1]`, and the white-ambient fallback. |
| `GxRsSet` / `CGxDevice::RsSet` | When `which` is `GxRs_Lighting`, `GxRs_MatDiffuse`, `GxRs_MatEmissive`, or `GxRs_ColorMaterial`. Stores are listed under texture stage; those four values use the same functions. |

`CGxDevice::Light` (`CGxDevice.cpp:1998`) copies out of `m_lights`. It does not mark.

These callers are covered only because `LightSet` / `LightEnable` mark: `CM2Light::GxLightSet` (`M2Light.cpp:133`), `CM2Lighting::SetupGxLights` (`M2Light.cpp:328`), `GxuLightApplyImpl` (`GxuLight.cpp:226`).

## Fog

The pack reads master-enable bit 1 (`GxMasterEnable_Fog`), `GxRs_Fog`, `GxRs_FogColor`, `GxRs_FogStart`, and `GxRs_FogEnd` (`CGxDeviceVulkan.cpp:1036`).

| Function | Write |
| --- | --- |
| `CGxDevice::MasterEnableSet` | Fog is case 1 at `CGxDevice.cpp:1317`. |
| `GxRsSet(int)` | `Gx.cpp:894`, store at `Gx.cpp:903`. Fog enable and fog color. |
| `GxRsSet(float)` | `Gx.cpp:925`, store at `Gx.cpp:934`. Fog start and fog end. |
| `CGxDevice::RsSet(int)` | `CGxDevice.cpp:1953`, store at `CGxDevice.cpp:1962` |
| `CGxDevice::RsSet(float)` | `CGxDevice.cpp:1967`, store at `CGxDevice.cpp:1976` |
| `CGxDevice::RsPop` | `CGxDevice.cpp:1369`, restore at `CGxDevice.cpp:1381` |
| `CGxDevice::IRsInit` | Defaults at `CGxDevice.cpp:1404`. Constructor only (`CGxDevice.cpp:1578`); the clear `m_uniformsValid` covers that call. |

`CM2Lighting::SetupGxFog` (`M2Light.cpp:422`) only calls `GxRsSet`. The bit is on `GxRsSet`, not on `SetupGxFog` alone.

## Texture stage

For TMU 0 and 1 the pack reads the texture pointer (null disables the stage), color op, alpha op, texgen, texture coord (`CGxDeviceVulkan.cpp:983`), and texture-shader mode inside `IXformTex` (`CGxDeviceVulkan.cpp:939`). `ISetTexGen` then writes `m_texGen`.

`GxRsSet(int)` and `GxRsSet(float)` store the value themselves. They do not call `CGxDevice::RsSet`. `GxRsSet(void*)` (`Gx.cpp:888`) does forward. `g_theGxDevicePtr->RsSet(...)` hits the methods. Both sides mark.

| Function | Write |
| --- | --- |
| `GxRsSet(int)` | `Gx.cpp:894`, store at `Gx.cpp:903`. Color op, alpha op, texgen, texture shader, texture coord for TMU 0 and 1. |
| `GxRsSet(void*)` | Forwards to `CGxDevice::RsSet(void*)`. |
| `CGxDevice::RsSet(void*)` | `CGxDevice.cpp:1344`, pointer at `CGxDevice.cpp:1356`. `GxRs_Texture0` and `GxRs_Texture1`. |
| `CGxDevice::RsSet(int)` | `CGxDevice.cpp:1953`, store at `CGxDevice.cpp:1962` |
| `CGxDevice::RsSet(float)` | `CGxDevice.cpp:1967`, store at `CGxDevice.cpp:1976` |
| `CGxDevice::RsPop` | `CGxDevice.cpp:1381`. Restores whatever was pushed, including stage 0/1 and fog. |
| `CGxDeviceVulkan::ISetTexGen` | `CGxDeviceVulkan.cpp:898`. Texgen matrix for the TMU `IBuildUniforms` will sample. |
| `CGxDeviceVulkan::Draw` | Stage-op patch at `CGxDeviceVulkan.cpp:1361` when `ITexReady` fails. |

`CGxDevice::RsInit` (`CGxDevice.cpp:2099`) only calls `RsSet`. `CGMinimapFrame::RenderInsideTexture` calls it at `MinimapFrame.cpp:3136`. The bit is on `RsSet`.

`CGxDevice::IRsInit` writes the stage defaults at `CGxDevice.cpp:1418`. Constructor only.

`GxUpdateAlphaRef` writes `GxRs_AlphaRef` without `RsSet` (`Gx.cpp:909`, store at `Gx.cpp:919`). The pack stores it in `flags[3]` (`CGxDeviceVulkan.cpp:1037`). That function marks. `GxRs_PointScale` is `misc[0]` (`CGxDeviceVulkan.cpp:1043`) and marks through `GxRsSet(float)` / `CGxDevice::RsSet(float)`.

`CGxDevice::RsSet(const C3Vector&)` (`CGxDevice.cpp:1981`) is point-scale attenuation. The pack does not read it.

Sampler flags, descriptor image views, blend, depth, cull, scissor, and viewport are rebuilt in `Draw` outside the 768-byte record. They do not set this bit.
