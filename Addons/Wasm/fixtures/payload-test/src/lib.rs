use wow_addon_sdk::*;
use std::sync::atomic::{AtomicI32, Ordering};
static ARGC: AtomicI32 = AtomicI32::new(-1);
static TAG: AtomicI32 = AtomicI32::new(-1);
static NUM: AtomicI32 = AtomicI32::new(0);

fn tag_of(arg: &EventArg<'_>) -> i32 {
    match arg {
        EventArg::Nil => 0,
        EventArg::Bool(_) => 1,
        EventArg::Number(_) => 2,
        EventArg::Text(_) => 3,
        EventArg::Truncated(_, _) => 4,
        EventArg::Redacted => 5,
    }
}

fn on_load() -> i32 {
    let a = subscribe("BAG_UPDATE");
    let b = subscribe("COMBAT_LOG_EVENT");
    if a != 0 { a } else { b }
}

fn on_event(_name: &str, args: &[EventArg<'_>]) -> i32 {
    ARGC.store(args.len() as i32, Ordering::Relaxed);
    if let Some(first) = args.first() {
        TAG.store(tag_of(first), Ordering::Relaxed);
        if let EventArg::Number(n) = first { NUM.store(*n as i32, Ordering::Relaxed); }
    }
    0
}

wow_addon_v2!(on_load, on_event);

#[no_mangle]
pub extern "C" fn wow_last_argc() -> i32 { ARGC.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_last_tag() -> i32 { TAG.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_last_num() -> i32 { NUM.load(Ordering::Relaxed) }
