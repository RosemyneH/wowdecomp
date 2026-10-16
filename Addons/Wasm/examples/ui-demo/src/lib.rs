use wow_addon_sdk::*;
use std::sync::atomic::{AtomicI32, Ordering};
static HANDLE: AtomicI32 = AtomicI32::new(0);
fn on_load() -> i32 {
    let handle = ui_create("text");
    if handle <= 0 { return handle; }
    HANDLE.store(handle, Ordering::Relaxed);
    ui_set_text(handle, "Hello from WASM UI");
    let _ = subscribe("BAG_UPDATE");
    0
}
fn on_event(event: &str) -> i32 {
    if event == "BAG_UPDATE" { ui_set_text(HANDLE.load(Ordering::Relaxed), "Inventory changed") } else { 0 }
}
wow_addon!(on_load, on_event);
