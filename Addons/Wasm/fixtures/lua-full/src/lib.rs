use std::sync::atomic::{AtomicI32, Ordering};
use wow_addon_sdk::lua::{self, Val};
use wow_addon_sdk::*;

static EXEC: AtomicI32 = AtomicI32::new(-99);
static MISSING: AtomicI32 = AtomicI32::new(-99);
static CALL: AtomicI32 = AtomicI32::new(-99);

fn on_load() -> i32 {
    EXEC.store(lua_exec("SlashCmdList = SlashCmdList"), Ordering::Relaxed);
    MISSING.store(lua_load_addon("Missing"), Ordering::Relaxed);
    CALL.store(match lua::call("CreateFrame", &[Val::Str("Frame".into()), Val::Nil, Val::Nil]) {
        Ok(vals) => match vals.first() { Some(Val::Obj(handle)) => *handle, _ => -1 },
        Err(code) => code,
    }, Ordering::Relaxed);
    let _ = print("lua.full bridge ready");
    0
}

fn on_event(_: &str, _: &[EventArg<'_>]) -> i32 { 0 }

wow_addon_v2!(on_load, on_event);

#[no_mangle]
pub extern "C" fn wow_exec() -> i32 { EXEC.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_missing() -> i32 { MISSING.load(Ordering::Relaxed) }
#[no_mangle]
pub extern "C" fn wow_call() -> i32 { CALL.load(Ordering::Relaxed) }
