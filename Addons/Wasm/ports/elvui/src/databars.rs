use wow_addon_sdk::lua::{self, Val};

fn bar() -> &'static mut i32 {
    static mut BAR: i32 = 0;
    unsafe { &mut *std::ptr::addr_of_mut!(BAR) }
}

pub fn enable(parent: i32) -> Result<(), i32> {
    let frame = lua::frame("StatusBar", parent)?;
    lua::set_size(frame, 540.0, 10.0)?;
    lua::set_point(frame, "BOTTOM", parent, "BOTTOM", 0.0, 43.0)?;
    lua::method(frame, "SetStatusBarTexture", &[Val::Str(r"Interface\TargetingFrame\UI-StatusBar".into())])?;
    lua::method(frame, "SetStatusBarColor", &[Val::Num(0.2), Val::Num(0.45), Val::Num(0.8), Val::Num(1.0)])?;
    lua::backdrop(frame, 0.05, 0.05, 0.05, 0.8)?;
    *bar() = frame;
    if let Ok(Val::Obj(blizz)) = lua::global("MainMenuExpBar") { let _ = lua::method(blizz, "Hide", &[]); }
    Ok(())
}

pub fn update() -> i32 {
    let frame = *bar();
    if frame == 0 { return 0; }
    let xp = lua::num(&lua::call("UnitXP", &[Val::Str("player".into())]).unwrap_or_default());
    let max = lua::num(&lua::call("UnitXPMax", &[Val::Str("player".into())]).unwrap_or_default()).max(1.0);
    let _ = lua::method(frame, "SetMinMaxValues", &[Val::Num(0.0), Val::Num(max)]);
    let _ = lua::method(frame, "SetValue", &[Val::Num(xp)]);
    0
}
