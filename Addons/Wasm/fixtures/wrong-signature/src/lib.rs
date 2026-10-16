// ʕ •ᴥ•ʔ✿ Imports wow.chat_print with the wrong signature; instantiation must fail ✿ ʕ •ᴥ•ʔ
#[link(wasm_import_module = "wow")]
extern "C" { fn chat_print(ptr: *const u8) -> i32; }
#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 { unsafe { chat_print(b"x".as_ptr()) } }
