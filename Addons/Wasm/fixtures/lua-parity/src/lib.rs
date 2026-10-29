use std::sync::atomic::{AtomicI32, Ordering};
use wow_addon_sdk::{wow_addon_v2, EventArg};

// ʕ •ᴥ•ʔ✿ Local imports so this crate does not wait on the SDK wrappers ✿ ʕ •ᴥ•ʔ
#[link(wasm_import_module = "wow")]
extern "C" {
    fn ui_spawn(parent: i32, ptr: *const u8, len: u32) -> i32;
    fn ui_style(handle: i32, op: i32, ptr: *const u8, len: u32) -> i32;
    fn ui_bind(handle: i32, ptr: *const u8, len: u32) -> i32;
    fn ui_get_text(handle: i32, out: *mut u8, cap: u32) -> i32;
    fn unit_names(tptr: *const u8, tlen: u32, out: *mut u8, cap: u32) -> i32;
    fn event_subscribe(ptr: *const u8, len: u32) -> i32;
}

static FRAME: AtomicI32 = AtomicI32::new(0);
static EDIT: AtomicI32 = AtomicI32::new(0);
static ALPHA: AtomicI32 = AtomicI32::new(0);
static MOUSE: AtomicI32 = AtomicI32::new(0);
static TEX: AtomicI32 = AtomicI32::new(0);
static BIND: AtomicI32 = AtomicI32::new(0);
static GETTEXT: AtomicI32 = AtomicI32::new(0);
static NAMES: AtomicI32 = AtomicI32::new(0);
static UI_EVENT: AtomicI32 = AtomicI32::new(0);

fn spawn(parent: i32, kind: &[u8]) -> i32 {
    unsafe { ui_spawn(parent, kind.as_ptr(), kind.len() as u32) }
}
fn style(handle: i32, op: i32, blob: &[u8]) -> i32 {
    unsafe { ui_style(handle, op, blob.as_ptr(), blob.len() as u32) }
}

fn on_load() -> i32 {
    let frame = spawn(0, b"frame");
    FRAME.store(frame, Ordering::Relaxed);
    if frame <= 0 { return frame; }
    let edit = spawn(frame, b"editbox");
    EDIT.store(edit, Ordering::Relaxed);
    if edit <= 0 { return edit; }
    let tex = spawn(frame, b"texture");
    if tex <= 0 { return tex; }

    // ʕ •ᴥ•ʔ✿ Later host errors stay in the atomics; a live frame and edit box still load ✿ ʕ •ᴥ•ʔ
    ALPHA.store(style(frame, 12, &500i32.to_le_bytes()), Ordering::Relaxed);
    MOUSE.store(style(frame, 13, &[1]), Ordering::Relaxed);
    TEX.store(style(tex, 14, b"Interface\\Buttons\\WHITE8X8"), Ordering::Relaxed);
    let enter = b"enter_pressed";
    BIND.store(unsafe { ui_bind(edit, enter.as_ptr(), enter.len() as u32) }, Ordering::Relaxed);

    let mut text = [0u8; 32];
    GETTEXT.store(unsafe { ui_get_text(edit, text.as_mut_ptr(), text.len() as u32) }, Ordering::Relaxed);
    let ev = b"WASM_UI";
    unsafe { event_subscribe(ev.as_ptr(), ev.len() as u32) };

    let tokens = b"player\0target\0";
    let mut names = [0u8; 256];
    NAMES.store(unsafe { unit_names(tokens.as_ptr(), tokens.len() as u32, names.as_mut_ptr(), names.len() as u32) }, Ordering::Relaxed);
    0
}

fn on_event(name: &str, _: &[EventArg<'_>]) -> i32 {
    if name == "WASM_UI" { UI_EVENT.store(1, Ordering::Relaxed); }
    0
}

wow_addon_v2!(on_load, on_event);

#[no_mangle]
pub extern "C" fn wow_frame() -> i32 { FRAME.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_edit() -> i32 { EDIT.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_alpha() -> i32 { ALPHA.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_mouse() -> i32 { MOUSE.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_tex() -> i32 { TEX.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_bind() -> i32 { BIND.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_gettext() -> i32 { GETTEXT.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_names() -> i32 { NAMES.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_ui_event() -> i32 { UI_EVENT.load(Ordering::Relaxed) }
