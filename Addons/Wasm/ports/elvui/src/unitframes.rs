use wow_addon_sdk::lua::{self, Val};
use crate::elv as core;

pub struct Unit {
    pub token: &'static str,
    pub root: i32,
    pub health: i32,
    pub power: i32,
    pub name: i32,
    pub value: i32,
    pub cast: i32,
    pub keep: bool,
}

fn plates() -> &'static mut Vec<Unit> {
    static mut UNITS: Vec<Unit> = Vec::new();
    unsafe { &mut *std::ptr::addr_of_mut!(UNITS) }
}

fn build(parent: i32, token: &'static str, width: f64, height: f64, point: &str, rel: i32, rel_point: &str, x: f64, y: f64, keep: bool) -> Result<Unit, i32> {
    let root = lua::frame("Frame", parent)?;
    lua::set_size(root, width, height)?;
    lua::set_point(root, point, rel, rel_point, x, y)?;
    lua::backdrop(root, 0.05, 0.05, 0.055, 0.92)?;
    lua::method(root, "SetFrameStrata", &[Val::Str("LOW".into())])?;
    let health = core::status_bar(root, width - 8.0, height - 22.0)?;
    lua::set_point(health, "TOPLEFT", root, "TOPLEFT", 4.0, -16.0)?;
    let power = core::status_bar(root, width - 8.0, 8.0)?;
    lua::set_point(power, "BOTTOMLEFT", root, "BOTTOMLEFT", 4.0, 3.0)?;
    lua::method(power, "SetStatusBarColor", &[Val::Num(0.15), Val::Num(0.45), Val::Num(0.9), Val::Num(1.0)])?;
    let name = lua::font_string(root)?;
    lua::set_point(name, "TOPLEFT", root, "TOPLEFT", 6.0, -1.0)?;
    lua::method(name, "SetFont", &[Val::Str(r"Fonts\FRIZQT__.TTF".into()), Val::Num(12.0), Val::Str("OUTLINE".into())])?;
    let value = lua::font_string(root)?;
    lua::set_point(value, "TOPRIGHT", root, "TOPRIGHT", -6.0, -1.0)?;
    lua::method(value, "SetJustifyH", &[Val::Str("RIGHT".into())])?;
    lua::method(value, "SetFont", &[Val::Str(r"Fonts\FRIZQT__.TTF".into()), Val::Num(11.0), Val::Str("OUTLINE".into())])?;
    let cast = core::status_bar(root, width, 10.0)?;
    lua::set_point(cast, "TOPLEFT", root, "BOTTOMLEFT", 0.0, -4.0)?;
    lua::method(cast, "SetStatusBarColor", &[Val::Num(0.9), Val::Num(0.7), Val::Num(0.2), Val::Num(1.0)])?;
    lua::method(cast, "Hide", &[])?;
    if !keep { let _ = lua::method(root, "Hide", &[]); }
    Ok(Unit { token, root, health, power, name, value, cast, keep })
}

pub fn enable(parent: i32) -> Result<(), i32> {
    let units = plates();
    units.clear();
    // ʕ •ᴥ•ʔ✿ ElvUI install movers: player -341,138 target 342,138 pet/tot 99 focus 59 party bottom-left ✿ ʕ •ᴥ•ʔ
    units.push(build(parent, "player", 270.0, 54.0, "BOTTOM", parent, "BOTTOM", -341.0, 138.0, true)?);
    units.push(build(parent, "target", 270.0, 54.0, "BOTTOM", parent, "BOTTOM", 342.0, 138.0, true)?);
    units.push(build(parent, "pet", 130.0, 36.0, "BOTTOM", parent, "BOTTOM", -341.0, 99.0, false)?);
    units.push(build(parent, "targettarget", 130.0, 36.0, "BOTTOM", parent, "BOTTOM", 342.0, 99.0, false)?);
    units.push(build(parent, "focus", 180.0, 40.0, "BOTTOM", parent, "BOTTOM", 342.0, 59.0, false)?);
    units.push(build(parent, "focustarget", 130.0, 28.0, "BOTTOM", parent, "BOTTOM", 490.0, 59.0, false)?);
    for i in 1..=4 {
        let token: &'static str = match i { 1 => "party1", 2 => "party2", 3 => "party3", _ => "party4" };
        units.push(build(parent, token, 180.0, 40.0, "BOTTOMLEFT", parent, "BOTTOMLEFT", 4.0, 248.0 + (i as f64 - 1.0) * 46.0, false)?);
    }
    for i in 1..=10 {
        let token: &'static str = match i {
            1 => "raid1", 2 => "raid2", 3 => "raid3", 4 => "raid4", 5 => "raid5",
            6 => "raid6", 7 => "raid7", 8 => "raid8", 9 => "raid9", _ => "raid10",
        };
        let col = (i - 1) % 5;
        let row = (i - 1) / 5;
        units.push(build(parent, token, 90.0, 28.0, "TOPLEFT", parent, "BOTTOMLEFT", 4.0 + col as f64 * 94.0, 482.0 - row as f64 * 32.0, false)?);
    }
    Ok(())
}

fn paint_cast(plate: &Unit) -> Result<(), i32> {
    let vals = lua::call("UnitCastingInfo", &[Val::Str(plate.token.into())]).unwrap_or_default();
    let (start, end) = match (vals.get(4), vals.get(5)) {
        (Some(Val::Num(start)), Some(Val::Num(end))) if *end > *start => (*start, *end),
        _ => {
            let _ = lua::method(plate.cast, "Hide", &[]);
            return Ok(());
        }
    };
    let now = lua::num(&lua::call("GetTime", &[]).unwrap_or_default()) * 1000.0;
    let _ = lua::method(plate.cast, "Show", &[]);
    lua::method(plate.cast, "SetMinMaxValues", &[Val::Num(start), Val::Num(end)])?;
    lua::method(plate.cast, "SetValue", &[Val::Num(now)])?;
    Ok(())
}

fn paint(plate: &Unit) -> Result<(), i32> {
    if !core::exists(plate.token) {
        if plate.keep {
            lua::set_text(plate.name, plate.token)?;
            lua::set_text(plate.value, "--")?;
        } else {
            let _ = lua::method(plate.root, "Hide", &[]);
        }
        let _ = lua::method(plate.cast, "Hide", &[]);
        return Ok(());
    }
    let _ = lua::method(plate.root, "Show", &[]);
    let health = core::number("UnitHealth", plate.token);
    let max = core::number("UnitHealthMax", plate.token).max(1.0);
    let dead = core::dead(plate.token);
    let (r, g, b) = if dead { (0.45, 0.45, 0.45) } else { core::class_color(plate.token) };
    lua::method(plate.health, "SetMinMaxValues", &[Val::Num(0.0), Val::Num(max)])?;
    lua::method(plate.health, "SetValue", &[Val::Num(if dead { max } else { health })])?;
    lua::method(plate.health, "SetStatusBarColor", &[Val::Num(r), Val::Num(g), Val::Num(b), Val::Num(1.0)])?;
    let power = core::number("UnitPower", plate.token);
    let power_max = core::number("UnitPowerMax", plate.token).max(1.0);
    lua::method(plate.power, "SetMinMaxValues", &[Val::Num(0.0), Val::Num(power_max)])?;
    lua::method(plate.power, "SetValue", &[Val::Num(power)])?;
    let named = core::name(plate.token);
    let level = core::number("UnitLevel", plate.token) as i32;
    let caption = if named.is_empty() { plate.token.to_string() } else if level > 0 { format!("{named} {level}") } else { named };
    lua::set_text(plate.name, &caption)?;
    let label = if dead { "Dead".into() } else { format!("{}%", (health * 100.0 / max) as i32) };
    lua::set_text(plate.value, &label)?;
    if plate.token == "player" || plate.token == "target" || plate.token == "focus" { paint_cast(plate)?; }
    Ok(())
}

pub fn update() -> i32 {
    for plate in plates().iter() {
        if let Err(code) = paint(plate) { return code; }
    }
    0
}
