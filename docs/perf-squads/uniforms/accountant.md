# Campaign 2 accountant — per-draw uniform pack

Static byte account of `CGxDeviceVulkan::Draw`. No Campaign 0 log line is in tree yet, so this file is the conversion: multiply the log’s draw count, uniform uploads, and descriptor binds by the widths below. Do not rename those three log fields.

Budget scene is outdoor. `VkDrawUniforms` stays 768 bytes (`CGxDeviceVulkan.h:46`, `static_assert`, `Tools/spirv-pin-compare.py`).

## Bytes per draw today

A draw that reaches the pack (`CGxDeviceVulkan.cpp:1358`) calls `IBuildUniforms` (`CGxDeviceVulkan.cpp:1359`). Early returns before that line move no uniform bytes.

| Step | When | Width | Site |
| --- | --- | --- | --- |
| `memset` of the stack `VkDrawUniforms` | every pack | 768 | `CGxDeviceVulkan.cpp:972` |
| `memcmp` against cached `m_uniforms` | every pack | 768 | `CGxDeviceVulkan.cpp:1377` |
| `memcpy` into the frame uniform ring | dirty only | 768 | `CGxDeviceVulkan.cpp:1385` |

Dirty means `m_uniformsValid` is clear or the 768-byte compare fails. `ISceneBegin` clears `m_uniformsValid` (`CGxDeviceVulkan.cpp:589` via `CGxDeviceVulkan.cpp:535`), so the first pack of a frame always takes the memcpy. A later pack with the same bytes still pays the memset and the memcmp, and skips the memcpy.

The dirty path also assigns the 768-byte cache (`CGxDeviceVulkan.cpp:1388`). That is CPU traffic on top of the ring memcpy.

`DrawInstanced` (`CGxDeviceVulkan.cpp:1499`) and `DrawTerrainBatch` (`CGxDeviceVulkan.cpp:1538`) each call `Draw` once. Instance count does not multiply the 768-byte pack.

## Ring

Each in-flight frame owns one uniform buffer of `UniformRingSize` (`VkContext.h:132`, `32 << 20` = 32 MiB), created once in `VkContext.cpp:290`. `FramesInFlight` is 2, so 64 MiB stays resident. `PrepareFrame` resets the cursor (`VkContext.cpp:717`). Nothing on this path calls `malloc`.

The cursor is aligned to `minUniformBufferOffsetAlignment`, then advanced by 768 (`CGxDeviceVulkan.cpp:1378`, `CGxDeviceVulkan.cpp:1386`). 768 divides by the common 256-byte alignment (768 = 3 × 256), so one dirty upload consumes 768 ring bytes and no pad. A full ring holds 43,690 records (33,554,432 / 768) with 512 bytes unused.

If the next record would pass `frame.uniforms.size`, the draw returns and does not reuse another draw’s matrices (`CGxDeviceVulkan.cpp:1381`).

## Binds

`vkCmdBindDescriptorSets` runs on every draw that reaches it, with the current dynamic offset (`CGxDeviceVulkan.cpp:1441`, `CGxDeviceVulkan.cpp:1442`). The descriptor set is rebuilt only when the bound set is empty or the two views or two samplers differ (`CGxDeviceVulkan.cpp:1399`). An unchanged set and an unchanged offset still bind.

## Conversion

For draws that reach the pack:

- CPU clear + compare = `draws × 1,536`
- Ring bytes = `uniform uploads × 768`
- Extra CPU cache write = `uniform uploads × 768` (`CGxDeviceVulkan.cpp:1388`)
- Descriptor binds = draws that reach `CGxDeviceVulkan.cpp:1442`

Today, uploads are at most the draw count (one per dirty pack, including the first pack of the frame). Binds equal that draw count.

## Budget

Uniform uploads and descriptor binds follow state changes, not draw count, on the outdoor scene.

- A clean pack writes 0 bytes into the ring and does not bind.
- Ring bytes per frame = state changes × 768, still inside the 32 MiB ring.
- Descriptor binds per frame = changes of set or dynamic offset.
- The overflow return at `CGxDeviceVulkan.cpp:1381` stays. A missed dirty bit is a wrong frame.
