use std::sync::atomic::{AtomicI32, Ordering};
use wow_addon_sdk::lua::{self, Val};
use wow_addon_sdk::*;

const TOKENS: [&str; 6] = ["player", "target", "targettarget", "focus", "party1", "party2"];
const TITLES: [&str; 6] = ["Player", "Target", "Target of Target", "Focus", "Party 1", "Party 2"];

struct Plate {
    root: i32,
    health: i32,
    power: i32,
    name: i32,
    value: i32,
    keep: bool,
}

static COUNT: AtomicI32 = AtomicI32::new(0);

fn plates() -> &'static mut Vec<Plate> {
    static mut PLATES: Vec<Plate> = Vec::new();
    unsafe { &mut *std::ptr::addr_of_mut!(PLATES) }
}

fn unit_num(func: &str, token: &str) -> f64 {
    lua::call(func, &[Val::Str(token.into())]).map(|vals| lua::num(&vals)).unwrap_or(0.0)
}

fn unit_name(token: &str) -> String {
    lua::call("UnitName", &[Val::Str(token.into())]).map(|vals| lua::text(&vals)).unwrap_or_default()
}

fn class_rgb(token: &str) -> (f64, f64, f64) {
    let vals = lua::call("UnitClass", &[Val::Str(token.into())]).unwrap_or_default();
    let id = vals.get(2).and_then(|val| match val { Val::Num(n) => Some(*n as i32), _ => None }).unwrap_or(0);
    match id {
        1 => (0.78, 0.61, 0.43),
        2 => (0.96, 0.55, 0.73),
        3 => (0.67, 0.83, 0.45),
        4 => (1.0, 0.96, 0.41),
        5 => (1.0, 1.0, 1.0),
        6 => (0.77, 0.12, 0.23),
        7 => (0.0, 0.44, 0.87),
        8 => (0.41, 0.80, 0.94),
        9 => (0.58, 0.51, 0.79),
        11 => (1.0, 0.49, 0.04),
        _ => (0.6, 0.6, 0.6),
    }
}

fn build_plate(parent: i32, index: usize) -> Result<Plate, i32> {
    let wide = index < 2;
    let width = if wide { 230.0 } else { 180.0 };
    let height = if wide { 52.0 } else { 36.0 };
    let root = lua::frame("Frame", parent)?;
    lua::set_size(root, width, height)?;
    let (point, rel_point, x, y) = match index {
        0 => ("TOPLEFT", "LEFT", 24.0, 48.0),
        1 => ("TOPRIGHT", "RIGHT", -24.0, 48.0),
        2 => ("TOPLEFT", "BOTTOMLEFT", 0.0, -8.0),
        3 => ("TOPLEFT", "BOTTOMLEFT", 0.0, -8.0),
        _ => ("TOPLEFT", "LEFT", 24.0, -40.0 - (index as f64 - 4.0) * 40.0),
    };
    let rel = if index == 2 { plates().get(1).map(|plate| plate.root).unwrap_or(parent) }
        else if index == 3 { plates().get(0).map(|plate| plate.root).unwrap_or(parent) }
        else { parent };
    lua::set_point(root, point, rel, rel_point, x, y)?;
    lua::backdrop(root, 0.05, 0.05, 0.06, 0.9)?;
    lua::method(root, "SetFrameStrata", &[Val::Str("HIGH".into())])?;

    let health = lua::frame("StatusBar", root)?;
    lua::set_size(health, width - 10.0, if wide { 14.0 } else { 10.0 })?;
    lua::set_point(health, "TOPLEFT", root, "TOPLEFT", 5.0, -18.0)?;
    lua::method(health, "SetStatusBarTexture", &[Val::Str(r"Interface\TargetingFrame\UI-StatusBar".into())])?;
    lua::method(health, "SetMinMaxValues", &[Val::Num(0.0), Val::Num(1.0)])?;
    lua::method(health, "SetStatusBarColor", &[Val::Num(0.2), Val::Num(0.7), Val::Num(0.3), Val::Num(1.0)])?;

    let power = lua::frame("StatusBar", root)?;
    lua::set_size(power, width - 10.0, 6.0)?;
    lua::set_point(power, "BOTTOMLEFT", root, "BOTTOMLEFT", 5.0, 4.0)?;
    lua::method(power, "SetStatusBarTexture", &[Val::Str(r"Interface\TargetingFrame\UI-StatusBar".into())])?;
    lua::method(power, "SetMinMaxValues", &[Val::Num(0.0), Val::Num(1.0)])?;
    lua::method(power, "SetStatusBarColor", &[Val::Num(0.15), Val::Num(0.45), Val::Num(0.9), Val::Num(1.0)])?;

    let name = lua::font_string(root)?;
    lua::set_point(name, "TOPLEFT", root, "TOPLEFT", 6.0, -2.0)?;
    lua::method(name, "SetFont", &[Val::Str(r"Fonts\FRIZQT__.TTF".into()), Val::Num(12.0), Val::Str("".into())])?;
    lua::set_text(name, TITLES[index])?;

    let value = lua::font_string(root)?;
    lua::set_point(value, "TOPRIGHT", root, "TOPRIGHT", -6.0, -2.0)?;
    lua::method(value, "SetFont", &[Val::Str(r"Fonts\FRIZQT__.TTF".into()), Val::Num(11.0), Val::Str("".into())])?;
    lua::method(value, "SetJustifyH", &[Val::Str("RIGHT".into())])?;
    lua::set_text(value, "--")?;

    if index >= 2 { let _ = lua::method(root, "Hide", &[]); }
    Ok(Plate { root, health, power, name, value, keep: index < 2 })
}

fn paint(plate: &Plate, token: &str, title: &str) -> Result<(), i32> {
    let exists = lua::call("UnitExists", &[Val::Str(token.into())])
        .ok()
        .and_then(|vals| match vals.first() { Some(Val::Bool(v)) => Some(*v), Some(Val::Num(n)) => Some(*n != 0.0), _ => None })
        .unwrap_or(false);
    if !exists {
        if plate.keep {
            lua::set_text(plate.name, title)?;
            lua::set_text(plate.value, "--")?;
        } else {
            let _ = lua::method(plate.root, "Hide", &[]);
        }
        return Ok(());
    }
    let _ = lua::method(plate.root, "Show", &[]);
    let health = unit_num("UnitHealth", token);
    let max = unit_num("UnitHealthMax", token).max(1.0);
    let dead = lua::call("UnitIsDead", &[Val::Str(token.into())]).ok().and_then(|vals| match vals.first() {
        Some(Val::Bool(v)) => Some(*v),
        Some(Val::Num(n)) => Some(*n != 0.0),
        _ => None,
    }).unwrap_or(false);
    let (r, g, b) = if dead { (0.45, 0.45, 0.45) } else { class_rgb(token) };
    lua::method(plate.health, "SetMinMaxValues", &[Val::Num(0.0), Val::Num(max)])?;
    lua::method(plate.health, "SetValue", &[Val::Num(if dead { max } else { health })])?;
    lua::method(plate.health, "SetStatusBarColor", &[Val::Num(r), Val::Num(g), Val::Num(b), Val::Num(1.0)])?;
    let power = unit_num("UnitPower", token);
    let power_max = unit_num("UnitPowerMax", token).max(1.0);
    lua::method(plate.power, "SetMinMaxValues", &[Val::Num(0.0), Val::Num(power_max)])?;
    lua::method(plate.power, "SetValue", &[Val::Num(power)])?;
    let named = unit_name(token);
    let level = unit_num("UnitLevel", token) as i32;
    let caption = if named.is_empty() { title.to_string() } else if level > 0 { format!("{named} {level}") } else { named };
    lua::set_text(plate.name, &caption)?;
    let label = if dead { "Dead".to_string() } else { format!("{}%", (health * 100.0 / max) as i32) };
    lua::set_text(plate.value, &label)?;
    Ok(())
}

fn refresh() -> i32 {
    for (index, plate) in plates().iter().enumerate() {
        if let Err(code) = paint(plate, TOKENS[index], TITLES[index]) { return code; }
    }
    0
}

fn on_load() -> i32 {
    let parent = match lua::ui_parent() {
        Ok(handle) => handle,
        Err(code) => return code,
    };
    for index in 0..TOKENS.len() {
        match build_plate(parent, index) {
            Ok(plate) => plates().push(plate),
            Err(code) => return code,
        }
    }
    COUNT.store(plates().len() as i32, Ordering::Relaxed);
    for event in ["PLAYER_ENTERING_WORLD", "PLAYER_TARGET_CHANGED", "PLAYER_FOCUS_CHANGED", "UNIT_HEALTH", "UNIT_MANA", "UNIT_POWER", "UNIT_NAME_UPDATE"] {
        let code = subscribe(event);
        if code != 0 { return code; }
    }
    let _ = print("ElvUI rust frames are up");
    refresh()
}

fn on_event(_: &str, _: &[EventArg]) -> i32 { refresh() }

#[no_mangle]
pub extern "C" fn wow_on_update() -> i32 { refresh() }

#[no_mangle]
pub extern "C" fn wow_plates() -> i32 { COUNT.load(Ordering::Relaxed) }

wow_addon_v2!(on_load, on_event);
