use std::sync::atomic::{AtomicI32, Ordering};
use wow_addon_sdk::ui::*;
use wow_addon_sdk::*;

static FRAME: AtomicI32 = AtomicI32::new(0);
static LABEL: AtomicI32 = AtomicI32::new(0);
static BAD_KIND: AtomicI32 = AtomicI32::new(0);
static BAD_PARENT: AtomicI32 = AtomicI32::new(0);
static BAD_OP: AtomicI32 = AtomicI32::new(0);

fn on_load() -> i32 {
    let frame = spawn(0, "frame");
    if frame <= 0 { return frame; }
    FRAME.store(frame, Ordering::Relaxed);
    let label = spawn(frame, "fontstring");
    if label <= 0 { return label; }
    LABEL.store(label, Ordering::Relaxed);
    BAD_KIND.store(spawn(0, "nope"), Ordering::Relaxed);
    BAD_PARENT.store(spawn(label, "frame"), Ordering::Relaxed);
    BAD_OP.store(style(frame, 99, &[0, 0, 0, 0]), Ordering::Relaxed);
    let sized = set_size(frame, 180, 36);
    if sized != 0 { return sized; }
    ui_set_text(label, "laid out")
}

fn on_event(_: &str) -> i32 { 0 }

wow_addon!(on_load, on_event);

#[no_mangle]
pub extern "C" fn wow_frame() -> i32 { FRAME.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_label() -> i32 { LABEL.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_bad_kind() -> i32 { BAD_KIND.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_bad_parent() -> i32 { BAD_PARENT.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_bad_op() -> i32 { BAD_OP.load(Ordering::Relaxed) }
