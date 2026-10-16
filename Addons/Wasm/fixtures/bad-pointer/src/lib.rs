#[link(wasm_import_module = "wow")]
extern "C" { fn chat_print(ptr: *const u8, len: u32) -> i32; }
static mut LAST: i32 = 0;
#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 {
    unsafe { LAST = chat_print(0x7fff_0000 as *const u8, 4096); }
    0
}
#[no_mangle]
pub extern "C" fn wow_last_result() -> i32 { unsafe { LAST } }
