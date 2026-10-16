#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 {
    let mut v: Vec<Vec<u8>> = Vec::new();
    loop { v.push(vec![1u8; 1024 * 1024]); }
}
