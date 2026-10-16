use wow_addon_sdk::lua::{self, Val};

pub fn enable(parent: i32) -> Result<(), i32> {
    let Ok(Val::Obj(frame)) = lua::global("ChatFrame1") else { return Ok(()) };
    lua::set_size(frame, 430.0, 140.0)?;
    lua::set_point(frame, "BOTTOMLEFT", parent, "BOTTOMLEFT", 8.0, 32.0)?;
    let _ = lua::backdrop(frame, 0.04, 0.04, 0.05, 0.55);
    if let Ok(Val::Obj(edit)) = lua::global("ChatFrame1EditBox") {
        lua::set_point(edit, "TOPLEFT", frame, "BOTTOMLEFT", 0.0, -4.0)?;
        lua::set_point(edit, "TOPRIGHT", frame, "BOTTOMRIGHT", 0.0, -4.0)?;
    }
    Ok(())
}
