use wow_addon_sdk::lua::{self, Val};

pub fn parent() -> Result<i32, i32> {
    lua::ui_parent()
}

pub fn hide(name: &str) {
    let Ok(Val::Obj(frame)) = lua::global(name) else { return };
    let _ = lua::method(frame, "UnregisterAllEvents", &[]);
    let _ = lua::method(frame, "Hide", &[]);
}

pub fn exists(token: &str) -> bool {
    match lua::call("UnitExists", &[Val::Str(token.into())]) {
        Ok(vals) => match vals.first() {
            Some(Val::Bool(value)) => *value,
            Some(Val::Num(value)) => *value != 0.0,
            _ => false,
        },
        Err(_) => false,
    }
}

pub fn number(func: &str, token: &str) -> f64 {
    lua::call(func, &[Val::Str(token.into())]).map(|vals| lua::num(&vals)).unwrap_or(0.0)
}

pub fn name(token: &str) -> String {
    lua::call("UnitName", &[Val::Str(token.into())]).map(|vals| lua::text(&vals)).unwrap_or_default()
}

pub fn dead(token: &str) -> bool {
    flag("UnitIsDead", token) || flag("UnitIsGhost", token)
}

fn flag(func: &str, token: &str) -> bool {
    match lua::call(func, &[Val::Str(token.into())]) {
        Ok(vals) => match vals.first() {
            Some(Val::Bool(value)) => *value,
            Some(Val::Num(value)) => *value != 0.0,
            _ => false,
        },
        Err(_) => false,
    }
}

pub fn class_color(token: &str) -> (f64, f64, f64) {
    let vals = lua::call("UnitClass", &[Val::Str(token.into())]).unwrap_or_default();
    let id = vals.get(2).and_then(|val| match val { Val::Num(n) => Some(*n as i32), _ => None }).unwrap_or(0);
    match id {
        1 => (0.78, 0.61, 0.43),
        2 => (0.96, 0.55, 0.73),
        3 => (0.67, 0.83, 0.45),
        4 => (1.00, 0.96, 0.41),
        5 => (1.00, 1.00, 1.00),
        6 => (0.77, 0.12, 0.23),
        7 => (0.00, 0.44, 0.87),
        8 => (0.41, 0.80, 0.94),
        9 => (0.58, 0.51, 0.79),
        11 => (1.00, 0.49, 0.04),
        _ => (0.55, 0.55, 0.55),
    }
}

pub fn status_bar(parent: i32, width: f64, height: f64) -> Result<i32, i32> {
    let bar = lua::frame("StatusBar", parent)?;
    lua::set_size(bar, width, height)?;
    lua::method(bar, "SetStatusBarTexture", &[Val::Str(r"Interface\TargetingFrame\UI-StatusBar".into())])?;
    lua::method(bar, "SetMinMaxValues", &[Val::Num(0.0), Val::Num(1.0)])?;
    Ok(bar)
}
