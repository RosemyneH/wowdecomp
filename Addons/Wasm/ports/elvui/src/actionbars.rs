use wow_addon_sdk::lua::{self, Val};

const BUTTONS: [&str; 12] = [
    "ActionButton1", "ActionButton2", "ActionButton3", "ActionButton4",
    "ActionButton5", "ActionButton6", "ActionButton7", "ActionButton8",
    "ActionButton9", "ActionButton10", "ActionButton11", "ActionButton12",
];

pub fn enable(parent: i32) -> Result<(), i32> {
    let holder = lua::frame("Frame", parent)?;
    lua::set_size(holder, 12.0 * 32.0 + 11.0 * 2.0, 32.0)?;
    lua::set_point(holder, "BOTTOM", parent, "BOTTOM", 0.0, 190.0)?;
    lua::method(holder, "SetFrameStrata", &[Val::Str("MEDIUM".into())])?;
    for (index, name) in BUTTONS.iter().enumerate() {
        let Ok(Val::Obj(button)) = lua::global(name) else { continue };
        lua::set_size(button, 32.0, 32.0)?;
        lua::set_point(button, "BOTTOMLEFT", holder, "BOTTOMLEFT", index as f64 * 34.0, 0.0)?;
        lua::method(button, "SetFrameStrata", &[Val::Str("MEDIUM".into())])?;
    }
    if let Ok(Val::Obj(bar)) = lua::global("MainMenuBar") {
        let _ = lua::method(bar, "Hide", &[]);
    }
    Ok(())
}
