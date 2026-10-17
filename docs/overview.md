# wowdecomp

This tree is an experimental fork of [SatyPardus/wowdecomp](https://github.com/SatyPardus/wowdecomp): a C++ reconstruction of the World of Warcraft **3.3.5a** client, `Wow.exe` build **12340**. Upstream keeps each ported function tied to an address in that binary. This fork keeps that source and adds a Vulkan renderer, a longer view, an AwesomeWotLK-style Lua API, sandboxed WASM addons, and a job pool on the hot frame paths.

It is playable on the reference builds and still experimental. Expect API changes, and expect a ported function to differ from the original client.

Several other 3.3.5a clients also render with Vulkan or run on Linux and macOS. This repository is a different codebase. Their patches, binaries, and docs do not apply here.

Discord: https://discord.gg/DsZMGUD8aT

## Read next

| If you want to know | Page |
|---|---|
| What the picture is made of | [Graphics](graphics.md) |
| How far the world draws | [World](world.md) |
| Nameplates, speech, interact, camera | [AwesomeWotLK](awesomewotlk.md) |
| Sharp UI text, and screenshots | [Fonts](fonts.md) |
| Where a frame spends its time | [Performance](performance.md) |
| Which operating system actually builds | [Platforms](platforms.md) |
| How to write a sandboxed addon | [Writing addons](../Addons/Wasm/README.md) |
| A page, a model, or a browser in a frame | [Surfaces](surfaces.md) |

Build commands and the decompilation percentages are in the [README](https://github.com/RosemyneH/wowdecomp/blob/main/README.md). No game `Data` ships with the source. You bring a 3.3.5a client.

## What this fork adds

| Area | What you get |
|---|---|
| **Vulkan** | Default graphics API when the loader is present. Windows x64 can still select D3D9 or OpenGL. Linux and Windows on ARM are Vulkan-only. |
| **Shaders** | In-tree GLSL that reproduces fixed-function lighting, texture stages, and linear distance fog. The retail `.bls` shader library stays unloaded. |
| **Terrain** | One pass per layer. DXT stays compressed on the GPU. A bindless texture array is used when the device allows it. |
| **Characters** | On Vulkan, one-UV character meshes and many doodads skin on the GPU. The remaining batches stay on the CPU. |
| **View** | A **Full view** checkbox pushes the far clip out to 50,000 and draws the continent horizon past the detail ring. |
| **AwesomeWotLK** | Lua globals for nameplates, text to speech, unit flags, transmog, cooldowns, the window, the clipboard, and interact. WASM addons can call the same list. |
| **Fonts** | Optional multi-channel distance-field UI fonts (`gxMsdfFonts`). |
| **Addons** | Wasmtime packages under `Interface/WasmAddOns`, beside ordinary FrameXML addons. ElvUI and Details can load as their original Lua trees. |
| **Threads** | A job pool for terrain fill, model animation, doodad culling, and the CPU skinning that remains. |
| **Profiler** | Zone times on the latency-bar tooltip, and `GetProfilerZones()` from Lua. |

## The binary underneath

Measured against the 12340 binary on 2026-10-15: **24,515 / 27,157** functions (90.3%) and **93.2%** of the code bytes have an `// OFFSET:` tag in this tree. Those numbers describe the decompilation. They do not say the Vulkan path is finished.

x64 is the architecture the original client shipped. Win32 still builds on Windows so struct layouts can be checked against the 32-bit binary. Most of the untagged remainder is compiler-generated code, the C runtime, and libraries linked from upstream sources (FreeType, zlib, PCRE, parts of FMOD Ex) instead of being decompiled.
