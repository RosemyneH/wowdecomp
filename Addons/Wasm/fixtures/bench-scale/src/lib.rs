use wow_addon_sdk::*;
use std::sync::atomic::{AtomicI32, Ordering::Relaxed};
static MODE: AtomicI32 = AtomicI32::new(1);
static EVENTS: AtomicI32 = AtomicI32::new(0);
static BYTES: AtomicI32 = AtomicI32::new(0);
static SPANS: AtomicI32 = AtomicI32::new(0);

fn accept(bytes: &[u8]) -> i32 {
    EVENTS.fetch_add(1, Relaxed);
    BYTES.fetch_add(bytes.len() as i32, Relaxed);
    match MODE.load(Relaxed) {
        0 => 0,
        2 => with_event(bytes, |_name, args| {
            args.iter().map(|a| match a { EventArg::Text(s) => s.len() as i32, _ => 1 }).sum::<i32>() & 0
        }),
        _ => with_event(bytes, |_name, _args| 0),
    }
}

#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 {
    let a = subscribe("COMBAT_LOG_EVENT_UNFILTERED");
    let b = subscribe("UNIT_HEALTH");
    let c = subscribe("BAG_UPDATE");
    a | b | c
}

#[no_mangle]
pub extern "C" fn wow_on_event_v2(ptr: *const u8, len: u32) -> i32 {
    let bytes = unsafe { core::slice::from_raw_parts(ptr, len as usize) };
    accept(bytes)
}

#[no_mangle]
pub extern "C" fn wow_on_event_span(ptr: *const u8, len: u32) -> i32 {
    let bytes = unsafe { core::slice::from_raw_parts(ptr, len as usize) };
    SPANS.fetch_add(1, Relaxed);
    visit_span(bytes, accept)
}

#[no_mangle]
pub extern "C" fn wow_set_mode(mode: i32) -> i32 { MODE.store(mode, Relaxed); 0 }
#[no_mangle]
pub extern "C" fn wow_events(_: i32) -> i32 { EVENTS.load(Relaxed) }
#[no_mangle]
pub extern "C" fn wow_span_calls(_: i32) -> i32 { SPANS.load(Relaxed) }
#[no_mangle]
pub extern "C" fn wow_bytes(_: i32) -> i32 { BYTES.load(Relaxed) }
