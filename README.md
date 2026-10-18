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
| **Cross-platform** | Tested CI is **Windows x64** and **Linux x86_64** only. macOS has not been tested. Android, Windows ARM64, and Linux aarch64 are local builds, not release clients. |
| **Custom Vulkan shaders** | GLSL sources under `Engine/Source/Gx/CGxDeviceVulkan/Shaders/`; SPIR-V is embedded in-tree. Regenerate with the `GxVulkanShadersRegenerate` CMake target when `glslc` is installed. |
| **Lighting & fog** | Classic fixed-function lighting and linear distance fog in the Vulkan FFP shaders (`Ffp.vert`, `Ffp.frag`, `FfpTerrain.frag`), plus the existing `ShaderEffect` stack. Volumetric fog, aerial haze, ambient occlusion, bloom, god rays, and tone mapping are **not** in this tree. |
| **WASM addons** | Host runtime (`WasmAddon`, Wasmtime) for sandboxed `wasm32` addons. GitHub reading: [`Addons/Wasm/README.md`](Addons/Wasm/README.md), [`docs/lua-wasm.md`](docs/lua-wasm.md), [`docs/wasm-addon-architecture.md`](docs/wasm-addon-architecture.md). Wiki: [wowdecomp/wiki](https://github.com/RosemyneH/wowdecomp/wiki). Install addons under `Interface/WasmAddOns/`. |
| **Multithreaded performance** | Shared **job pool** (`Engine/Source/Base/Job/JobSystem.h`) drives parallel terrain, models, UI, and other hot paths—a broad performance rewrite on top of the decompiled client. |

Everything above is **experimental**: APIs move, backends differ per OS, and regressions are expected. Treat nightly-quality builds as the norm.

### Where to read more

The [wiki](https://github.com/RosemyneH/wowdecomp/wiki) explains each area in the table. The same pages live in this repo:

| Wiki | In the repo | What it answers |
|------|-------------|-----------------|
| [Home](https://github.com/RosemyneH/wowdecomp/wiki) | [`docs/overview.md`](docs/overview.md) | What this fork is, and where to start |
| [Graphics](https://github.com/RosemyneH/wowdecomp/wiki/Graphics) | [`docs/graphics.md`](docs/graphics.md) | Vulkan device, shaders, terrain, models |
| [World](https://github.com/RosemyneH/wowdecomp/wiki/World) | [`docs/world.md`](docs/world.md) | Full view, far clip, horizon, ground casts |
| [AwesomeWotLK](https://github.com/RosemyneH/wowdecomp/wiki/AwesomeWotLK) | [`docs/awesomewotlk.md`](docs/awesomewotlk.md) | Nameplates, speech, interact, camera, the Lua API |
| [Fonts](https://github.com/RosemyneH/wowdecomp/wiki/Fonts) | [`docs/fonts.md`](docs/fonts.md) | Distance-field UI text and screenshots |
| [Performance](https://github.com/RosemyneH/wowdecomp/wiki/Performance) | [`docs/performance.md`](docs/performance.md) | Job pool and the in-game profiler |
| [Platforms](https://github.com/RosemyneH/wowdecomp/wiki/Platforms) | [`docs/platforms.md`](docs/platforms.md) | Which OS builds, and what a release contains |
| [Writing addons](https://github.com/RosemyneH/wowdecomp/wiki/Writing-Addons) | [`Addons/Wasm/README.md`](Addons/Wasm/README.md) | WASM packages, permissions, ports |
| [Surfaces](https://github.com/RosemyneH/wowdecomp/wiki/Surfaces) | [`docs/surfaces.md`](docs/surfaces.md) | Canvas, models, hooks, and a Wayland client in a frame |

### Build & CI status

What **GitHub Actions** exercises on every push/PR, what **releases** ship, and what you can expect when building locally. “**WowClient**” means the full game executable (`Tools/WowClient`); “**Os + Gx**” is only the engine layers needed for the Vulkan path.

| OS / arch | CI workflow | CI builds | Release archive | Typical graphics | Playability notes |
|-----------|---------------|-----------|--------------------|------------------|-------------------|
| **Windows x64** | [`windows-x64`](.github/workflows/windows.yml) | **WowClient** (Ninja, Release) | `wowdecomp-windows-x64.zip` | Vulkan (default) + D3D9 + OpenGL | The tested Windows client. |
| **Linux x86_64** | [`linux-x64`](.github/workflows/linux.yml) | **WowClient** (Ninja, Release `-O3`) | `wowdecomp-linux-x86_64.tar.gz` | Vulkan on the session window | The tested Linux client. Wayland sessions get a Wayland window. X11 sessions stay on X11. |
| **Windows ARM64** | — | *not in CI* | — | Vulkan only if you build it | Local Ninja build only. |
| **Android arm64** | manual [`android-arm64`](.github/workflows/android.yml) | *not on push* | — | Vulkan on an SDL surface | Not a tested client. |
| **Linux aarch64** | — | *not in CI* | — | Vulkan only (expected) | Cross-build with `cmake/Toolchain-Clang-aarch64-linux.cmake`. |
| **macOS** | manual [`macos-arm64`](.github/workflows/macos.yml) | *not on push* | — | MoltenVK via **Gx** | Not tested. The workflow only compiles `Os` and `Gx`, and only when started by hand. |

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

- SDL3 (X11 and Wayland video, plus gamepads), OpenAL (ALSA), libcurl, pthreads
- Vulkan loader and drivers (or Mesa); headers are vendored via `ThirdParty/Vulkan-Headers` and volk
- Optional: system Wasmtime for WASM addons (see `cmake/Wasmtime.cmake`); otherwise addons may be compiled out

### macOS

- Xcode / Command Line Tools
- SDL2 (e.g. Homebrew)
- MoltenVK / Vulkan via the usual macOS Vulkan SDK or Homebrew stack
- Apple Silicon (`arm64`) and Intel are both targeted

### Android

- Android SDK 35, NDK r28, JDK 17, and an SDL 3.4.16 source tree (`SDL3_ROOT`)
- arm64-v8a only. The device needs Vulkan (API 33+)
- A gamepad is the same SDL3 path as Linux. Touch is delivered as mouse clicks
- FMOD has no Android output in this tree, so the build is silent. Xvid cinematics are stubbed

### Streaming `Data` from a CDN

If `WoW.mfil` is next to the client, startup calls `SFile::InitializeStreaming` and missing MPQ bytes are fetched with HTTP `Range` requests into `Data/*.part`. The file format is the one documented by **brian8544** for 3.3.5a:

- [streamingclient-3.3.5](https://github.com/brian8544/streamingclient-3.3.5) — client layout
- [mod-streamingclient](https://github.com/brian8544/mod-streamingclient) — GPL-2.0 server module

His server sources and the GPL-2.0 text are vendored in [`ThirdParty/streamingclient/`](ThirdParty/streamingclient/README.md). The module is not linked into the client. A copy of his example manifest is `ThirdParty/streamingclient/WoW.mfil.example`. `source=` must end with `/`.

The local Docker stack (`Tools/local-server/start-docker.sh` on Linux, `Tools/local-server/start-docker.ps1` on Windows) adds an `ac-cdn` nginx service on port **1119**. It serves `$WOWDECOMP_GAME_SRC/Data` (default `~/Games/WoW335/Data` on Linux, `W:\client\Data` on Windows) at the same `/Data/*.MPQ` paths his module serves from `DataDir/cdn`. He recommends a normal HTTP server for this. The script writes `WoW.mfil` into the acore-docker directory with `source=` set to this machine's LAN address, which is what the Android app should use.

### Windows local play (Docker + symlinked client)

1. Install **Docker Desktop** (WSL2 backend) and **Visual Studio 2022** with the C++ workload.
2. Enable **Developer Mode** (Settings → System → For developers) so directory symlinks work without an elevated shell.
3. Point at your 3.3.5a install (default `W:\client`):

```powershell
$env:WOWDECOMP_GAME_SRC = 'W:\client'   # optional if already W:\client
.\Tools\setup-windows-test-client.ps1   # play tree: test\dev-335 (gitignored)
.\Tools\local-server\start-docker.ps1   # AzerothCore on :3724 / :8085, CDN on :1119
```

Or use `.\Tools\dev-station.ps1 all` for scope + server, or `launch` to also start `WowClient64.exe` after a Release build. `dev-station` configures with `-DWOWDECOMP_WASM_ADDONS=OFF` so vanilla local play does not pull in static Wasmtime (which can crash on some Windows setups). Account **a** / **a** (GM). Stop with `.\Tools\local-server\stop-docker.ps1`.

---

## Building

Prebuilt clients are on the [latest build](https://github.com/RosemyneH/wowdecomp/releases/tag/continuous) release, updated from `main` by [`.github/workflows/release.yml`](.github/workflows/release.yml). A version tag (`v0.1.0`) publishes a normal release with the same archives. Each push also uploads `linux-x64-WowClient64` and `windows-x64-WowClient64` as Actions artifacts. Game `Data` is not in either archive.

| Archive | Platform |
|---------|----------|
| `wowdecomp-linux-x86_64.tar.gz` | Linux x86_64, `WowClient64` |
| `wowdecomp-windows-x64.zip` | Windows x64, `WowClient64.exe` |

Put a 3.3.5a `Data` folder beside the executable, or start it with `-datadir /path/to/client` (the folder that contains `Data`).

Compiling it yourself is the rest of this section. In-source builds are refused—use a separate `build/` directory. The `cmake --build … --target WowClient` line is what produces the game binary.

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

Packages: `ninja-build`, `g++`, `libsdl3-dev`, `libwayland-dev`, `libcurl4-openssl-dev`, `libasound2-dev`, `libvulkan-dev`, `libxvidcore-dev`. `libsdl3-dev` is in Ubuntu 25.04 and later (CI uses 26.04). macOS stays on SDL2.

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

### Android (experimental)

Not part of CI. [`android-arm64`](.github/workflows/android.yml) runs only when started by hand. This produces a native APK, not a desktop binary. Gradle drives CMake with the NDK. `SDL3_ROOT` must be an SDL **3.4.16** checkout so the Java activity and the native library are the same release.

```bash
curl -fsSL -o sdl.tgz https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.16.tar.gz
tar -xzf sdl.tgz
export SDL3_ROOT="$PWD/SDL-release-3.4.16"
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/28.2.13676358"
sdkmanager --install "platforms;android-35" "build-tools;35.0.0" "ndk;28.2.13676358" "cmake;3.31.6"
gradle -p android assembleRelease
```

The APK is `android/app/build/outputs/apk/release/app-release.apk`. It is signed with the debug keystore. Install it on an arm64 device or emulator with Vulkan. On first launch the app writes `WoW.mfil`, `WTF/Config.wtf`, and `streaming-credits.txt` under the app's external files directory (`Android/data/com.wowdecomp.client/files`). Edit `source=` in `WoW.mfil` to your CDN, and `realmList` in `WTF/Config.wtf`, before expecting the game to get past the login screen. Game archives are not in the APK.

### macOS (experimental, partial)

macOS is not tested and is not part of CI. A hand-started [`macos-arm64`](.github/workflows/macos.yml) run only compiles `Os` and `Gx`. Local smoke:

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

**Prebuilt binaries:** every push to `main` runs [`.github/workflows/release.yml`](.github/workflows/release.yml) and replaces the [latest build](https://github.com/RosemyneH/wowdecomp/releases/tag/continuous) prerelease with **Linux x86_64** and **Windows x64**. A version tag (`v0.1.0`) publishes a normal release with those same two archives. macOS, Android, and Windows ARM are not shipped. Game `Data` is never included.

The same commits also leave raw binaries as Actions artifacts: `linux-x64-WowClient64` and `windows-x64-WowClient64`.

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
| Android | `wowdecomp-android-arm64.apk` (`libmain.so`, Vulkan) |
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
| `ThirdParty/streamingclient/` | brian8544's GPL-2.0 CDN server, plus the example `WoW.mfil` |
| `android/` | Native Android Gradle project (SDL activity, Vulkan) |
| `Tools/WowClient/` | Client executable target |
| `Tools/WasmAddonHost/` | Headless addon test host |

---

## Special thanks

- [SatyPardus/wowdecomp](https://github.com/SatyPardus/wowdecomp) and everyone upstream credited there
- [brian8544](https://github.com/brian8544) for the 3.3.5a streaming client and `mod-streamingclient` (GPL-2.0)
- [binana](https://github.com/thunderbrewhq/binana)
- [thunderbrew](https://github.com/thunderbrewhq/thunderbrew)
- [whoa](https://github.com/whoahq/whoa)
- TOMRUS, for the initial IDB
- Everyone who has reported bugs so far
- The WoW modding community
