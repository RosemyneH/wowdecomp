use wow_addon_sdk::*;

fn on_load() -> i32 {
    let packaged = lua_load("ElvUI.toc");
    if packaged == 0 {
        let _ = print("ElvUI loaded from this package");
        return 0;
    }
    let installed = lua_load_addon("ElvUI");
    if installed == 0 {
        let _ = print("ElvUI loaded from Interface/AddOns");
        return 0;
    }
    let _ = print(&format!("ElvUI did not load (package {packaged}, Interface/AddOns {installed}). Put the real addon in this folder or in Interface/AddOns/ElvUI"));
    0
}

fn on_event(_: &str, _: &[EventArg]) -> i32 { 0 }

wow_addon_v2!(on_load, on_event);
