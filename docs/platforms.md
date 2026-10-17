# Platforms

What builds, what a release archive contains, and how the two launch modes differ. Exact CMake lines and package lists are in the [README](https://github.com/RosemyneH/wowdecomp/blob/main/README.md).

**WowClient** is the game executable (`Tools/WowClient`). **Os + Gx** is only the platform and graphics libraries, which is as far as macOS CI gets.

| OS / arch | CI | Release archive | Graphics in that build |
|---|---|---|---|
| Windows x64 | Full WowClient | `wowdecomp-windows-x64.zip` | Vulkan by default, plus D3D9 and OpenGL |
| Windows ARM64 | Full WowClient on `windows-11-arm`. The job is allowed to fail without failing x64 | `wowdecomp-windows-arm64.zip` when the ARM job succeeds | Vulkan only |
| Linux x86_64 | Full WowClient | `wowdecomp-linux-x86_64.tar.gz` | Vulkan only |
| Android arm64 | Full WowClient inside an APK ([`android-arm64`](../.github/workflows/android.yml)) | `wowdecomp-android-arm64.apk` | Vulkan only. API 33+ |
| Linux aarch64 | None yet | None | Vulkan, via `cmake/Toolchain-Clang-aarch64-linux.cmake` |
| macOS Apple Silicon | `Os` and `Gx` only | None | MoltenVK. The full client still fails to link |
| macOS Intel | None | None | Same stack if you build it locally |

Tag a version (`v0.1.0`) to run the release workflow. Game `Data` is never in the archive. CI artifacts from ordinary pushes are `linux-x64-WowClient64`, `windows-x64-WowClient64`, `windows-arm64-WowClient64`, and `android-arm64-wowdecomp`.

The Android package is a native activity. SDL3 creates the Vulkan surface. A plugged-in gamepad uses the same mapping as Linux. Touch arrives as mouse clicks. The first launch writes `WoW.mfil` into the app files directory so archives can stream from an HTTP CDN. That manifest format, and the server that serves the range requests, are brian8544's GPL-2.0 streaming client: [streamingclient-3.3.5](https://github.com/brian8544/streamingclient-3.3.5) and [mod-streamingclient](https://github.com/brian8544/mod-streamingclient). The server sources are in `ThirdParty/streamingclient/`. The Docker AzerothCore stack serves those archives from nginx on port 1119 (`Tools/local-server/start-docker.sh`). Sound and in-game cinematics are not available in the Android build.

A separate workflow rebuilds the embedded Vulkan SPIR-V when the GLSL under `Engine/Source/Gx/CGxDeviceVulkan/Shaders/` changes.

## What you need

A 3.3.5a (12340) client for the `Data` folder, CMake 3.24 or newer, and a C++17 compiler. Visual Studio 2022 on Windows, including the ARM64 toolset if you are building for Windows on ARM. Clang or GCC on Linux and macOS. Linux wants SDL3 with its Wayland driver, OpenAL, libcurl, pthreads, a Vulkan loader, and the Xvid library. A plugged-in gamepad is on by default (`Joystick` `1`) and follows the retail layout: left stick moves, right stick looks, right-stick click toggles the cursor, A jumps, B cancels. macOS stays on SDL2. Headers for Vulkan itself are in the tree. The game window is created on Wayland. Set `SDL_VIDEODRIVER` yourself only if you mean to override that.

`WOWDECOMP_DISABLE_SIGNATURE_CHECKS=ON` skips MPQ and `.toc.sig` checks. You want that for a modified `Data` or a modified `Interface`.

`--target WowClient` builds the game only. It does not compile WASM addon packages, and it does not copy anything into `Interface/WasmAddOns`.

## Two ways to launch on Linux

`Tools/dev-station.sh` is the local play setup. It is optional.

| Command | Binary | Notes |
|---|---|---|
| `Tools/dev-station.sh launch` | `WowClient64`, RelWithDebInfo | Dev client. Profiler zones default **on**. Can stage ElvUI, Details, and the wasm packages |
| `Tools/dev-station.sh release` | `WowClient64.release`, `-O3 -DNDEBUG` | Separate build directory, so it does not replace the dev binary. Profiler defaults **off**. Writes `gxMsdfFonts` `1` into `WTF/Config.wtf` unless that file is a symlink |

Both start from the play directory (`test/dev-335` unless you point them elsewhere) so the client finds `Data` next to the executable. Copying a CI binary beside your own `Data` folder and starting it there is the same idea without the script.

## Addons on a play client

FrameXML addons stay in `Interface/AddOns`. WASM packages go in `Interface/WasmAddOns/<Name>/` with an `addon.toml` and an `addon.wasm`. The addon list, `DisableAddOn`, and `AddOns.txt` disable a WASM package the same way they disable any other addon.

ElvUI and Details, when the play script stages them, are the original Lua trees in their own `Interface/WasmAddOns/<Name>` folders plus a wasm manifest. The `addon.wasm` in that folder is a package stub. The UI that appears is still the TOC. `/ec` opens ElvUI's options. `/details` opens the meter.

The in-tree Rust ports (WIM, the AwesomeWotLK panel, the unit-frame and whisper examples) are real WASM modules. [Writing addons](../Addons/Wasm/README.md) is the package guide. [AwesomeWotLK](awesomewotlk.md) is the extra Lua API those ports call.
