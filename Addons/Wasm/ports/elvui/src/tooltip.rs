use wow_addon_sdk::lua::{self, Val};

pub fn enable() -> Result<(), i32> {
    let Ok(Val::Obj(tip)) = lua::global("GameTooltip") else { return Ok(()) };
    let _ = lua::backdrop(tip, 0.05, 0.05, 0.06, 0.92);
    let _ = lua::method(tip, "SetScale", &[Val::Num(1.0)]);
    Ok(())
}
