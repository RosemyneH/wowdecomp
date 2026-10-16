#[link(wasm_import_module = "wow")]
extern "C" {
    fn storage_set(scope: i32, kp: *const u8, kl: u32, vp: *const u8, vl: u32) -> i32;
    fn storage_get(scope: i32, kp: *const u8, kl: u32, out: *mut u8, cap: u32) -> i32;
    fn storage_len(scope: i32, kp: *const u8, kl: u32) -> i32;
}
static mut LAST: i32 = 0;
fn set(scope: i32, k: &[u8], v: &[u8]) -> i32 { unsafe { storage_set(scope, k.as_ptr(), k.len() as u32, v.as_ptr(), v.len() as u32) } }

#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 {
    // ʕ •ᴥ•ʔ✿ Only seed when absent, so persistence across reloads is observable ✿ ʕ •ᴥ•ʔ
    unsafe {
        LAST = if storage_len(0, b"greeting".as_ptr(), 8) < 0 { set(0, b"greeting", b"denied?") } else { storage_len(0, b"greeting".as_ptr(), 8) };
    }
    0
}
#[no_mangle]
pub extern "C" fn wow_last_result() -> i32 { unsafe { LAST } }
#[no_mangle]
pub extern "C" fn wow_do_set() -> i32 {
    let a = set(0, b"greeting", b"hello-wasm");
    let b = set(1, b"char-key", b"char-value");
    if a != 0 { return a; }
    b
}
#[no_mangle]
pub extern "C" fn wow_read_back() -> i32 {
    let mut buf = [0u8; 64];
    let n = unsafe { storage_get(0, b"greeting".as_ptr(), 8, buf.as_mut_ptr(), 64) };
    if n < 0 { return n; }
    ((n as i32) << 8) | buf[0] as i32
}
#[no_mangle]
pub extern "C" fn wow_big_set() -> i32 {
    let v = vec![7u8; 2048];
    set(0, b"big", &v)
}
#[no_mangle]
pub extern "C" fn wow_len() -> i32 { unsafe { storage_len(0, b"greeting".as_ptr(), 8) } }
