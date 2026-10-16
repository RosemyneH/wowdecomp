# Campaign 2 counters — per-draw uniforms

Counters and `OsGetAsyncTimeClocks` zones only. No behavior change. Increments are relaxed `uint64_t` atomics behind the Campaign 0 compile-out flag (default off). Campaign 0 prints the one-second line; these field names stay as written here.

`CGxDeviceVulkan::Draw` (`Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.cpp:1183`) calls `IBuildUniforms` on every draw that reaches the pack (`1359`). That function `memset`s 768 bytes (`971:972`; `sizeof(VkDrawUniforms)`, `Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h:46`) and then packs matrices, lights, fog, and texture stages (`978:1045`). `vkCmdBindDescriptorSets` runs on every draw that reaches `1442`, with the current set and dynamic offset, including when both are unchanged from the previous draw.

`IBuildUniforms` has one caller (`1359`). `DrawInstanced` (`1499`) and `DrawTerrainBatch` (`1538`) each enter `Draw` once. One instanced or indirect command is one `draws` sample.

## Counters

| Field | Increments when | Site |
| --- | --- | --- |
| `uniform_uploads` | The 768-byte `memcpy` into the uniform ring runs | `CGxDeviceVulkan.cpp:1385` |
| `uniform_memcmp_fail` | `memcmp` of those 768 bytes returns non-zero | `CGxDeviceVulkan.cpp:1377` |
| `descriptor_binds` | `vkCmdBindDescriptorSets` runs | `CGxDeviceVulkan.cpp:1442` |
| `draws` | One draw command is recorded | `CGxDeviceVulkan.cpp:1467`, `1470`, `1474` |

### `uniform_uploads`

The copy is `memcpy(frame.uniforms.mapped + offset, &uniforms, sizeof(VkDrawUniforms))` at `1385`, after the aligned cursor fits (`1378:1381`). Each sample is 768 bytes. The ring is `VkContext::UniformRingSize` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.h:132`, `32 << 20`). `VkContext::PrepareFrame` zeros `frame.uniformOffset` (`Engine/Source/Gx/CGxDeviceVulkan/VkContext.cpp:717`).

The branch that can reach the copy opens at `1377`: `!m_uniformsValid`, or a failed `memcmp`. The ring-full return at `1383` happens before the `memcpy`. That attempt is an upload miss: no `uniform_uploads` sample.

### `uniform_memcmp_fail`

`||` at `1377` evaluates `memcmp(&uniforms, &this->m_uniforms, sizeof(VkDrawUniforms))` only when `m_uniformsValid` is already set. A non-zero return is one `uniform_memcmp_fail`, including when `1383` then returns without a copy.

`m_uniformsValid == 0` takes the same branch and skips `memcmp`. `IInvalidateCmdState` clears it (`535`), from `ISceneBegin` (`589`) and from the readback flush (`2229`). The first draw after that clear can upload with `uniform_memcmp_fail` left unchanged.

Evaluate `memcmp` once and use that result in the existing `if`. The counter reads the result.

### `descriptor_binds`

Increment at `1442`, where `dynamicOffset` is `m_uniformOffset` (`1441`) and the set is `m_boundSet`. The view/sampler check at `1399` can skip `vkUpdateDescriptorSets` (`1408:1433`). It does not guard `1442`. The bind still runs when the set and the dynamic offset are the same as the previous draw.

### `draws`

| Line | Command | Path |
| --- | --- | --- |
| `1467` | `vkCmdDrawIndexedIndirect` | `count != 0` and `m_indirectCount != 0` |
| `1470` | `vkCmdDrawIndexed` | `count != 0` and `m_indirectCount == 0` |
| `1474` | `vkCmdDraw` | `count == 0` |

One increment per call. `1470` passes `instances` from `1469`, which `DrawInstanced` may set above 1 (`1496:1498`). `1467` records `m_indirectCount` indexed commands in that single call (`1532:1538`).

## Order inside one `Draw`

Samples follow the call. A return stops the list:

1. Pack at `1359` (`IBuildUniforms`, `memset` at `972`).
2. Stage-disable stores at `1361:1366` (may set `stageColorOp` / `stageAlphaOp` to `-1`). These bytes are part of the record `memcmp` sees.
3. `!m_passActive` returns at `1370`.
4. Compare / upload at `1377:1389`, or the ring-full return at `1383`.
5. `CachedSet` failure returns at `1404`.
6. `descriptor_binds` at `1442`.
7. Index-buffer returns at `1452` and `1458` (`count != 0`). A bind already happened.
8. One of `draws` at `1467`, `1470`, or `1474`.

Returns before `1359` (`1194`, `1225`, `1244`, `1253`, `1259`, `1305`, `1318`, `1347`, `1354`) leave all four counters unchanged.

On a draw that records a command, `descriptor_binds` and `draws` match. The index returns at `1452` and `1458` are the binds with no draw command. `uniform_uploads` matches the `memcpy`s. `uniform_memcmp_fail` matches failed compares, which is a subset of the `1377` branch (the `!m_uniformsValid` entries are the rest).

## Zones

`OsGetAsyncTimeClocks` (`Engine/Source/Os/TimeManager.h:30`). On Linux, timing method 2 reads `CLOCK_MONOTONIC` in nanoseconds (`Engine/Source/Os/Linux/TimeManager.cpp:59:61`). Store `end - start` in a relaxed atomic. Scale with `OsGetAsyncClocksPerSecond` (`Engine/Source/Os/Linux/TimeManager.cpp:78:84`) when the one-second line is printed.

| Zone | Bracket | Body |
| --- | --- | --- |
| `uniforms_pack` | the call at `1359` | `IBuildUniforms` `971:1045`, including `memset` at `972` and both `IXformTex` calls at `988` |
| `uniforms_memcmp` | the `memcmp` at `1377` | the 768-byte compare |
| `uniforms_upload` | `1385:1389` | the `memcpy` and the stores to `frame.uniformOffset`, `m_uniformOffset`, `m_uniforms`, and `m_uniformsValid` |

`IXformTex` clears `m_dirty` on the texgen and texture-matrix stacks at `967:968`, inside `uniforms_pack`. Leave those stores on that path.

`descriptor_binds` is the measurement for `vkCmdBindDescriptorSets`. It has no zone.

## Budget check

Outdoor scene, same camera as Campaign 0. After the dirty-bit pack and the conditional descriptor bind, `uniform_uploads` and `descriptor_binds` follow state changes. `draws` stays the per-command count in the table above. `uniforms_pack` clocks fall when `IBuildUniforms` is skipped.
