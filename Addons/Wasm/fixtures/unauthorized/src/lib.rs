#[link(wasm_import_module = "wow")]
extern "C" { fn chat_print(ptr: *const u8, len: u32) -> i32; }
static mut LAST: i32 = 0;
#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 {
    let m = b"should be denied";
    unsafe { LAST = chat_print(m.as_ptr(), m.len() as u32); }
    0
}
#[no_mangle]
pub extern "C" fn wow_last_result() -> i32 { unsafe { LAST } }
