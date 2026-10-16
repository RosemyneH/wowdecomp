use wow_addon_sdk::*;
#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 { 0 }
#[no_mangle]
pub extern "C" fn wow_bench_ping(n: i32) -> i32 {
    let mut acc = 0;
    for _ in 0..n { acc += host_ping(); }
    acc
}
#[no_mangle]
pub extern "C" fn wow_bench_empty(n: i32) -> i32 {
    let mut acc = 0i32;
    for i in 0..n { acc = acc.wrapping_add(i & 1); }
    acc
}
