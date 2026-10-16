mod actionbars;
mod auras;
mod bags;
mod blizzard;
mod chat;
mod elv;
mod databars;
mod datatexts;
mod maps;
mod skins;
mod tooltip;
mod unitframes;

use wow_addon_sdk::*;

fn on_load() -> i32 {
    let parent = match elv::parent() {
        Ok(handle) => handle,
        Err(code) => return code,
    };
    blizzard::enable();
    if let Err(code) = unitframes::enable(parent) { return code; }
    if let Err(code) = actionbars::enable(parent) { return code; }
    if let Err(code) = chat::enable(parent) { return code; }
    if let Err(code) = datatexts::enable(parent) { return code; }
    if let Err(code) = databars::enable(parent) { return code; }
    if let Err(code) = auras::enable(parent) { return code; }
    if let Err(code) = maps::enable(parent) { return code; }
    if let Err(code) = bags::enable(parent) { return code; }
    if let Err(code) = tooltip::enable() { return code; }
    skins::enable();
    for event in [
        "PLAYER_ENTERING_WORLD", "PLAYER_TARGET_CHANGED", "PLAYER_FOCUS_CHANGED",
        "UNIT_HEALTH", "UNIT_MANA", "UNIT_ENERGY", "UNIT_RAGE", "UNIT_RUNIC_POWER",
        "UNIT_NAME_UPDATE", "PARTY_MEMBERS_CHANGED", "RAID_ROSTER_UPDATE",
    ] {
        let code = subscribe(event);
        if code != 0 { return code; }
    }
    let _ = print("ElvUI is up");
    update()
}

fn update() -> i32 {
    let frames = unitframes::update();
    if frames != 0 { return frames; }
    let texts = datatexts::update();
    if texts != 0 { return texts; }
    databars::update()
}

fn on_event(_: &str, _: &[EventArg]) -> i32 { update() }

#[no_mangle]
pub extern "C" fn wow_on_update() -> i32 { update() }

wow_addon_v2!(on_load, on_event);
