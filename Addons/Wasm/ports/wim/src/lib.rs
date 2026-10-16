use wow_addon_sdk::*;

fn on_load() -> i32 {
    let packaged = lua_load("WIM.toc");
    if packaged == 0 {
        let _ = print("WIM loaded from this package");
        return 0;
    }
    let installed = lua_load_addon("WIM");
    if installed == 0 {
        let _ = print("WIM loaded from Interface/AddOns");
        return 0;
    }
    let _ = print(&format!("WIM did not load (package {packaged}, Interface/AddOns {installed}). Put the real addon in this folder or in Interface/AddOns/WIM"));
    0
}

fn on_event(_: &str, _: &[EventArg]) -> i32 { 0 }

wow_addon_v2!(on_load, on_event);
