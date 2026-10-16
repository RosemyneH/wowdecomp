mod window;

use details_meter::Arg;
use wow_addon_sdk::lua::{self, Val};
use wow_addon_sdk::*;
use window::Window;

fn slot() -> &'static mut Option<Window> {
    static mut SLOT: Option<Window> = None;
    unsafe { &mut *std::ptr::addr_of_mut!(SLOT) }
}

fn on_load() -> i32 {
    let window = match Window::build() {
        Ok(window) => window,
        Err(code) => return code,
    };
    *slot() = Some(window);
    for event in ["COMBAT_LOG_EVENT_UNFILTERED", "PLAYER_ENTERING_WORLD", "PLAYER_REGEN_DISABLED", "WASM_UI"] {
        let code = subscribe(event);
        if code != 0 { return code; }
    }
    let _ = print("Details is up. wasmgrant port.details combat.read");
    refresh_roster();
    if let Some(window) = slot() { window.tick(game_time(), false); }
    0
}

fn on_event(name: &str, args: &[EventArg]) -> i32 {
    match name {
        "COMBAT_LOG_EVENT_UNFILTERED" => on_combat(args),
        "WASM_UI" => {
            let Some(handle) = args.get(1).and_then(number) else { return 0 };
            if let Some(window) = slot() { window.click(handle); }
        }
        "PLAYER_ENTERING_WORLD" | "PLAYER_REGEN_DISABLED" => refresh_roster(),
        _ => {}
    }
    0
}

fn on_combat(args: &[EventArg]) {
    let started = {
        let Some(window) = slot() else { return };
        if args.iter().any(|arg| matches!(arg, EventArg::Redacted)) {
            window.mark_blocked();
            return;
        }
        let mut buf = [Arg::Nil; 32];
        let count = args.len().min(buf.len());
        for (index, arg) in args.iter().take(count).enumerate() { buf[index] = view(arg); }
        window.ingest(&buf[..count])
    };
    if started { refresh_roster(); }
}

fn on_update() -> i32 {
    let Some(window) = slot() else { return 0 };
    let live = window.in_combat();
    let dirty = window.is_dirty();
    if !live && !dirty { return 0; }
    let now = game_time();
    let locked = if live { group_locked() } else { false };
    window.tick(now, locked);
    0
}

fn view(arg: &EventArg) -> Arg<'_> {
    match arg {
        EventArg::Number(value) => Arg::Num(*value),
        EventArg::Bool(value) => Arg::Num(if *value { 1.0 } else { 0.0 }),
        EventArg::Text(value) | EventArg::Truncated(value, _) => Arg::Text(value),
        EventArg::Nil | EventArg::Redacted => Arg::Nil,
    }
}

fn number(arg: &EventArg) -> Option<i32> {
    match arg { EventArg::Number(value) => Some(*value as i32), _ => None }
}

fn game_time() -> f64 {
    lua::call("GetTime", &[]).map(|vals| lua::num(&vals)).unwrap_or(0.0)
}

fn call_num(name: &str) -> i32 {
    lua::call(name, &[]).map(|vals| lua::num(&vals) as i32).unwrap_or(0)
}

fn affecting(token: &str) -> bool {
    match lua::call("UnitAffectingCombat", &[Val::Str(token.into())]) {
        Ok(vals) => match vals.first() {
            Some(Val::Num(value)) => *value != 0.0,
            Some(Val::Bool(value)) => *value,
            _ => false,
        },
        Err(_) => false,
    }
}

fn group_locked() -> bool {
    if affecting("player") { return true; }
    let raid = call_num("GetNumRaidMembers");
    if raid > 0 {
        for index in 1..=raid.min(40) {
            if affecting(&format!("raid{index}")) { return true; }
        }
        return false;
    }
    let party = call_num("GetNumPartyMembers");
    for index in 1..=party.min(4) {
        if affecting(&format!("party{index}")) { return true; }
    }
    false
}

fn refresh_roster() {
    remember("player");
    let raid = call_num("GetNumRaidMembers");
    if raid > 0 {
        for index in 1..=raid.min(40) { remember(&format!("raid{index}")); }
        return;
    }
    let party = call_num("GetNumPartyMembers");
    for index in 1..=party.min(4) { remember(&format!("party{index}")); }
}

fn remember(token: &str) {
    let name = lua::call("UnitName", &[Val::Str(token.into())]).map(|vals| lua::text(&vals)).unwrap_or_default();
    if name.is_empty() { return; }
    let vals = lua::call("UnitClass", &[Val::Str(token.into())]).unwrap_or_default();
    let id = match vals.get(2) { Some(Val::Num(value)) => *value as i32, _ => 0 };
    if let Some(window) = slot() { window.set_class(name, class_rgb(id)); }
}

fn class_rgb(id: i32) -> [u8; 3] {
    match id {
        1 => [199, 156, 110],
        2 => [245, 140, 186],
        3 => [171, 212, 115],
        4 => [255, 245, 105],
        5 => [255, 255, 255],
        6 => [196, 31, 59],
        7 => [0, 112, 222],
        8 => [105, 204, 240],
        9 => [148, 130, 201],
        11 => [255, 125, 10],
        _ => [140, 140, 140],
    }
}

#[no_mangle]
pub extern "C" fn wow_on_update() -> i32 { on_update() }

wow_addon_v2!(on_load, on_event);
