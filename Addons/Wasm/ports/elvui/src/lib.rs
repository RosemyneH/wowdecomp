use std::sync::OnceLock;
use std::sync::atomic::{AtomicI32, Ordering};

use wow_addon_sdk::frame::{self, Frame};
use wow_addon_sdk::units::{self, Unit};
use wow_addon_sdk::*;

mod group;
mod panels;

pub const TOKENS: [&str; 8] = [
    "player", "target", "targettarget", "focus", "party1", "party2", "party3", "party4",
];

const TITLES: [&str; 4] = ["Player", "Target", "Target of Target", "Focus"];
const PLATES: usize = 4;
const PARTS: usize = 5;
const ROOT: usize = 0;

static HANDLES: [AtomicI32; PLATES * PARTS] = [const { AtomicI32::new(0) }; PLATES * PARTS];
static SLOTS: [OnceLock<Plate>; PLATES] = [
    OnceLock::new(),
    OnceLock::new(),
    OnceLock::new(),
    OnceLock::new(),
];

const EVENTS: [&str; 12] = [
    "PLAYER_ENTERING_WORLD",
    "PLAYER_TARGET_CHANGED",
    "PLAYER_FOCUS_CHANGED",
    "UNIT_HEALTH",
    "UNIT_MAXHEALTH",
    "UNIT_MANA",
    "UNIT_ENERGY",
    "UNIT_RAGE",
    "UNIT_RUNIC_POWER",
    "UNIT_FOCUS",
    "UNIT_DISPLAYPOWER",
    "UNIT_NAME_UPDATE",
];

#[derive(Clone, Copy)]
struct Plate {
    root: Frame,
    health: Frame,
    power: Frame,
    name: Frame,
    value: Frame,
}

fn check(code: i32) -> Result<(), i32> {
    if code < 0 { Err(code) } else { Ok(()) }
}

fn host(result: Result<Frame, i32>) -> Result<Frame, i32> {
    result.map_err(|code| if code < 0 { code } else { -1 })
}

fn remember(index: usize, plate: Plate) {
    let handles = [
        plate.root.handle(),
        plate.health.handle(),
        plate.power.handle(),
        plate.name.handle(),
        plate.value.handle(),
    ];
    for (part, handle) in handles.into_iter().enumerate() {
        HANDLES[index * PARTS + part].store(handle, Ordering::Relaxed);
    }
    let _ = SLOTS[index].set(plate);
}

fn slot(index: usize, part: usize) -> i32 {
    HANDLES[index * PARTS + part].load(Ordering::Relaxed)
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

fn clamped(value: u32) -> i32 {
    i32::try_from(value).unwrap_or(i32::MAX)
}

// ʕ •ᴥ•ʔ✿ name band, then health, then a 6px power strip on the bottom edge ✿ ʕ •ᴥ•ʔ
fn stack(height: i32) -> (i32, i32, i32) {
    let (name_band, power_y) = if height >= 48 { (18, 4) } else if height >= 32 { (14, 2) } else { (14, 1) };
    (-name_band, height - name_band - 6 - power_y, power_y)
}

fn make_plate(index: usize, width: i32, height: i32, shown: bool) -> Result<Plate, i32> {
    let root = host(Frame::new(0))?;
    let parent = root.handle();
    check(root.set_size(width, height))?;
    check(root.set_backdrop(12, 12, 14, 230))?;
    check(root.set_strata(frame::STRATA_HIGH))?;
    check(root.set_shown(shown))?;

    let (health_y, health_h, power_y) = stack(height);
    let bar_w = width - 8;
    let health = host(Frame::status_bar(parent))?;
    check(health.set_size(bar_w, health_h))?;
    check(health.set_point(frame::TOPLEFT, parent, frame::TOPLEFT, 4, health_y))?;
    check(health.set_color(40, 40, 44, 255))?;
    check(health.set_value(0, 0, 1))?;

    let power = host(Frame::status_bar(parent))?;
    check(power.set_size(bar_w, 6))?;
    check(power.set_point(frame::BOTTOMLEFT, parent, frame::BOTTOMLEFT, 4, power_y))?;
    check(power.set_color(40, 120, 220, 255))?;
    check(power.set_value(0, 0, 1))?;

    let name = host(Frame::font_string(parent))?;
    check(name.set_size(width - 68, 12))?;
    check(name.set_point(frame::TOPLEFT, parent, frame::TOPLEFT, 6, -1))?;
    check(name.set_font_size(12))?;
    check(name.set_justify(frame::JUSTIFY_LEFT, frame::JUSTIFY_TOP))?;
    check(name.set_text_color(236, 236, 236, 255))?;
    check(name.set_text(TITLES[index]))?;

    let value = host(Frame::font_string(parent))?;
    check(value.set_size(56, 12))?;
    check(value.set_point(frame::TOPRIGHT, parent, frame::TOPRIGHT, -6, -1))?;
    check(value.set_font_size(11))?;
    check(value.set_justify(frame::JUSTIFY_RIGHT, frame::JUSTIFY_TOP))?;
    check(value.set_text_color(210, 210, 210, 255))?;
    check(value.set_text("--"))?;

    Ok(Plate { root, health, power, name, value })
}

fn build_plates() -> Result<(), i32> {
    let player = make_plate(0, 230, 52, true)?;
    check(player.root.set_point(frame::TOPLEFT, 0, frame::LEFT, 24, 48))?;
    remember(0, player);

    let target = make_plate(1, 230, 52, true)?;
    check(target.root.set_point(frame::TOPRIGHT, 0, frame::RIGHT, -24, 48))?;
    remember(1, target);

    let tot = make_plate(2, 150, 28, false)?;
    check(tot.root.set_point(frame::TOPLEFT, slot(1, ROOT), frame::TOPLEFT, 0, -56))?;
    remember(2, tot);

    let focus = make_plate(3, 170, 32, false)?;
    check(focus.root.set_point(frame::TOPLEFT, slot(0, ROOT), frame::TOPLEFT, 0, -58))?;
    remember(3, focus);
    Ok(())
}

fn caption(index: usize, names: &[(u32, String)], level: u32) -> String {
    let found = names.iter().find(|(token, _)| *token == index as u32).map(|(_, text)| text.as_str());
    let base = found.filter(|text| !text.is_empty()).unwrap_or(TITLES[index]);
    if level > 0 { format!("{base} {level}") } else { base.to_string() }
}

fn show_missing(plate: &Plate, title: &str) -> Result<(), i32> {
    check(plate.root.set_shown(true))?;
    check(plate.health.set_color(40, 40, 44, 255))?;
    check(plate.health.set_value(0, 0, 1))?;
    check(plate.power.set_color(40, 120, 220, 255))?;
    check(plate.power.set_value(0, 0, 1))?;
    check(plate.name.set_text_color(220, 220, 220, 255))?;
    check(plate.name.set_text(title))?;
    check(plate.value.set_text_color(170, 170, 170, 255))?;
    check(plate.value.set_text("--"))?;
    Ok(())
}

fn show_unit(plate: &Plate, index: usize, unit: Unit, names: &[(u32, String)]) -> Result<(), i32> {
    check(plate.root.set_shown(true))?;
    let (r, g, b) = if unit.dead() { (120, 120, 120) } else { class_rgb(unit.class_id) };
    check(plate.health.set_color(r, g, b, 255))?;
    check(plate.name.set_text_color(r, g, b, 255))?;
    if unit.dead() {
        check(plate.health.set_value(1, 0, 1))?;
    } else {
        let max = if unit.health_max == 0 { 1 } else { clamped(unit.health_max) };
        check(plate.health.set_value(clamped(unit.health).min(max), 0, max))?;
    }
    let (pr, pg, pb) = power_rgb(unit.power_type);
    check(plate.power.set_color(pr, pg, pb, 255))?;
    let power_max = if unit.power_max == 0 { 1 } else { clamped(unit.power_max) };
    check(plate.power.set_value(clamped(unit.power).min(power_max), 0, power_max))?;
    check(plate.name.set_text(&caption(index, names, unit.level)))?;
    if unit.dead() {
        check(plate.value.set_text_color(190, 190, 190, 255))?;
        check(plate.value.set_text("Dead"))?;
    } else if unit.health_max == 0 {
        check(plate.value.set_text_color(210, 210, 210, 255))?;
        check(plate.value.set_text("--"))?;
    } else {
        let pct = unit.health as u64 * 100 / unit.health_max as u64;
        check(plate.value.set_text_color(230, 230, 230, 255))?;
        check(plate.value.set_text(&format!("{pct}%")))?;
    }
    Ok(())
}

fn paint_plate(index: usize, list: &[Unit], names: &[(u32, String)]) -> Result<(), i32> {
    let Some(plate) = SLOTS[index].get() else { return Ok(()) };
    let unit = list.iter().copied().find(|unit| unit.token_index == index as u32 && unit.exists());
    match unit {
        Some(unit) => show_unit(plate, index, unit, names),
        None if index >= 2 => check(plate.root.set_shown(false)),
        None => show_missing(plate, TITLES[index]),
    }
}

fn refresh() -> i32 {
    // ʕ •ᴥ•ʔ✿ A denied units.read leaves the plates as placeholders instead of failing the addon ✿ ʕ •ᴥ•ʔ
    let list = units::snapshot(&TOKENS).unwrap_or_default();
    let names = units::names(&TOKENS).unwrap_or_default();
    match (|| -> Result<(), i32> {
        check(group::refresh(&list, &names))?;
        check(panels::refresh())?;
        for index in 0..PLATES {
            paint_plate(index, &list, &names)?;
        }
        Ok(())
    })() {
        Ok(()) => 0,
        Err(code) => code,
    }
}

fn on_load() -> i32 {
    match (|| -> Result<(), i32> {
        build_plates()?;
        check(group::build())?;
        check(panels::build())?;
        for event in EVENTS {
            check(subscribe(event))?;
        }
        let _ = print("ElvUI frames are up. wasmgrant port.elvui units.read");
        check(refresh())
    })() {
        Ok(()) => 0,
        Err(code) => code,
    }
}

fn on_event(_name: &str, _args: &[EventArg]) -> i32 {
    refresh()
}

#[no_mangle]
pub extern "C" fn wow_on_update() -> i32 {
    refresh()
}

wow_addon_v2!(on_load, on_event);
