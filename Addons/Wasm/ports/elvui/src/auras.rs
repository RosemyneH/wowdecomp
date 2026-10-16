use wow_addon_sdk::lua::{self, Val};

pub fn enable(parent: i32) -> Result<(), i32> {
    if let Ok(Val::Obj(buffs)) = lua::global("BuffFrame") {
        lua::set_point(buffs, "TOPRIGHT", parent, "TOPRIGHT", -180.0, -8.0)?;
    }
    if let Ok(Val::Obj(debuffs)) = lua::global("DebuffFrame") {
        let _ = lua::set_point(debuffs, "TOPRIGHT", parent, "TOPRIGHT", -180.0, -80.0);
    }
    Ok(())
}
