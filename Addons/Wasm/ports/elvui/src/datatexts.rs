use wow_addon_sdk::lua::{self, Val};

struct Texts { left: i32, center: i32, right: i32 }

fn texts() -> &'static mut Option<Texts> {
    static mut TEXTS: Option<Texts> = None;
    unsafe { &mut *std::ptr::addr_of_mut!(TEXTS) }
}

pub fn enable(parent: i32) -> Result<(), i32> {
    let bar = lua::frame("Frame", parent)?;
    lua::set_size(bar, 430.0, 20.0)?;
    lua::set_point(bar, "BOTTOMLEFT", parent, "BOTTOMLEFT", 8.0, 4.0)?;
    lua::backdrop(bar, 0.05, 0.05, 0.055, 0.9)?;
    let left = lua::font_string(bar)?;
    lua::set_point(left, "LEFT", bar, "LEFT", 8.0, 0.0)?;
    let center = lua::font_string(bar)?;
    lua::set_point(center, "CENTER", bar, "CENTER", 0.0, 0.0)?;
    let right = lua::font_string(bar)?;
    lua::set_point(right, "RIGHT", bar, "RIGHT", -8.0, 0.0)?;
    lua::method(right, "SetJustifyH", &[Val::Str("RIGHT".into())])?;
    *texts() = Some(Texts { left, center, right });
    Ok(())
}

pub fn update() -> i32 {
    let Some(text) = texts() else { return 0 };
    let fps = lua::num(&lua::call("GetFramerate", &[]).unwrap_or_default()) as i32;
    let money = lua::num(&lua::call("GetMoney", &[]).unwrap_or_default()) as i64;
    let gold = money / 10000;
    let zone = lua::call("GetMinimapZoneText", &[]).map(|vals| lua::text(&vals)).unwrap_or_default();
    if lua::set_text(text.left, &format!("{fps} fps")).is_err() { return 0; }
    let _ = lua::set_text(text.center, if zone.is_empty() { "ElvUI" } else { &zone });
    let _ = lua::set_text(text.right, &format!("{gold}g"));
    0
}
