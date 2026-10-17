# Writing WASM addons

Sandboxed `wasm32` packages for the wowdecomp 3.3.5a client. The longer Lua dictionary is [docs/lua-wasm.md](../../docs/lua-wasm.md). Host layout is [docs/wasm-addon-architecture.md](../../docs/wasm-addon-architecture.md). The rest of the fork — Vulkan, the long view, AwesomeWotLK — is the [project overview](../../docs/overview.md). The same pages are on the [GitHub wiki](https://github.com/RosemyneH/wowdecomp/wiki).

## Build

```bash
export PATH="$HOME/.cargo/bin:$PATH"
rustup target add wasm32-unknown-unknown
cargo build --target wasm32-unknown-unknown --release
```

On Arch, the system `rustc` has no wasm standard library. Put `~/.cargo/bin` first.

Use the SDK in `sdk/wow-addon-sdk`. Examples: `hello-world`, `inventory-watcher`, `ui-demo`, `unit-frames`, `whisper-window`. A C example with raw imports is `examples/c-hello`. `Addons/Wasm/build.sh` builds the in-tree crates into `Addons/Wasm/dist`.

The module must export `memory` and `wow_on_load() -> i32`. Export `wow_on_event` or, for API v2, `wow_on_event_v2`. `wow_alloc` / `wow_free` come from the SDK. Anything imported outside the `wow` module, including WASI, fails validation.

## Package

```text
MyAddon/
├── addon.toml
├── addon.wasm
└── LICENSE        # optional; addon.sig is detected but not verified yet
```

Copy the folder into `Interface/WasmAddOns/` in the client directory. The host also accepts a top-level `WasmAddOns/` if `Interface/WasmAddOns` is absent. Each package is also an entry in the addon list: `DisableAddOn`, the character screen checkbox, and `AddOns.txt` stop it the same way they stop any other addon. The description on that entry is a colored list: id, version, API, author, and each declared permission. Green permissions are ordinary. Gold ones need `wasmgrant`. Hover the row on the character screen and the same text sits in a panel beside the list. A package that also has a TOC keeps its own notes above that list.

## Frames beyond widgets

`ui_spawn` also accepts `canvas` and `model`. `canvas_write` uploads RGBA into the canvas. `model_set` points a `PlayerModel` at `unit:player`, a creature id, or a model path. `hook_bind` is `hooksecurefunc` on one global and delivers `WASM_HOOK`. Those three use `ui.create`, except `hook_bind`, which needs `ui.hook`.

`surface_open` puts a Wayland client in a frame you can drag and close, the same way you close a bag. It is not a desktop overlay. The game is a private Wayland display for that one window. An `https://` address opens Chromium. `app:` runs another program with `WAYLAND_DISPLAY` pointed at that frame, so anything that already speaks Wayland can sit in the corner. The permission is `surface.host`, and it stays off until `wasmgrant`.

```rust
use wow_addon_sdk::{hook_bind, model_set, surface_open};

let page = surface_open("https://www.youtube.com", 640, 400);
let _ = model_set(model, "unit:player");
let _ = hook_bind("CastSpellByName");
```

```toml
[addon]
id = "port.example"
name = "Example"
version = "0.1.0"
author = "you"
api_version = "2"

[permissions]
ui.create = true
chat.print = true

[limits]
memory_mb = 32
```

`api_version` `"1"` still loads and receives the event name only. `"2"` requires `wow_on_event_v2` and receives WEV1 payloads. Limits are requests and are clamped: memory 256 MB, fuel 50,000,000 per callback, storage 4 MB, 256 UI handles, 128 event subscriptions.

Sensitive permissions (`units.read`, `chat.read`, `chat.send`, `storage.*`, `auction.*`, `combat.read`) stay denied until the developer console grants them. Grants are stored per addon id and module hash in `wasm-permissions.cfg`.

```text
wasmgrant <addon-id> <permission>
wasmdeny <addon-id> <permission>
```

## Frames

`ui_spawn(parent, kind)` creates a `frame`, `fontstring`, `texture`, `statusbar`, `button`, `editbox`, or `scrollframe`. Parent `0` is `UIParent`. Any other parent must be a frame, button, status bar, edit box, or scroll frame you own.

`ui_style(handle, op, blob)` places and colors it. `wow_addon_sdk::ui` builds ops 1–11 (size, point, color, show, status-bar value, justify, font size, backdrop, text color, level, strata). `wow_addon_sdk::frame::Frame` is the same surface as methods. Ops 12–14 are alpha (thousandths), mouse enable, and an `Interface\` texture path.

`ui_set_text` sets font-string or edit-box text and keeps newlines. `ui_create` is the older one-shot label. An addon that exports `wow_on_update` is called about five times a second.

`ui_bind` attaches `click`, `enter`, `leave`, `escape`, `enter_pressed`, or `text_changed`. Subscribe to `WASM_UI`. In this client, `OnClick` exists on buttons. Edit boxes receive enter, escape, and text-changed. `ui_get_text` reads an edit box. `units::names` returns display names when `units.read` is granted.

## Full Lua from Rust

Declare `lua.full = true`. Addons stay Rust. `wow_addon_sdk::lua::call` and `lua::method` invoke any 3.3.5 global or widget method. `Val::Map` is how you pass a Lua table. `lua::bind` sends `OnClick` / `OnEnterPressed` back as `WASM_UI`. The guide is [docs/lua-wasm.md](../../docs/lua-wasm.md).

## Ports

Clean-room layouts, not the upstream Lua addons:

| Port | Path | Id | Draws |
|---|---|---|---|
| Unit frames | `examples/unit-frames` | `example.unit-frames` | Player, target, and party bars |
| Whisper list | `examples/whisper-window` | `example.whisper-window` | One whisper log |
| WIM | `ports/wim` | `port.wim` | Rust whisper windows. Enter calls `SendChatMessage` |
| Awesome | `ports/awesome` | `port.awesome` | Nameplates, TTS, window flash, and the AwesomeWotLK CVars |

The play client keeps **ElvUI** and **Details** as their own `Interface/WasmAddOns/<Name>` folders. Those folders are the original addon trees plus a wasm manifest. `addon.wasm` in that folder is only a package stub; the UI is still the original TOC. `/ec` is ElvUI’s options. `/details` is the meter. `Tools/dev-station.sh` stages those folders on `scope`.

```text
wasmgrant port.wim chat.read
```

`lua.full` is already granted because the manifests declare it. Whisper text stays redacted until `chat.read` is granted.

## Inspect

In game:

```text
/script DEFAULT_CHAT_FRAME:AddMessage(WasmAddonInspectorText())
```

Headless:

```text
wasm_addon_host <addon-dir> --inspector
```

Host functions return negative codes instead of trapping the addon: `-2` permission, `-3` bad memory, `-4` stale handle, `-5` bad argument, `-6` limit, `-7` buffer too small, `-8` unavailable, `-9` unknown event. A trap or fuel exhaustion faults only that addon.

## Versioning

`api_version` in the manifest is checked against the host. Breaking ABI changes bump the version. An addon built for an older version is rejected with a load error.
