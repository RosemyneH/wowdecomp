#[link(wasm_import_module = "env")]
extern "C" { fn totally_unrestricted(ptr: *const u8) -> i32; }
#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 { unsafe { totally_unrestricted(b"x".as_ptr()) } }
