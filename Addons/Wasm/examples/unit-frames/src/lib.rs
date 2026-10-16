use std::sync::atomic::{AtomicI32, Ordering};
use wow_addon_sdk::ui::*;
use wow_addon_sdk::*;

const PANELS: usize = 6;
const WIDGETS: usize = 5;
static HANDLES: [AtomicI32; PANELS * WIDGETS] = [const { AtomicI32::new(0) }; PANELS * WIDGETS];

const TOKENS: [&str; PANELS] = ["player", "target", "party1", "party2", "party3", "party4"];
const TITLES: [&str; PANELS] = ["Player", "Target", "Party 1", "Party 2", "Party 3", "Party 4"];

fn slot(panel: usize, widget: usize) -> i32 {
    HANDLES[panel * WIDGETS + widget].load(Ordering::Relaxed)
}
fn put(panel: usize, widget: usize, handle: i32) {
    HANDLES[panel * WIDGETS + widget].store(handle, Ordering::Relaxed);
}

fn check(code: i32) -> Result<(), i32> {
    if code < 0 { Err(code) } else { Ok(()) }
}

fn place(index: usize) -> (i32, i32, i32, i32) {
    if index == 0 { (16, 90, 220, 48) }
    else if index == 1 { (16, 28, 220, 48) }
    else { (252, 90 - (index as i32 - 2) * 40, 180, 36) }
}

fn build(index: usize) -> Result<(), i32> {
    let (x, y, width, height) = place(index);
    let root = spawn(0, "frame");
    if root <= 0 { return Err(root); }
    put(index, 0, root);
    check(set_size(root, width, height))?;
    check(set_point(root, TOPLEFT, 0, LEFT, x, y))?;
    check(set_backdrop(root, 14, 14, 16, 220))?;
    check(set_strata(root, STRATA_HIGH))?;
    let health = spawn(root, "statusbar");
    if health <= 0 { return Err(health); }
    put(index, 1, health);
    check(set_size(health, width - 10, if height > 40 { 14 } else { 10 }))?;
    check(set_point(health, TOPLEFT, 0, TOPLEFT, 5, -18))?;
    check(set_color(health, 70, 160, 90, 255))?;
    check(set_value(health, 1, 0, 1))?;
    let power = spawn(root, "statusbar");
    if power <= 0 { return Err(power); }
    put(index, 2, power);
    check(set_size(power, width - 10, 6))?;
    check(set_point(power, BOTTOMLEFT, 0, BOTTOMLEFT, 5, 4))?;
    check(set_color(power, 40, 120, 220, 255))?;
    check(set_value(power, 1, 0, 1))?;
    let name = spawn(root, "fontstring");
    if name <= 0 { return Err(name); }
    put(index, 3, name);
    check(set_point(name, TOPLEFT, 0, TOPLEFT, 6, -2))?;
    check(set_font_size(name, if height > 40 { 12 } else { 10 }))?;
    check(set_justify(name, JUSTIFY_LEFT, JUSTIFY_TOP))?;
    check(set_text_color(name, 240, 240, 240, 255))?;
    check(ui_set_text(name, TITLES[index]))?;
    let value = spawn(root, "fontstring");
    if value <= 0 { return Err(value); }
    put(index, 4, value);
    check(set_point(value, TOPRIGHT, 0, TOPRIGHT, -6, -2))?;
    check(set_font_size(value, 11))?;
    check(set_justify(value, JUSTIFY_RIGHT, JUSTIFY_TOP))?;
    check(set_text_color(value, 210, 210, 210, 255))?;
    check(ui_set_text(value, "--"))?;
    if index >= 2 { check(set_shown(root, false))?; }
    Ok(())
}

fn class_rgb(id: u32) -> (u8, u8, u8) {
    match id {
        1 => (199, 156, 110),
        2 => (245, 140, 186),
        3 => (171, 212, 115),
        4 => (255, 245, 105),
        5 => (255, 255, 255),
        6 => (196, 31, 59),
        7 => (0, 112, 222),
        8 => (105, 204, 240),
        9 => (148, 130, 201),
        11 => (255, 125, 10),
        _ => (150, 150, 150),
    }
}

fn power_rgb(kind: u32) -> (u8, u8, u8) {
    match kind {
        1 => (220, 40, 40),
        2 => (220, 150, 40),
        3 => (255, 220, 30),
        6 => (40, 190, 230),
        _ => (40, 120, 220),
    }
}

fn as_i32(value: u32) -> i32 {
    if value > i32::MAX as u32 { i32::MAX } else { value as i32 }
}

fn paint(index: usize, unit: Option<units::Unit>) -> Result<(), i32> {
    let root = slot(index, 0);
    let health = slot(index, 1);
    let power = slot(index, 2);
    let name = slot(index, 3);
    let value = slot(index, 4);
    let Some(unit) = unit.filter(|unit| unit.exists()) else {
        if index >= 2 { check(set_shown(root, false))?; }
        else {
            check(ui_set_text(name, TITLES[index]))?;
            check(ui_set_text(value, "--"))?;
        }
        return Ok(());
    };
    check(set_shown(root, true))?;
    let (r, g, b) = if unit.dead() { (120, 120, 120) } else { class_rgb(unit.class_id) };
    check(set_color(health, r, g, b, 255))?;
    let max = if unit.health_max == 0 { 1 } else { as_i32(unit.health_max) };
    check(set_value(health, as_i32(unit.health).min(max), 0, max))?;
    let (pr, pg, pb) = power_rgb(unit.power_type);
    check(set_color(power, pr, pg, pb, 255))?;
    let power_max = if unit.power_max == 0 { 1 } else { as_i32(unit.power_max) };
    check(set_value(power, as_i32(unit.power).min(power_max), 0, power_max))?;
    let title = if unit.level > 0 { format!("{} {}", TITLES[index], unit.level) } else { TITLES[index].to_string() };
    check(ui_set_text(name, &title))?;
    let label = if unit.dead() {
        "Dead".to_string()
    } else if unit.health_max == 0 {
        "--".to_string()
    } else {
        format!("{}%", unit.health.saturating_mul(100) / unit.health_max)
    };
    check(ui_set_text(value, &label))?;
    Ok(())
}

fn refresh() -> i32 {
    let painted = match units::snapshot(&TOKENS) {
        Ok(list) => {
            for index in 0..PANELS {
                let unit = list.iter().find(|unit| unit.token_index == index as u32).copied();
                if let Err(code) = paint(index, unit) { return code; }
            }
            0
        }
        Err(_) => {
            for index in 0..PANELS {
                if let Err(code) = paint(index, None) { return code; }
            }
            0
        }
    };
    painted
}

fn on_load() -> i32 {
    for index in 0..PANELS {
        if let Err(code) = build(index) { return code; }
    }
    for event in ["PLAYER_ENTERING_WORLD", "PLAYER_TARGET_CHANGED", "UNIT_HEALTH", "UNIT_MAXHEALTH",
        "UNIT_MANA", "UNIT_ENERGY", "UNIT_RAGE", "UNIT_RUNIC_POWER", "UNIT_FOCUS", "UNIT_DISPLAYPOWER"] {
        let code = subscribe(event);
        if code != 0 { return code; }
    }
    let _ = print("Unit frames are up. wasmgrant example.unit-frames units.read");
    refresh()
}

fn on_event(name: &str, args: &[EventArg]) -> i32 {
    if let Some(rest) = name.strip_prefix("UNIT_") {
        if !rest.is_empty() {
            if let Some(EventArg::Text(unit)) = args.first() {
                if unit != "player" && unit != "target" && !unit.starts_with("party") { return 0; }
            }
        }
    }
    refresh()
}

#[no_mangle]
pub extern "C" fn wow_on_update() -> i32 { refresh() }

wow_addon_v2!(on_load, on_event);
