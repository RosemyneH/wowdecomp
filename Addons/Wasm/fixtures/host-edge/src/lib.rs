#[link(wasm_import_module = "wow")]
extern "C" {
    fn perf_query(kind: i32) -> i64;
    fn storage_set(scope: i32, kp: *const u8, kl: u32, vp: *const u8, vl: u32) -> i32;
    fn storage_len(scope: i32, kp: *const u8, kl: u32) -> i32;
    fn event_subscribe(ptr: *const u8, len: u32) -> i32;
}

#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 { 0 }

#[no_mangle]
pub extern "C" fn wow_perf(kind: i32) -> i32 { unsafe { perf_query(kind) as i32 } }

#[no_mangle]
pub extern "C" fn wow_storage_scope(scope: i32) -> i32 {
    unsafe { storage_set(scope, b"k".as_ptr(), 1, b"v".as_ptr(), 1) }
}

#[no_mangle]
pub extern "C" fn wow_storage_keylen(len: i32) -> i32 {
    let key = vec![b'a'; len as usize];
    unsafe { storage_set(0, key.as_ptr(), len as u32, b"v".as_ptr(), 1) }
}

#[no_mangle]
pub extern "C" fn wow_storage_empty() -> i32 {
    unsafe { storage_len(0, b"".as_ptr(), 0) }
}

#[no_mangle]
pub extern "C" fn wow_storage_ctrl() -> i32 {
    let key = [b'a', 1u8];
    unsafe { storage_set(0, key.as_ptr(), 2, b"v".as_ptr(), 1) }
}

#[no_mangle]
pub extern "C" fn wow_sub(case: i32) -> i32 {
    let name: &[u8] = match case {
        0 | 1 => b"BAG_UPDATE",
        2 => b"PLAYER_LOGIN",
        3 => b"UNIT_HEALTH",
        4 => b"FAKE_EVENT",
        _ => b"",
    };
    unsafe { event_subscribe(name.as_ptr(), name.len() as u32) }
}
