# WASM addon architecture

Where the sandboxed addon runtime sits in the 3.3.5a client. Authoring is [lua-wasm.md](lua-wasm.md). The short package guide is [Addons/Wasm/README.md](../Addons/Wasm/README.md).

## Discovery map

| Concern | Location | Boundary |
|---|---|---|
| Client init / shutdown | `WoW/Source/Client.cpp`: `InitializeClient()`, `WowClientDestroy()` | WASM init after `FrameScript_Initialize(0)`; shutdown before `FrameScript_Destroy()` |
| Lua VM | `Engine/Source/FrameScript/` (`s_context`, `FrameScript_RegisterFunction`) | Untouched. WASM is a parallel runtime. |
| Lua addon loading / TOC | `FrameXML`, glue addon list (`ShutdownAddOns`) | Packages live in `Interface/WasmAddOns/` (fallback `WasmAddOns/`), discovered independently. |
| Event dispatch | `FrameScript_SignalEvent` in `FrameScript.cpp` | Observer fires after Lua listeners. WASM-only events are not dropped when Lua has no listener. |
| Chat | `DEFAULT_CHAT_FRAME:AddMessage`, `CSimpleMessageFrame::AddMessage` | Host calls the Lua method with `lua_pcall`. No interpolated guest strings. |
| Inventory | `WoW/Source/Ui/ContainerFrame_Scripts.cpp` | One host call snapshots bags 0–4. |
| UI ownership | `CSimpleFrame` under `UIParent` | Guests hold generation-tagged handles. `ui_spawn` / `ui_style` build frames, bars, and text. The client maps them to Lua registry refs and hides them on unload. |
| API v2 guest UI | [lua-wasm.md](lua-wasm.md) | Edit boxes, scroll frames, click/enter/escape binds, edit text, and unit names under `units.read`. |
| Logging / config | `ConsoleWrite`, CVars in `Client_CVar.cpp` | Manager logs through host `Log`. Approvals persist in `wasm-permissions.cfg`. |
| Build | Root `CMakeLists.txt`, `cmake/Platform.cmake` | `Engine/Source/WasmAddon` static lib. Wasmtime via `cmake/Wasmtime.cmake`. |

## Data flow

```text
Game event ──▶ FrameScript_SignalEvent ──▶ Lua listeners (unchanged order)
                                        └─▶ observer ─▶ WasmAddonManager (main thread)
                                                             ├─▶ Addon A (own Wasmtime instance + memory)
                                                             ├─▶ Addon B        ▲ fuel + limiter per callback
Host call  ◀── wow.* imports (permission-checked, memory-validated) ──────────┘
           └─▶ IWasmHostServices ─▶ client: Lua/UI layer | tests: mock host
```

## Confirm overlay

An accepted confirm is executed by the host. Chat goes through `CGChat::SendNative`. Auction bids go through `CGAuctionHouse::PlaceBidNative`. Each frame, `WasmAddonClient_ServiceConfirms` ticks the queue and applies a choice recorded by `WasmConfirmUi`. The prompt paints on a screen layer at z 11. Y or Enter accepts and N or Escape denies, after the 750 ms arming delay. `wasmconfirm <id> accept|deny` still works from the developer console.

## Threading and lifetime

`WasmAddonManager` is main-thread only. Guest callbacks never run on worker threads. Events raised reentrantly from inside a host call are queued and delivered after the current callback returns. Each addon owns one Wasmtime instance. Unload calls `wow_on_unload` when that export exists, hides its UI handles, clears subscriptions, and drops the instance. A trap, including fuel exhaustion, faults or suspends only that addon.

`wow_on_update`, when exported, runs about every 200 ms from the same service tick.

## Risks

- Wasmtime is fetched prebuilt (pinned 36.0.0) when no system install exists. Hash pinning is a follow-up.
- API v2 addons receive WEV1 payloads. Chat and unit arguments are redacted unless `chat.read` or `units.read` is granted. `UNIT_*`, `CHAT_MSG_*`, `PLAYER_*`, `NAME_PLATE_*`, and `VOICE_CHAT_*` names can be subscribed.
- `awesome.api` is sensitive. `awesome_call` exposes the AwesomeWotLK whitelist without `lua.full`. Nameplate frames on an event arrive as `nameplateN`.
- Client host services depend on Lua globals (`DEFAULT_CHAT_FRAME`, `UIParent`) that appear during FrameXML load. Early calls return unavailable rather than failing the process.
- Windows, ARM64, and macOS builds use the same CMake and are less verified than the Linux host tests.
