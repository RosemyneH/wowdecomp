use wow_addon_sdk::*;
use std::sync::atomic::{AtomicI32, Ordering::Relaxed};
static STATUS: AtomicI32 = AtomicI32::new(-1);
static REQID: AtomicI32 = AtomicI32::new(0);

fn on_load() -> i32 { subscribe("WASM_CONFIRM_RESULT") }

fn on_event(name: &str, args: &[EventArg<'_>]) -> i32 {
    if name == "WASM_CONFIRM_RESULT" {
        if let (Some(EventArg::Number(id)), Some(EventArg::Number(status))) = (args.first(), args.get(1)) {
            REQID.store(*id as i32, Relaxed);
            STATUS.store(*status as i32, Relaxed);
        }
    }
    0
}

wow_addon_v2!(on_load, on_event);

#[no_mangle]
pub extern "C" fn wow_chat_request() -> i32 { confirm::chat_send(1, "", "hello from confirm") }
#[no_mangle]
pub extern "C" fn wow_bid_request() -> i32 { confirm::auction_bid(0, 1, 100) }
#[no_mangle]
pub extern "C" fn wow_last_status() -> i32 { STATUS.load(Relaxed) }
#[no_mangle]
pub extern "C" fn wow_last_reqid() -> i32 { REQID.load(Relaxed) }
