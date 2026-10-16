# wowdecomp (experimental fork)

**This repository is an extremely experimental fork** of the [SatyPardus/wowdecomp](https://github.com/SatyPardus/wowdecomp) project: a C++ decompilation of the World of Warcraft **3.3.5a** client (`Wow.exe`, build **12340**, June 2010). The upstream tree is a faithful, address-tagged reconstruction of the original binary; **this fork layers on rendering, platform, and performance work that is incomplete, unstable, and not suitable for production use.**

If you want the closest match to the reference decompilation effort, use upstream. If you want to break things on purpose—Vulkan, new platforms, WASM addons, and a multithreaded client rewrite—you are in the right place.

**Not the other Vulkan / Linux / macOS clients.** Several independent 3.3.5a clients and ports (Vulkan renderers, native Linux builds, macOS wrappers, and similar projects) exist in the modding ecosystem. **This fork is a separate codebase and lineage** (SatyPardus decomp → this tree). We have **not** tested compatibility, interchangeability, or parity with those projects—do not assume patches, addons, binaries, or docs from them apply here.

Join the Discord for other clients and to show off cool stuff you build: https://discord.gg/DsZMGUD8aT

---

## What this fork adds (high level)

| Area | Status in this fork |
|------|---------------------|
| **Vulkan renderer** | Primary graphics backend on every supported OS (MoltenVK on macOS). Direct3D 9 remains a selectable fallback on Windows. |
| **Cross-platform** | **Windows** (x64 and **Windows on ARM64**), **Linux** (x86_64 and **aarch64**), and **macOS** (Intel and **Apple Silicon**). Linux aarch64 cross-builds use `cmake/Toolchain-Clang-aarch64-linux.cmake`. |
| **Custom Vulkan shaders** | GLSL sources under `Engine/Source/Gx/CGxDeviceVulkan/Shaders/`; SPIR-V is embedded in-tree. Regenerate with the `GxVulkanShadersRegenerate` CMake target when `glslc` is installed. |
| **Lighting & fog** | Modernized lighting paths through the Vulkan fixed-function pipeline and existing `ShaderEffect` stack; **volumetric fog** and related atmosphere work are part of this experimental rendering push (behavior and quality will change). |
| **WASM addons** | Host runtime (`WasmAddon`, Wasmtime) for sandboxed `wasm32` addons—see `Addons/Wasm/README.html` and `docs/wasm-addon-architecture.html`. Install addons under `Interface/WasmAddOns/`. |
| **Multithreaded performance** | Shared **job pool** (`Engine/Source/Base/Job/JobSystem.h`) drives parallel terrain, models, UI, and other hot paths—a broad performance rewrite on top of the decompiled client. |

Everything above is **experimental**: APIs move, backends differ per OS, and regressions are expected. Treat nightly-quality builds as the norm.

---

## Decompilation baseline (shared with upstream)

Every ported function keeps an `// OFFSET:` tag tied to the original binary so it can be compared against 12340. The source tree follows the original layout (`Engine/Source/<Module>`, `WoW/Source/<Module>`, original file names).

x64 is the main target. Win32 still builds on Windows and is used to check struct layouts against the 32-bit original.

### Progress

The percentages below measure how much of the original binary has a counterpart in this source tree. They do **not** say how much of the game works on every platform; on the reference Windows x64 path the client builds into a complete executable and the game is largely playable. **Non-Windows and Vulkan paths are less mature.** Bugs are always possible—a ported function can behave differently from the original.

Measured against the 12340 binary on 2026-10-15. A function counts as ported when its address is tagged in the source.

| | Ported | Total | Share of the original |
|---|---:|---:|---:|
| Functions | 24,515 | 27,157 | 90.3% |
| Code bytes | 5,270,186 | 5,656,573 | 93.2% |

The total leaves out 658 functions that IDA identifies as compiler library code and 132 import thunks. Most of the untagged remainder is code the build gets elsewhere: compiler-generated code, C runtime helpers, and third-party libraries linked from upstream sources instead of being decompiled (FreeType, zlib, PCRE, parts of FMOD Ex).

---

## Requirements

- A World of Warcraft **3.3.5a (12340)** client. **No game data is included** in this repository.
- **CMake 3.24+**
- **C++17** toolchain (Visual Studio 2022 on Windows; Clang or GCC on Linux/macOS)

### Windows

- Windows 10 or 11 (x64 PCs and **ARM64** devices such as Snapdragon laptops)
- Visual Studio 2022 with **Desktop development with C++** (MSVC v143 and a Windows SDK). Install the **ARM64** MSVC toolset when building for Windows on ARM.
- Optional: [NASM](https://www.nasm.us/) for Win32 builds; `DivxDecoder.dll` from your 3.3.5a client in `ThirdParty/DivxDecoder/` for Win32 only (x64/ARM64 use the in-tree Xvid decoder where applicable)

### Linux

- SDL2, X11, OpenAL (ALSA), libcurl, pthreads
- Vulkan loader and drivers (or Mesa); headers are vendored via `ThirdParty/Vulkan-Headers` and volk
- Optional: system Wasmtime for WASM addons (see `cmake/Wasmtime.cmake`); otherwise addons may be compiled out

### macOS

- Xcode / Command Line Tools
- SDL2 (e.g. Homebrew)
- MoltenVK / Vulkan via the usual macOS Vulkan SDK or Homebrew stack
- Apple Silicon (`arm64`) and Intel are both targeted

---

## Building

In-source builds are refused—use a separate `build/` directory.

### Windows (x64, main target)

```powershell
git clone https://github.com/RosemyneH/wowdecomp.git
cd wowdecomp

cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release --target WowClient --parallel
```

Windows on ARM64 (experimental; same client, native ARM64 binary):

```powershell
cmake -S . -B build/arm64 -G "Visual Studio 17 2022" -A ARM64
cmake --build build/arm64 --config Release --target WowClient --parallel
```

Win32 (optional, needs NASM):

```powershell
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/win32 --config Release --target WowClient --parallel
```

### Linux / macOS (experimental)

Example Ninja release build on Linux:

```bash
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --target WowClient
```

On Apple Silicon, pass `-DCMAKE_OSX_ARCHITECTURES=arm64` if needed. For aarch64 Linux cross-builds, see `cmake/Toolchain-Clang-aarch64-linux.cmake`.

### CMake options (selected)

| Option | Default | Effect |
|--------|---------|--------|
| `WOWDECOMP_ENABLE_VULKAN` | `ON` | Build the Vulkan Gx backend (MoltenVK on macOS) |
| `WOWDECOMP_DISABLE_SIGNATURE_CHECKS` | `OFF` | Skip MPQ and `.toc.sig` signature checks (modded data/UI) |
| `WOWCLIENT_CRASH_LOG` | `ON` (Windows) | Crash logger executable variant where enabled |
| `WOWCLIENT_OUTPUT_OVERRIDE` | empty | Override the `WowClient` binary name |

Regenerate embedded Vulkan SPIR-V (requires matching `glslc`—see `Engine/Source/Gx/CGxDeviceVulkan/Shaders/glslc.version`):

```bash
cmake --build <build-dir> --target GxVulkanShadersRegenerate
```

### CI and releases

GitHub Actions on push/PR: **`linux-x64`** and **`windows-x64`** build **WowClient**; **`macos-arm64`** compiles **Os + Gx** (Vulkan path) until the full macOS client port is ready; **`windows-arm64`** is experimental. Shader drift is checked by `generated-drift` when GLSL under `Engine/Source/Gx/CGxDeviceVulkan/Shaders/` changes.

**Prebuilt binaries:** push a version tag (`v0.1.0`, etc.) to run the **Release** workflow (Linux + Windows x64 packages; Windows ARM64 when that job succeeds). macOS releases are not published yet. Game `Data` is never included—use your own 3.3.5a install.

---

## Running

1. Copy the `Data` folder from your 3.3.5a install next to the client binary.
2. Point `Data/<locale>/realmlist.wtf` at your realm.
3. Launch the client from the platform output directory:

| Platform | Typical binary path |
|----------|---------------------|
| Windows (x64) | `bin/Win/WowClient64.exe` (or `Wow64.exe`) |
| Windows (ARM64) | `bin/Win/WowClient64.exe` (ARM64 build from `-A ARM64`) |
| Linux | `bin/Linux/WowClient64` |
| macOS | `bin/Mac/WowClient64` |

`WowClient` is the variant with extra diagnostics where the platform supports them; `Wow` is the same client without that wrapper on Windows.

---

## Layout

| Path | Contents |
|------|----------|
| `Engine/Source/Gx/CGxDeviceVulkan/` | Vulkan device, shaders, platform glue |
| `Engine/Source/WasmAddon/` | WASM addon host (Wasmtime) |
| `Engine/Source/Base/Job/` | Multithreaded job pool |
| `Engine/Source/` | Other engine modules: Storm, Gx, Model2, Frame/FrameXML, SoundEngine, Lua, … |
| `WoW/Source/` | Game modules: world rendering, UI, networking, database, glue |
| `Addons/Wasm/` | Addon SDK, examples, packaging docs |
| `ThirdParty/` | Libraries linked from upstream sources (Vulkan headers, volk, …) |
| `Tools/WowClient/` | Client executable target |
| `Tools/WasmAddonHost/` | Headless addon test host |

---

## Special thanks

- [SatyPardus/wowdecomp](https://github.com/SatyPardus/wowdecomp) and everyone upstream credited there
- [binana](https://github.com/thunderbrewhq/binana)
- [thunderbrew](https://github.com/thunderbrewhq/thunderbrew)
- [whoa](https://github.com/whoahq/whoa)
- TOMRUS, for the initial IDB
- Everyone who has reported bugs so far
- The WoW modding community
