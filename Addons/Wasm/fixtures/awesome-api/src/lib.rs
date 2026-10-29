use wow_addon_sdk::awesome;
use wow_addon_sdk::*;

fn on_load() -> i32 { 0 }
fn on_event(_: &str, _: &[EventArg<'_>]) -> i32 { 0 }

wow_addon_v2!(on_load, on_event);

#[no_mangle]
pub extern "C" fn wow_probe() -> i32 {
    match awesome::is_window_focused() {
        Ok(true) => 1,
        Ok(false) => 0,
        Err(code) => code,
    }
}

#[no_mangle]
pub extern "C" fn wow_missing() -> i32 {
    match awesome::call("Nope", &[]) {
        Ok(_) => 0,
        Err(code) => code,
    }
}
