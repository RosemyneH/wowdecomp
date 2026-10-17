# Graphics

The client draws through the old Gx device interface. On this fork the device behind that interface is Vulkan, unless you are on Windows x64 and you pick another API.

The retail 3.3.5 shader library (`.bls`) is not loaded. `CShaderEffect` stays off. Lighting, fog, terrain blends, UI text, and GPU skinning are GLSL in `Engine/Source/Gx/CGxDeviceVulkan/Shaders/`, compiled to SPIR-V and embedded in the tree. Regenerate that SPIR-V with the `GxVulkanShadersRegenerate` CMake target when `glslc` matches `Shaders/glslc.version`.

| File | Draws |
|---|---|
| `Ffp.vert` | Transforms, up to four lights, linear fog, GPU skinning, per-draw and per-instance data |
| `Ffp.frag` | Two texture stages, alpha test, fog mix, distance-field text |
| `FfpTerrain.frag` | Terrain layer blend, including the bindless texture array |

## Which API starts

`gxApi` defaults to Vulkan when this tree was built with Vulkan and a loader is present. On Windows x64 the same build still contains Direct3D 9 and OpenGL, and the client falls back to D3D9 if Vulkan cannot start. Linux and Windows on ARM64 link Vulkan only. macOS goes through MoltenVK. The full client on macOS does not link yet; see [Platforms](platforms.md).

```text
/console gxApi Vulkan
```

The name has to be one of `OpenGL`, `D3D9`, or `Vulkan`, and that backend has to be in the binary.

## What a draw contains

Each draw uploads a 768-byte uniform block: world, view, and projection, two texture matrices, material and ambient color, four lights, fog color and distances, and the fixed-function texture-stage ops. The vertex shader lights in the usual fixed-function way and writes a fog factor. The fragment shader runs the stage ops (`modulate`, `modulate2x`, `add`, `select`, and the two mixes), does the alpha test, then mixes toward the fog color.

Distance fog is that linear factor. It uses the fog distances the game just set. Volumetric fog, aerial haze, ambient occlusion, bloom, god rays, and tone mapping are absent.

## Textures

DXT1, DXT3, and DXT5 stay compressed. The device samples them as BC1, BC2, and BC3. If the GPU has no block-compression format, that format is expanded to RGBA before upload. A texture pointer that is not a `CGxTex` is rejected.

UI and world textures share this path. The indoor minimap is a special case: the cave map is drawn into a render target, then sampled with the circular mask texture so the square target stays inside the round frame.

## Terrain

A visible chunk is grouped by layer and texture. One group is one draw, so a layer that shares a texture across chunks is not a draw per chunk.

When the device supports descriptor indexing, sampled-image arrays, and update-after-bind, terrain uses a bindless array of up to 65,536 texture slots. The slot survives from frame to frame. The array is not rebuilt just because a new scene started. Devices without that feature set use the two ordinary samplers.

The base layer has no blend map, and the shader does not sample a second texture for it. Blend weights are taken from texel centers. While the camera stays put, the visibility set is reused. When the camera moves, the previous set is dropped. Layer groups are kept for as long as the same chunks stay visible.

Past the full-detail ring, the continent continues as the low-detail horizon mesh. [World](world.md) covers how far that reaches.

## Models

M2 characters and doodads have two paths.

On Vulkan, a character section with one UV set is uploaded once in bind pose (`GxVBF_PBNT2`: position, blend weights, blend indices, normal, two UVs). The vertex shader blends up to four bones, then applies the same world matrix the CPU path used to bake in. Bone matrices live in a storage buffer, 256 matrices per palette. Multi-bone doodads do the same, one palette per instance. One-bone doodads are instanced: one mesh, many worlds.

The palette ring is finite. If a bind does not fit, that draw is skinned on the CPU and uploaded as a plain mesh. Environment sections (a second UV set), optimized multi-section batches, and multi-sample batches stay on the CPU. Direct3D 9 and OpenGL always skin on the CPU, because `GpuSkinSupported` is true only for the Vulkan device.

The bind-pose buffers and the rigid-mesh uploads are kept across frames. A bone change does not reallocate the mesh.

## Screenshots

Print Screen uses `screenshotFormat` (`jpeg`, `tga`, or `jxl`) and `screenshotQuality` (1–10). JPEG is the default. The encoder accepts a row wider than the old 16,384-byte scratch limit, which is what used to fault a wide window.

JPEG XL is written only when the `Images` library was built against `libjxl` (`pkg-config` name `libjxl`). A build without that library fails the JXL write and leaves the other formats alone.
