use wow_addon_sdk::*;
use std::sync::atomic::{AtomicI32, Ordering};
static HANDLE: AtomicI32 = AtomicI32::new(0);
#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 {
    let h = ui_create("text");
    HANDLE.store(h, Ordering::Relaxed);
    if h > 0 { ui_set_text(h, "fixture") } else { h }
}
#[no_mangle]
pub extern "C" fn wow_handle() -> i32 { HANDLE.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_try_foreign(handle: i32) -> i32 { ui_set_text(handle, "stolen") }
