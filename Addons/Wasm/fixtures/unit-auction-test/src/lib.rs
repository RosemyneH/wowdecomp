use wow_addon_sdk::*;
use std::sync::atomic::{AtomicI32, Ordering};
static LAST: AtomicI32 = AtomicI32::new(0);
static UCOUNT: AtomicI32 = AtomicI32::new(-1);
static HEALTH: AtomicI32 = AtomicI32::new(0);
static ACOUNT: AtomicI32 = AtomicI32::new(-1);
static ITEM: AtomicI32 = AtomicI32::new(0);

fn do_units() -> i32 {
    match units::snapshot(&["player", "target"]) {
        Ok(list) => {
            UCOUNT.store(list.len() as i32, Ordering::Relaxed);
            if let Some(first) = list.first() { HEALTH.store(first.health as i32, Ordering::Relaxed); }
            0
        }
        Err(code) => code,
    }
}
fn do_auction() -> i32 {
    match auction::snapshot(auction::List::Browse) {
        Ok((_total, list)) => {
            ACOUNT.store(list.len() as i32, Ordering::Relaxed);
            if let Some(first) = list.first() { ITEM.store(first.item_id as i32, Ordering::Relaxed); }
            0
        }
        Err(code) => code,
    }
}

#[no_mangle]
pub extern "C" fn wow_on_load() -> i32 { LAST.store(do_units(), Ordering::Relaxed); 0 }
#[no_mangle]
pub extern "C" fn wow_last_result() -> i32 { LAST.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_do_units() -> i32 { do_units() }
#[no_mangle]
pub extern "C" fn wow_do_auction() -> i32 { do_auction() }
#[no_mangle]
pub extern "C" fn wow_unit_count() -> i32 { UCOUNT.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_first_health() -> i32 { HEALTH.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_auction_count() -> i32 { ACOUNT.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_first_item() -> i32 { ITEM.load(Ordering::Relaxed) }
