# wowdecomp (experimental fork)

**This repository is an extremely experimental fork** of the [SatyPardus/wowdecomp](https://github.com/SatyPardus/wowdecomp) project: a C++ decompilation of the World of Warcraft **3.3.5a** client (`Wow.exe`, build **12340**, June 2010). The upstream tree is a faithful, address-tagged reconstruction of the original binary; **this fork layers on rendering, platform, and performance work that is incomplete, unstable, and not suitable for production use.**

If you want the closest match to the reference decompilation effort, use upstream. If you want to break things on purpose—Vulkan, new platforms, WASM addons, and a multithreaded client rewrite—you are in the right place.

**Not the other Vulkan / Linux / macOS clients.** Several independent 3.3.5a clients and ports (Vulkan renderers, native Linux builds, macOS wrappers, and similar projects) exist in the modding ecosystem. **This fork is a separate codebase and lineage** (SatyPardus decomp → this tree). We have **not** tested compatibility, interchangeability, or parity with those projects—do not assume patches, addons, binaries, or docs from them apply here.

Join the Discord for other clients and to show off cool stuff you build: https://discord.gg/DsZMGUD8aT

---

## What this fork adds (high level)

| Area | Status in this fork |
|------|---------------------|
| **Vulkan renderer** | Default backend wherever the full client builds (MoltenVK on macOS **Gx**). **Windows x64** still exposes Direct3D 9 and OpenGL; **Windows on ARM64** and **Linux** are **Vulkan-only** at the Gx layer. |
| **Cross-platform** | See **[Build & CI status](#build--ci-status)** below—full **WowClient** CI covers **Windows x64**, **Windows ARM64**, and **Linux x86_64**; **macOS** is a partial **Os + Gx** compile only; **Linux aarch64** is local/cross-build via `cmake/Toolchain-Clang-aarch64-linux.cmake` (no CI yet). |
| **Custom Vulkan shaders** | GLSL sources under `Engine/Source/Gx/CGxDeviceVulkan/Shaders/`; SPIR-V is embedded in-tree. Regenerate with the `GxVulkanShadersRegenerate` CMake target when `glslc` is installed. |
| **Lighting & fog** | Classic fixed-function lighting and linear distance fog in the Vulkan FFP shaders (`Ffp.vert`, `Ffp.frag`, `FfpTerrain.frag`), plus the existing `ShaderEffect` stack. Volumetric fog, aerial haze, ambient occlusion, bloom, god rays, and tone mapping are **not** in this tree. |
| **WASM addons** | Host runtime (`WasmAddon`, Wasmtime) for sandboxed `wasm32` addons. GitHub reading: [`Addons/Wasm/README.md`](Addons/Wasm/README.md), [`docs/lua-wasm.md`](docs/lua-wasm.md), [`docs/wasm-addon-architecture.md`](docs/wasm-addon-architecture.md). Wiki: [wowdecomp/wiki](https://github.com/RosemyneH/wowdecomp/wiki). Install addons under `Interface/WasmAddOns/`. |
| **Multithreaded performance** | Shared **job pool** (`Engine/Source/Base/Job/JobSystem.h`) drives parallel terrain, models, UI, and other hot paths—a broad performance rewrite on top of the decompiled client. |

Everything above is **experimental**: APIs move, backends differ per OS, and regressions are expected. Treat nightly-quality builds as the norm.

### Build & CI status

What **GitHub Actions** exercises on every push/PR, what **releases** ship, and what you can expect when building locally. “**WowClient**” means the full game executable (`Tools/WowClient`); “**Os + Gx**” is only the engine layers needed for the Vulkan path.

| OS / arch | CI workflow | CI builds | Release tag (`v*`) | Typical graphics | Playability notes |
|-----------|---------------|-----------|--------------------|------------------|-------------------|
| **Windows x64** | [`windows-x64`](.github/workflows/windows.yml) | **WowClient** (Ninja, Release) | `wowdecomp-windows-x64.zip` | Vulkan (default) + D3D9 + OpenGL | Main reference path; closest to “retail-like” on this fork. |
| **Windows ARM64** | same workflow, job **`build-arm64`** | **WowClient** on `windows-11-arm` | `wowdecomp-windows-arm64.zip` when the ARM job succeeds | **Vulkan only** (no D3D9/NvAPI/OpenGL in the build) | CI job uses `continue-on-error: true` so x64 stays authoritative if ARM regresses; **currently building in CI**. |
| **Linux x86_64** | [`linux-x64`](.github/workflows/linux.yml) | **WowClient** (Ninja, RelWithDebInfo) | `wowdecomp-linux-x86_64.tar.gz` | Vulkan only | Experimental; needs Vulkan loader/drivers. |
| **Linux aarch64** | — | *not in CI* | — | Vulkan only (expected) | Cross-build with `cmake/Toolchain-Clang-aarch64-linux.cmake`; treat as untested in automation. |
| **macOS Apple Silicon** | [`macos-arm64`](.github/workflows/macos.yml) | **Os + Gx** static libs only | *not published* | MoltenVK via **Gx** | Full **WowClient** still fails to link (e.g. `malloc.h`, FMOD codec paths)—CI is a compile smoke test, not a runnable client. |
| **macOS Intel** | — | *not in CI* | — | Same stack as arm64 locally | No automated build; use `-DCMAKE_OSX_ARCHITECTURES=x86_64` if you try it. |

Other automation: [`generated-drift`](.github/workflows/generated-drift.yml) regenerates embedded Vulkan SPIR-V when GLSL under `Engine/Source/Gx/CGxDeviceVulkan/Shaders/` changes.

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

`--target WowClient` is the game binary alone. It does not build wasm addon packages, the addon test host, or copy anything into `Interface/WasmAddOns`. `Tools/dev-station.sh` is an optional local play setup that does those extra steps.

### Windows (x64, main target)

```powershell
git clone https://github.com/RosemyneH/wowdecomp.git
cd wowdecomp

cmake -S . -B build/x64 -G "Visual Studio 17 2022" -A x64
cmake --build build/x64 --config Release --target WowClient --parallel
```

Windows on ARM64 (native **WowClient**; **Vulkan-only** Gx—match CI with Ninja):

```powershell
cmake -S . -B build/arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/arm64 --target WowClient --parallel
```

Visual Studio generator also works: `-G "Visual Studio 17 2022" -A ARM64` with `--config Release`.

Win32 (optional, needs NASM):

```powershell
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build/win32 --config Release --target WowClient --parallel
```

### Linux (experimental)

Matches CI (`linux-x64` workflow). This builds **only the game client**. It does not compile the wasm addon packages, the addon test host, or stage anything under `Interface/WasmAddOns`.

Packages: `ninja-build`, `g++`, `libsdl2-dev`, `libx11-dev`, `libcurl4-openssl-dev`, `libasound2-dev`, `libvulkan-dev`, `libxvidcore-dev`.

```bash
cmake -S . -B build/linux -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON
cmake --build build/linux --target WowClient -j"$(nproc)"
```

The binary is `bin/Linux/WowClient64`. Copy it next to your own 3.3.5a `Data` folder and start it from that directory. Leave `Interface/WasmAddOns` absent. The client still links the addon host so it can load a package later. Nothing is loaded if that folder is missing.

`Tools/dev-station.sh` is a separate play setup. `build` and `all` also compile the wasm host and link addon trees into `test/dev-335`. Skip that script if you only want the client.

Release (`-O3 -DNDEBUG`) uses another build directory so it does not replace the RelWithDebInfo binary:

```bash
cmake -S . -B build/linux-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DWOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON
cmake --build build/linux-release --target WowClient -j"$(nproc)"
```

For **aarch64** Linux cross-builds (no CI), see `cmake/Toolchain-Clang-aarch64-linux.cmake`.

### macOS (experimental, partial)

You can configure the full tree locally, but **CI only builds `Os` and `Gx`** until the macOS **WowClient** port catches up. Smoke build (same as [`macos-arm64`](.github/workflows/macos.yml)):

```bash
brew install ninja sdl2 xvid molten-vk vulkan-loader vulkan-headers vulkan-tools
cmake -S . -B build/mac -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build/mac --target Os Gx -j3
```

A full `WowClient` target on macOS is still expected to fail until platform gaps are closed.

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

On every push/PR, see the **[Build & CI status](#build--ci-status)** table for job names and what each platform compiles.

**Prebuilt binaries:** push a version tag (`v0.1.0`, etc.) to run [`.github/workflows/release.yml`](.github/workflows/release.yml)—**Linux x86_64**, **Windows x64**, and **Windows ARM64** archives (ARM is packaged when its job succeeds; it does not block the release job). **macOS** is not shipped. Game `Data` is never included—use your own 3.3.5a install.

Workflow artifacts from non-tag CI runs: `linux-x64-WowClient64`, `windows-x64-WowClient64`, and `windows-arm64-WowClient64` when the ARM job passes.

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
