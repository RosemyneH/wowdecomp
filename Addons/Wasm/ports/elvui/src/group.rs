use std::sync::OnceLock;

use wow_addon_sdk::frame::{self, Frame};
use wow_addon_sdk::units::Unit;

const COUNT: usize = 4;
const TOKEN_BASE: u32 = 4;

struct Slot {
    root: Frame,
    health: Frame,
    power: Frame,
    name: Frame,
    percent: Frame,
}

static SLOTS: [OnceLock<Slot>; COUNT] = [
    OnceLock::new(),
    OnceLock::new(),
    OnceLock::new(),
    OnceLock::new(),
];

fn check(code: i32) -> Result<(), i32> {
    if code < 0 { Err(code) } else { Ok(()) }
}

fn host(result: Result<Frame, i32>) -> Result<Frame, i32> {
    result.map_err(|code| if code < 0 { code } else { -1 })
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
        _ => (140, 140, 140),
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

// ʕ •ᴥ•ʔ✿ 48-58-40-(i+1)*38 puts party1 at -88, then steps of 38 ✿ ʕ •ᴥ•ʔ
fn origin_y(index: usize) -> i32 {
    -88 - index as i32 * 38
}

fn party_label(index: usize) -> String {
    format!("Party {}", index + 1)
}

fn build_slot(index: usize) -> Result<(), i32> {
    let root = host(Frame::new(0))?;
    let parent = root.handle();
    check(root.set_size(190, 34))?;
    check(root.set_point(frame::TOPLEFT, 0, frame::LEFT, 24, origin_y(index)))?;
    check(root.set_backdrop(12, 12, 14, 220))?;
    check(root.set_strata(frame::STRATA_MEDIUM))?;

    let health = host(Frame::status_bar(parent))?;
    check(health.set_size(182, 12))?;
    check(health.set_point(frame::TOPLEFT, parent, frame::TOPLEFT, 4, -12))?;
    check(health.set_color(140, 140, 140, 255))?;
    check(health.set_value(0, 0, 1))?;

    let power = host(Frame::status_bar(parent))?;
    check(power.set_size(182, 4))?;
    check(power.set_point(frame::BOTTOMLEFT, parent, frame::BOTTOMLEFT, 4, 2))?;
    check(power.set_color(40, 120, 220, 255))?;
    check(power.set_value(0, 0, 1))?;

    let name = host(Frame::font_string(parent))?;
    check(name.set_point(frame::TOPLEFT, parent, frame::TOPLEFT, 6, -1))?;
    check(name.set_font_size(10))?;
    check(name.set_justify(frame::JUSTIFY_LEFT, frame::JUSTIFY_TOP))?;
    check(name.set_text_color(236, 236, 236, 255))?;
    check(name.set_text(&party_label(index)))?;

    let percent = host(Frame::font_string(parent))?;
    check(percent.set_point(frame::TOPRIGHT, parent, frame::TOPRIGHT, -6, -1))?;
    check(percent.set_font_size(10))?;
    check(percent.set_justify(frame::JUSTIFY_RIGHT, frame::JUSTIFY_TOP))?;
    check(percent.set_text_color(210, 210, 210, 255))?;
    check(percent.set_text("--"))?;

    check(root.set_shown(false))?;
    let _ = SLOTS[index].set(Slot { root, health, power, name, percent });
    Ok(())
}

pub fn build() -> i32 {
    for index in 0..COUNT {
        if let Err(code) = build_slot(index) {
            return code;
        }
    }
    0
}

fn paint(index: usize, list: &[Unit], names: &[(u32, String)]) -> Result<(), i32> {
    let Some(slot) = SLOTS[index].get() else { return Ok(()) };
    let token = TOKEN_BASE + index as u32;
    let Some(unit) = list.iter().find(|unit| unit.token_index == token && unit.exists()) else {
        check(slot.root.set_shown(false))?;
        return Ok(());
    };
    check(slot.root.set_shown(true))?;
    let (r, g, b) = if unit.dead() { (140, 140, 140) } else { class_rgb(unit.class_id) };
    check(slot.health.set_color(r, g, b, 255))?;
    check(slot.name.set_text_color(r, g, b, 255))?;
    let max = if unit.health_max == 0 { 1 } else { as_i32(unit.health_max) };
    check(slot.health.set_value(as_i32(unit.health).min(max), 0, max))?;
    let (pr, pg, pb) = power_rgb(unit.power_type);
    check(slot.power.set_color(pr, pg, pb, 255))?;
    let power_max = if unit.power_max == 0 { 1 } else { as_i32(unit.power_max) };
    check(slot.power.set_value(as_i32(unit.power).min(power_max), 0, power_max))?;
    let fallback = party_label(index);
    let title = names
        .iter()
        .find(|(id, _)| *id == token)
        .map(|(_, text)| text.as_str())
        .filter(|text| !text.is_empty())
        .unwrap_or(fallback.as_str());
    check(slot.name.set_text(title))?;
    let label = if unit.dead() {
        "Dead".to_string()
    } else if unit.health_max == 0 {
        "--".to_string()
    } else {
        format!("{}%", unit.health.saturating_mul(100) / unit.health_max)
    };
    check(slot.percent.set_text(&label))?;
    Ok(())
}

pub fn refresh(list: &[wow_addon_sdk::units::Unit], names: &[(u32, String)]) -> i32 {
    for index in 0..COUNT {
        if let Err(code) = paint(index, list, names) {
            return code;
        }
    }
    0
}
