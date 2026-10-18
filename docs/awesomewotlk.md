# AwesomeWotLK

The client registers an AwesomeWotLK-style Lua API at startup (`WoW/Source/Ui/AwesomeWotlk.cpp`). FrameXML and ordinary Lua addons call it as globals. A WASM addon calls the same names through `awesome.api`. See [Writing addons](../Addons/Wasm/README.md) for the grant.

This is the surface this client implements. It is the set of names AwesomeWotLK addons already use. It is not a claim that every return value matches some other client's build.

Anything outside the list below is a normal missing Lua global. From WASM, `awesome_call` returns `-10` for a name that is not on the list.

## Nameplates

Plates are tracked as they appear, up to 64. Each live plate has a token `nameplate1`, `nameplate2`, and so on. The token stays with that plate frame.

| Call | Returns |
|---|---|
| `C_NamePlate.GetNamePlates()` | A list of the nameplate frame objects currently shown |
| `C_NamePlate.GetNamePlateForUnit(unit)` | The frame for that unit token, or nothing |
| `C_NamePlate.GetNamePlateByGUID(guid)` | The frame for a `0x…` guid or a unit-guid string |
| `C_NamePlate.GetNamePlateTokenByGUID(guid)` | `nameplateN` |

Events:

| Event | Argument |
|---|---|
| `NAME_PLATE_CREATED` | The new nameplate frame |
| `NAME_PLATE_UNIT_ADDED` | `nameplateN` |
| `NAME_PLATE_UNIT_REMOVED` | `nameplateN` |
| `NAME_PLATE_OWNER_CHANGED` | `nameplateN`, when a plate that was already announced changes owner |

From WASM, a nameplate frame comes back as the `nameplateN` token. `UnitTokenFromGUID` returns the same token when the guid belongs to a plate and to no ordinary unit token.

### Layout CVars

These change the plates the client already draws.

| CVar | Default | Effect |
|---|---|---|
| `nameplateDistance` | 41 | How far, in yards, a plate is allowed to show |
| `nameplateStacking` | 0 | `1` stacks plates that overlap |
| `nameplateXSpace` | 130 | Horizontal gap, in pixels, before two plates count as overlapping |
| `nameplateYSpace` | 20 | Vertical gap used while stacking |
| `nameplateUpperBorder` | 30 | Top inset, in pixels, for the stack |
| `nameplateUpperBorderOnlyBoss` | 0 | `1` applies that inset to boss plates only (classification 3) |
| `nameplateOriginPos` | 20 | Extra origin offset, in pixels. Applied when stacking is on, or after you change the CVar |
| `nameplateMaxRaiseDistance` | 200 | How far a stacked plate may be pushed, in pixels |
| `nameplateStackFriendly` | 1 | Include friendly plates in the stack |
| `nameplateStackFriendlyMode` | 1 | `0` uses reaction. `1` treats a plate as friendly when its health-bar green exceeds its red |
| `nameplateHitboxWidth` / `Height` | 0 | Plate size in pixels. `0` leaves the stock size |
| `nameplateFriendlyHitboxWidth` / `Height` | 0 | Friendly override. `0` falls through to the hostile size |

`nameplateSpeedRaise`, `nameplateSpeedReset`, `nameplateSpeedLower`, and `nameplateExtendWorldFrameHeight` are stored so addons can read back what they wrote. The stacker does not animate with those speeds, and it does not grow the world frame from the extension CVar.

## Text to speech

`C_VoiceChat` and `C_TTSSettings` are the speech API.

| Call | Behavior |
|---|---|
| `C_VoiceChat.SpeakText(voiceID, text[, destination, rate, volume])` | Returns an utterance id. On Linux and macOS, queues the line. Omitted rate and volume use the CVars |
| `C_VoiceChat.StopSpeakingText()` | On Linux and macOS, clears the queue and stops the line that is playing |
| `C_VoiceChat.GetTtsVoices()` | List of `{ voiceID, name }` |
| `C_VoiceChat.GetRemoteTtsVoices()` | Same list |
| `C_TTSSettings.GetSpeechRate` / `GetSpeechVolume` / `GetSpeechVoiceID` | The CVars |
| `C_TTSSettings.GetVoiceOptionName()` | Name of the voice selected by `ttsVoice` |
| `C_TTSSettings.SetSpeechRate` / `SetSpeechVolume` / `SetVoiceOption` / `SetVoiceOptionByName` | Write the CVars |
| `C_TTSSettings.SetDefaultSettings()` | Voice 1, rate 0, volume 100 |
| `C_TTSSettings.RefreshVoices()` | Re-reads the system voice list and signals `VOICE_CHAT_TTS_VOICES_UPDATE` when it changed |

Playback signals `VOICE_CHAT_TTS_PLAYBACK_STARTED`, `VOICE_CHAT_TTS_PLAYBACK_FINISHED`, and `VOICE_CHAT_TTS_PLAYBACK_FAILED`. A failure status is `busy` (queue of 4 is full), `failed`, or `unavailable`.

On Linux and macOS the client speaks with `/usr/bin/spd-say` if it is executable, otherwise `espeak-ng`, otherwise `espeak`. Rate is -10..10 and volume is 0..100, mapped into that program's scale. The voice name is passed through when it is not `default`.

On Windows every speak request finishes as `unavailable`. The Lua functions are still there.

| CVar | Default | Meaning |
|---|---|---|
| `ttsVoice` | 1 | Voice id from the list above |
| `ttsSpeed` | 0 | -10..10 |
| `ttsVolume` | 100 | 0..100 |

## Units, items, spells

| Call | Returns |
|---|---|
| `UnitIsControlled(unit)` | `1` when the controlled flag is set, otherwise nothing |
| `UnitIsDisarmed(unit)` | `1` when the disarmed flag is set |
| `UnitIsSilenced(unit)` | `1` when the silenced flag is set |
| `UnitOccupations(unit)` | The unit's NPC flags |
| `UnitOwner(unit)` | Owner name and `0x` guid. Summoner first, then creator. Nothing if both are empty |
| `UnitTokenFromGUID(guid)` | A unit token such as `target`, or `nameplateN` |
| `GetInventoryItemTransmog(unit, slot)` | Visible item id and the first enchant, for the active player only. Slot is 1–19 |
| `GetSpellBaseCooldown(spellId)` | Recovery time in milliseconds, then the start-recovery (global cooldown) time, both from the spell DB. Recovery falls through to the category, then to a triggered spell |
| `GetItemInfoInstant(id or name or item link)` | Item id, class name, subclass name, inventory-type token, icon path, class id, subclass id |

`UnitOwner` is the call that hands a guid to Lua. The ordinary unit snapshot used by WASM addons still omits guids unless the addon is on this API.

## Window and clipboard

`CopyToClipboard(text)` writes the OS clipboard. The text has to be a string of 1 to 8192 bytes.

`FlashWindow()`, `IsWindowFocused()`, and `FocusWindow()` go through SDL: flash until the window is focused, report input focus as `1`, and raise the window. On Windows those three calls do nothing, and `IsWindowFocused` returns nothing. `CopyToClipboard` still works there.

## Interact

`QueueInteract()` picks the nearest visible unit or game object within **20 yards** and remembers it. `QueueInteract(token)` limits that search to one guid. The client then interacts on a later tick.

`/interact` is `SLASH_INTERACTCMD1`. The slash handler runs `SecureCmdOptionParse` when that function exists, so a secure option such as `[@mouseover]` can supply the token. Otherwise the rest of the command is the token.

| CVar | Default | Effect |
|---|---|---|
| `interactionMode` | 1 | `1` keeps the target inside a facing cone. `0` is distance only |
| `interactionAngle` | 60 | Full cone angle in degrees, clamped to 1–180 |

## Camera, player, stance

| CVar | Default | Effect |
|---|---|---|
| `cameraIndirectVisibility` | 0 | `1` lets the camera pass through world objects. See [World](world.md) |
| `cameraFov` | 100 | 1–200, and 100 is 90°. Missing or `0` uses 100, so a new `Config.wtf` is not stuck in the character's feet. See [World](world.md) |
| `showPlayer` | 1 | `0` hides the player model |
| `enableStancePatch` | 0 | After a form or stance spell succeeds, writes the form byte the stock client sometimes leaves stale |
| `cameraIndirectAlpha` | 0.6 | Stored only |
| `MSDFMode` | 1 | Stored only. Distance-field fonts follow `gxMsdfFonts`. See [Fonts](fonts.md) |

`enableStancePatch` knows these spells:

| Spell | Form byte | What it is |
|---|---|---|
| 768 | 1 | Cat |
| 783 | 3 | Travel |
| 1066 | 4 | Aquatic |
| 5487 | 5 | Bear |
| 9634 | 8 | Dire Bear |
| 33891 | 2 | Tree of Life |
| 2457 | 17 | Battle Stance |
| 71 | 18 | Defensive Stance |
| 2458 | 19 | Berserker Stance |

## Ground casts

`/cast [@cursor]` finishes the following cast on the terrain under the mouse. `/cast [@playerlocation]` finishes it at the player's feet. Both go through the terrain-click handler. The marker does not stick around waiting.

## From a WASM addon

Declare the permission, then grant it once in the developer console. Grants are stored per addon id and module hash in `wasm-permissions.cfg`.

```toml
[permissions]
awesome.api = true
```

```text
wasmgrant port.awesome awesome.api
```

```rust
use wow_addon_sdk::awesome;

let plates = awesome::call("C_NamePlate.GetNamePlates", &[])?;
```

`GetCVar` and `SetCVar` through this grant accept only the CVar names on this page. Tables come back as maps. `port.awesome` (`Addons/Wasm/ports/awesome`) flashes the window on a whisper and edits these CVars from a panel. It does not need `lua.full`.

`lua.full` is the other bridge: it can call any 3.3.5 global, including `CastSpellByName`. `awesome.api` cannot. It can call only the list in this page.
