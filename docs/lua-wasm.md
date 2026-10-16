# Lua addons to WASM

Guide for a 3.3.5 addon author moving a UI onto this client's sandboxed `wasm32` runtime. Packages live in `Interface/WasmAddOns`. The guest is Rust (or anything that emits `wasm32-unknown-unknown`) linked to `wow-addon-sdk`.

Widget helpers are `wow_addon_sdk::ui` and `wow_addon_sdk::frame`. A frame is the spawn kind `"frame"`, not a second crate.

Package steps are in [Addons/Wasm/README.md](../Addons/Wasm/README.md). Host layout is in [wasm-addon-architecture.md](wasm-addon-architecture.md).

## A parallel runtime

The Lua VM is untouched. FrameXML addons still load from TOC files, and `FrameScript_SignalEvent` still runs every Lua listener first. A WASM package is a second runtime: its own Wasmtime instance, its own linear memory, its own fuel budget. It does not see `_G`, frame objects, or the Lua stack.

The host is the only thing that calls into Lua. It does that on the main thread, with `lua_pcall`, and only for a fixed set of operations:

- **Widgets.** `CreateFrame`, `CreateFontString`, `CreateTexture`, then `SetWidth` / `SetHeight` / `SetPoint` / `SetText` and the other methods in the dictionary below. The guest receives a generation-tagged integer handle. The real frame stays in the Lua registry under `UIParent`.
- **Units.** `UnitExists`, `UnitHealth`, `UnitHealthMax`, `UnitPower`, `UnitPowerMax`, `UnitPowerType`, `UnitLevel`, `UnitClass`, `UnitRace`, and the dead/player checks. The guest gets numbers and flags. `UnitName` is a separate host call and never includes a GUID.
- **Chat display.** `print` becomes `DEFAULT_CHAT_FRAME:AddMessage`. A whisper the player confirms is sent by the host (`CGChat::SendNative`) after the confirm dialog. The guest never calls `SendChatMessage`.

What the guest is allowed to see: opaque UI handles, WEV1 event arguments (`nil`, bool, number, text, truncated text, or `Redacted`), unit snapshots (health, power, level, class id, race id, flags), names from the name call, storage bytes it wrote itself, and the text it put on its own widgets. It does not see GUIDs, positions, other addons' memory, or the Lua taint state beyond the host applying a taint scope around those calls.

## Package and build

```text
MyPort/
├── addon.toml
└── addon.wasm
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
units.read = true
chat.read = true
chat.send = true
storage.read = true
storage.write = true

[limits]
memory_mb = 32
fuel = 20000000
storage_kb = 1024
max_ui_handles = 256
max_events = 128
```

`api_version` must be `"2"` for this contract. A v1 module relabeled as 2 fails to load if it does not export `wow_on_event_v2`. The id is lowercase letters, digits, `.`, `-`, and `_`.

Permissions in the manifest are a declaration, not a grant. `ui.create`, `chat.print`, and `profiling.read` are available when declared. `units.read`, `chat.read`, `chat.send`, `storage.read`, `storage.write`, `auction.read`, `auction.write`, and `combat.read` stay denied until:

```text
wasmgrant <id> <permission>
```

Grants are stored per addon id and module hash in `wasm-permissions.cfg`. A deny follows the addon id across rebuilds.

Limit keys are requests. The host clamps them: memory to 256 MB (default 32), fuel per callback to 50,000,000 (default 20,000,000), storage to 4 MB (default 1 MB), UI handles to 256, event subscriptions to 128. Unknown permission names and unknown limit keys reject the manifest.

```bash
export PATH="$HOME/.cargo/bin:$PATH"
rustup target add wasm32-unknown-unknown
cargo build --target wasm32-unknown-unknown --release
```

Copy `target/wasm32-unknown-unknown/release/<crate>.wasm` to `addon.wasm` beside `addon.toml`. `Addons/Wasm/build.sh` does that for the in-tree examples and writes packages under `Addons/Wasm/dist`.

The crate is a `cdylib` depending on `Addons/Wasm/sdk/wow-addon-sdk`. The module must export `memory`. Imports outside the `wow` module, including WASI, fail validation.

## Exports

`wow_addon_v2!` exports `wow_on_load` and `wow_on_event_v2`. `wow_on_update` is optional and is not part of the macro. `wow_alloc` and `wow_free` are exported from the SDK crate. The host uses them to copy an event payload into guest memory and to free it afterward. A v2 addon missing `wow_on_event_v2` does not finish loading. Event delivery is skipped if `wow_alloc` is missing.

```rust
use wow_addon_sdk::ui::*;
use wow_addon_sdk::*;

fn on_load() -> i32 {
    let _ = subscribe("PLAYER_ENTERING_WORLD");
    0
}

fn on_event(name: &str, args: &[EventArg]) -> i32 {
    let _ = (name, args);
    0
}

#[no_mangle]
pub extern "C" fn wow_on_update() -> i32 { 0 }

wow_addon_v2!(on_load, on_event);
```

`wow_on_load` runs once and must return `0`. A negative return fails the addon. `wow_on_event_v2` receives a WEV1 blob. The macro decodes it and calls `on_event(name, args)`. `wow_on_update`, when exported, is called about five times a second (every 200 ms) on the main thread. There is no background thread.

WEV1 argument tags, as `EventArg`: `Nil`, `Bool`, `Number(f64)`, `Text`, `Truncated(text, original_len)` past 1024 bytes, `Redacted`. A payload that would pass 4096 bytes collapses the extra argument to `Redacted`.

## Lua to host

This is a dictionary, not a compatibility layer. The left column is the 3.3.5 call. The middle column is what the guest calls. The right column is what the host does.

| Lua | Guest | Host |
|---|---|---|
| `CreateFrame("Frame"\|"Button"\|"StatusBar"\|"EditBox"\|"ScrollFrame", nil, parent)` | `Frame::new` / `button` / `status_bar` / `edit_box` / `scroll_frame`, or `ui::spawn` | `CreateFrame` of that type. Parent `0` is `UIParent`. A status bar also gets `Interface\TargetingFrame\UI-StatusBar` and a 0..1 range. |
| `parent:CreateFontString` | `Frame::font_string(parent)` | `CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")` |
| `parent:CreateTexture` | `Frame::texture(parent)` | `CreateTexture(nil, "ARTWORK")` |
| `SetSize` / `SetWidth` / `SetHeight` | `set_size(handle, w, h)` | Style op 1. `SetWidth` and `SetHeight`. Each side is 1..=2000. |
| `SetPoint(point, relative, relPoint, x, y)` | `set_point(handle, point, relative, rel_point, x, y)` | Style op 2. `ClearAllPoints` then `SetPoint`. Relative `0` is the parent. Offsets are -4000..=4000. |
| `SetAlpha(0..1)` | `Frame::set_alpha_millis` (op 12) | Thousandths 0..=1000. 1000 is opaque. |
| `Show` / `Hide` | `set_shown(handle, true\|false)` | Style op 4, one byte. |
| `EnableMouse` | `Frame::set_mouse` (op 13) | `EnableMouse`. |
| `SetBackdrop` / `SetBackdropColor` | `set_backdrop(handle, r, g, b, a)` | Style op 8. Backdrop files are fixed. Border color is black. Frames, buttons, and status bars only. |
| `SetFrameStrata` / `SetFrameLevel` | `set_strata` / `set_level` | Ops 11 and 10. Level is 0..=128. Not valid on a font string or a texture. |
| `FontString:SetText` | `set_text(handle, text)` | Newlines stay. `\|` becomes `/`. Other controls become spaces. Max 4096 bytes. |
| `SetTextColor` | `set_text_color(handle, r, g, b, a)` | Style op 9. Font strings only. |
| `SetFont(path, size, flags)` | `set_font_size(handle, pixels)` | Style op 7. Host always uses `Fonts\FRIZQT__.TTF`, size 6..=64. |
| `SetJustifyH` / `SetJustifyV` | `set_justify(handle, h, v)` | Style op 6. 0 left/top, 1 center/middle, 2 right/bottom. |
| `SetMinMaxValues` + `SetValue` | `set_value(handle, value, min, max)` | Style op 5. Status bars only. `min <= max`. |
| `SetStatusBarColor` | `set_color(handle, r, g, b, a)` | Style op 3. Status bar, texture vertex color, or frame backdrop color, depending on kind. |
| `Texture:SetTexture(path)` | `Frame::set_texture` (op 14) | Path must start with `Interface\` or `Interface/`. No `..` and no `\|`. |
| `EditBox:GetText` | `Frame::get_text()` | Byte length, or `-7` if the 1024-byte SDK buffer is short. |
| `OnEnterPressed` / `OnEscapePressed` / `OnTextChanged` | `Frame::bind("enter_pressed"\|"escape"\|"text_changed")` | Edit boxes only. Subscribe to `WASM_UI`. Args: event name, guest handle. |
| `Button OnClick` / `OnEnter` / `OnLeave` | `bind("click"\|"enter"\|"leave")` | `OnClick` is registered on buttons in this client. A click bind on a plain frame fails. |
| `frame:RegisterEvent` | `subscribe("UNIT_HEALTH")` | `UNIT_*`, `CHAT_MSG_*`, and `PLAYER_*`, plus a fixed list (`BAG_UPDATE`, `PLAYER_LOGIN`, `AUCTION_HOUSE_SHOW`, `WASM_UI`, `WASM_CONFIRM_RESULT`, combat log). Unknown names return `-9`. |
| `UnitHealth` and the rest of the snapshot | `units::snapshot(&["player", "target"])` | One host call. Needs `units.read`. No names and no GUIDs in this blob. |
| `UnitName` | `units::names(&["player"])` | Same token allowlist. Needs `units.read`. Names only. |
| `SendChatMessage(text, "WHISPER", nil, target)` | `confirm::chat_send(7, target, message)` | Needs `chat.send`. The player still confirms. Result is `WASM_CONFIRM_RESULT`. |
| `SavedVariables` / per character | `storage::Scope::Account` / `Character` | Reads need `storage.read`. Writes need `storage.write`. Keys up to 128 bytes, values up to 64 KB. |

Anchors: 0 `TOPLEFT`, 1 `TOP`, 2 `TOPRIGHT`, 3 `LEFT`, 4 `CENTER`, 5 `RIGHT`, 6 `BOTTOMLEFT`, 7 `BOTTOM`, 8 `BOTTOMRIGHT`.

Strata byte: 0 `BACKGROUND`, 1 `LOW`, 2 `MEDIUM`, 3 `HIGH`, 4 `DIALOG`, 5 `FULLSCREEN`, 6 `FULLSCREEN_DIALOG`, 7 `TOOLTIP`. The SDK re-exports through `STRATA_DIALOG`. Pass `5`, `6`, or `7` yourself for the three higher strata.

## Frozen ABI

Integers in a style blob are little-endian. Strings are pointer plus length into guest memory. A positive `ui_spawn` return is a handle: high 15 bits generation, low 16 bits slot. A handle from a previous UI session is `-4`.

Every spawn and style call needs `ui.create`. The style blob is at most 64 bytes. Edit boxes and scroll frames can parent other widgets, same as frames.

| Call | Contract |
|---|---|
| `ui_spawn(parent, kind)` | `frame`, `fontstring`, `texture`, `statusbar`, `button`, `editbox`, `scrollframe`. Parent `0` is `UIParent`. |
| `ui_style(handle, op, blob)` | Op 1 size (`i32` w, h). Op 2 point (`u8` point, `u8` relPoint, `u16` 0, `i32` relHandle, `i32` x, `i32` y). Op 3 color rgba. Op 4 shown `u8`. Op 5 value, min, max. Op 6 justify (`u8` h, `u8` v). Op 7 font size. Op 8 backdrop rgba. Op 9 text color. Op 10 level. Op 11 strata. Op 12 alpha thousandths. Op 13 mouse `u8`. Op 14 texture path bytes. |
| `ui_set_text` | Keeps newlines. Replaces `\|` with `/`. |
| `ui_bind(handle, event)` | `click`, `enter`, `leave`, `escape`, `enter_pressed`, `text_changed`. Subscribe to `WASM_UI`. |
| `ui_get_text(handle, buf)` | Edit boxes. Returns the byte length written. |
| `unit_names(tokens)` | `u32` count, then `u32` tokenIndex, `u16` nameLen, UTF-8 bytes. Needs `units.read`. |
| `confirm::chat_send(7, target, message)` | Whisper. `CHAT_MSG_*` args are `Redacted` without `chat.read`. `UNIT_*` args are redacted without `units.read`. |
| `storage::Scope` | `Account = 0`, `Character = 1`. |

Unit tokens: `player`, `pet`, `target`, `focus`, `mouseover`, `targettarget`. Numbered: `party1`–`party4`, `partypet1`–`partypet4`, `raid1`–`raid40`, `raidpet1`–`raidpet40`, `boss1`–`boss5`, `arena1`–`arena5`. At most 64 tokens per call.

```rust
use wow_addon_sdk::frame::{self, Frame};

let panel = Frame::new(0)?;
panel.set_size(220, 48);
panel.set_point(frame::TOPLEFT, 0, frame::LEFT, 16, 90);
panel.set_backdrop(14, 14, 16, 220);
panel.set_strata(frame::STRATA_HIGH);
panel.set_alpha_millis(1000);
panel.set_mouse(true);

let icon = Frame::texture(panel.handle())?;
icon.set_texture(r"Interface\Icons\INV_Misc_QuestionMark");
```

`Frame` methods return `i32` (`0` or a negative host code), except constructors and `get_text()`, which return `Result`.

| Code | Meaning |
|---|---|
| `0` | ok (spawn and some requests return a positive id instead) |
| `-2` | permission denied |
| `-3` | bad memory |
| `-4` | bad handle |
| `-5` | bad argument |
| `-6` | limit |
| `-7` | buffer too small |
| `-8` | unavailable |
| `-9` | unknown event |
| `-10` | storage key missing |

A trap or an out-of-fuel callback faults only that addon. `-1` is a generic failure.

## How a port is structured

One crate, `api_version = "2"`, `wow_addon_v2!`, and `wow_on_update` for refresh. Build the chrome in `on_load`. If `units::snapshot` or a chat payload comes back denied or `Redacted`, leave the placeholders and return `0`. Do not fail `on_load` because `units.read` or `chat.read` is still denied. After `wasmgrant`, the next update or event fills the data.

```rust
fn on_load() -> i32 {
    if let Err(code) = build_chrome() { return code; }
    for event in ["PLAYER_ENTERING_WORLD", "UNIT_HEALTH", "CHAT_MSG_WHISPER"] {
        let code = subscribe(event);
        if code != 0 { return code; }
    }
    refresh()
}

fn refresh() -> i32 {
    match units::snapshot(&["player", "target"]) {
        Ok(list) => paint(&list),
        Err(_) => 0,
    }
}

fn on_event(name: &str, args: &[EventArg]) -> i32 {
    if matches!(args.first(), Some(EventArg::Redacted)) { return 0; }
    let _ = name;
    refresh()
}

#[no_mangle]
pub extern "C" fn wow_on_update() -> i32 { refresh() }
```

A whisper reply, once `chat.send` is granted, is `confirm::chat_send(7, who, text)`. The player still has to accept the dialog (about 750 ms before Y or Enter works; N or Escape denies). History that must survive a reload goes through `storage::set_str(storage::Scope::Character, key, text)`.

## Showing ElvUI and WIM

This tree ships clean-room ports. They are not the upstream Lua addons and they do not contain that source.

| Port | Path | Id | What it draws |
|---|---|---|---|
| ElvUI | `Addons/Wasm/ports/elvui` | `port.elvui` | Player, target, target-of-target, focus, party, and a top data bar |
| WIM | `Addons/Wasm/ports/wim` | `port.wim` | Per-person whisper windows, saved history, enter to send, escape to hide |

```text
wasmgrant port.elvui units.read
wasmgrant port.wim chat.read
wasmgrant port.wim chat.send
wasmgrant port.wim storage.read
wasmgrant port.wim storage.write
```

Without `units.read`, the ElvUI frames still show and the numbers stay empty. Without `chat.read`, WIM still loads and whisper text stays redacted. `chat.send` only queues the confirm dialog. Storage grants are what let WIM keep history on the character.

## Intentionally not Lua

- No secure action buttons, no `SecureActionButtonTemplate`, no macro text, no `CastSpellByName` from the guest.
- No arbitrary Lua. The guest cannot `RunScript`, `loadstring`, or call a FrameXML function by name.
- No WASI, no files, no sockets. Imports outside module `wow` are rejected.
- No widget XML and no inherited templates. Refresh is `wow_on_update` at about 5 Hz, or an event you subscribed to.
- Texture paths are restricted to `Interface\`. The font file is chosen by the host.
- Unit GUIDs never leave the host.
