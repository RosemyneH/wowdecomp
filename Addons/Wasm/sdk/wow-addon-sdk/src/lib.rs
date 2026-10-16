//! ʕ •ᴥ•ʔ✿ Host ABI v1: imports live in module `wow`; all strings are ptr/len into linear memory ✿ ʕ •ᴥ•ʔ

pub const ERR_PERMISSION_DENIED: i32 = -2;
pub const ERR_INVALID_MEMORY: i32 = -3;
pub const ERR_INVALID_HANDLE: i32 = -4;
pub const ERR_LIMIT_EXCEEDED: i32 = -6;
pub const ERR_BUFFER_TOO_SMALL: i32 = -7;

mod raw {
    #[link(wasm_import_module = "wow")]
    extern "C" {
        pub fn api_version() -> i32;
        pub fn ping() -> i32;
        pub fn chat_print(ptr: *const u8, len: u32) -> i32;
        pub fn event_subscribe(ptr: *const u8, len: u32) -> i32;
        pub fn inventory_snapshot(ptr: *mut u8, cap: u32) -> i32;
        pub fn ui_create(ptr: *const u8, len: u32) -> i32;
        pub fn ui_set_text(handle: i32, ptr: *const u8, len: u32) -> i32;
        pub fn perf_query(kind: i32) -> i64;
        pub fn storage_get(scope: i32, kptr: *const u8, klen: u32, out: *mut u8, cap: u32) -> i32;
        pub fn storage_len(scope: i32, kptr: *const u8, klen: u32) -> i32;
        pub fn storage_keys(scope: i32, out: *mut u8, cap: u32) -> i32;
        pub fn storage_usage(scope: i32) -> i64;
        pub fn storage_set(scope: i32, kptr: *const u8, klen: u32, vptr: *const u8, vlen: u32) -> i32;
        pub fn storage_delete(scope: i32, kptr: *const u8, klen: u32) -> i32;
        pub fn chat_send_request(kind: i32, tptr: *const u8, tlen: u32, mptr: *const u8, mlen: u32) -> i32;
        pub fn auction_bid_request(list: i32, index: i32, bid: i32) -> i32;
        pub fn unit_snapshot(tptr: *const u8, tlen: u32, out: *mut u8, cap: u32) -> i32;
        pub fn auction_snapshot(kind: i32, out: *mut u8, cap: u32) -> i32;
        pub fn ui_spawn(parent: i32, ptr: *const u8, len: u32) -> i32;
        pub fn ui_style(handle: i32, op: i32, ptr: *const u8, len: u32) -> i32;
        pub fn ui_bind(handle: i32, ptr: *const u8, len: u32) -> i32;
        pub fn ui_get_text(handle: i32, out: *mut u8, cap: u32) -> i32;
        pub fn unit_names(tptr: *const u8, tlen: u32, out: *mut u8, cap: u32) -> i32;
        pub fn lua_exec(ptr: *const u8, len: u32) -> i32;
        pub fn lua_load(ptr: *const u8, len: u32) -> i32;
        pub fn lua_load_addon(ptr: *const u8, len: u32) -> i32;
    }
}

pub fn host_api_version() -> i32 { unsafe { raw::api_version() } }
pub fn host_ping() -> i32 { unsafe { raw::ping() } }
pub fn print(text: &str) -> i32 { unsafe { raw::chat_print(text.as_ptr(), text.len() as u32) } }
pub fn subscribe(event: &str) -> i32 { unsafe { raw::event_subscribe(event.as_ptr(), event.len() as u32) } }
pub fn ui_create(kind: &str) -> i32 { unsafe { raw::ui_create(kind.as_ptr(), kind.len() as u32) } }
pub fn ui_set_text(handle: i32, text: &str) -> i32 { unsafe { raw::ui_set_text(handle, text.as_ptr(), text.len() as u32) } }

/// ʕ •ᴥ•ʔ✿ Frames, bars and text. Parent 0 is UIParent. Pixels are the 3.3.5 UI coordinate space ✿ ʕ •ᴥ•ʔ
pub mod ui {
    pub const SET_SIZE: i32 = 1;
    pub const SET_POINT: i32 = 2;
    pub const SET_COLOR: i32 = 3;
    pub const SET_SHOWN: i32 = 4;
    pub const SET_VALUE: i32 = 5;
    pub const SET_JUSTIFY: i32 = 6;
    pub const SET_FONT_SIZE: i32 = 7;
    pub const SET_BACKDROP: i32 = 8;
    pub const SET_TEXT_COLOR: i32 = 9;
    pub const SET_LEVEL: i32 = 10;
    pub const SET_STRATA: i32 = 11;

    pub const TOPLEFT: u8 = 0;
    pub const TOP: u8 = 1;
    pub const TOPRIGHT: u8 = 2;
    pub const LEFT: u8 = 3;
    pub const CENTER: u8 = 4;
    pub const RIGHT: u8 = 5;
    pub const BOTTOMLEFT: u8 = 6;
    pub const BOTTOM: u8 = 7;
    pub const BOTTOMRIGHT: u8 = 8;

    pub const JUSTIFY_LEFT: u8 = 0;
    pub const JUSTIFY_CENTER: u8 = 1;
    pub const JUSTIFY_RIGHT: u8 = 2;
    pub const JUSTIFY_TOP: u8 = 0;
    pub const JUSTIFY_MIDDLE: u8 = 1;
    pub const JUSTIFY_BOTTOM: u8 = 2;

    pub const STRATA_BACKGROUND: u8 = 0;
    pub const STRATA_LOW: u8 = 1;
    pub const STRATA_MEDIUM: u8 = 2;
    pub const STRATA_HIGH: u8 = 3;
    pub const STRATA_DIALOG: u8 = 4;

    pub fn spawn(parent: i32, kind: &str) -> i32 {
        unsafe { crate::raw::ui_spawn(parent, kind.as_ptr(), kind.len() as u32) }
    }
    pub fn style(handle: i32, op: i32, blob: &[u8]) -> i32 {
        unsafe { crate::raw::ui_style(handle, op, blob.as_ptr(), blob.len() as u32) }
    }
    pub fn set_size(handle: i32, width: i32, height: i32) -> i32 {
        let mut blob = [0u8; 8];
        blob[..4].copy_from_slice(&width.to_le_bytes());
        blob[4..].copy_from_slice(&height.to_le_bytes());
        style(handle, SET_SIZE, &blob)
    }
    pub fn set_point(handle: i32, point: u8, relative: i32, rel_point: u8, x: i32, y: i32) -> i32 {
        let mut blob = [0u8; 16];
        blob[0] = point;
        blob[1] = rel_point;
        blob[4..8].copy_from_slice(&relative.to_le_bytes());
        blob[8..12].copy_from_slice(&x.to_le_bytes());
        blob[12..].copy_from_slice(&y.to_le_bytes());
        style(handle, SET_POINT, &blob)
    }
    pub fn set_color(handle: i32, r: u8, g: u8, b: u8, a: u8) -> i32 {
        style(handle, SET_COLOR, &[r, g, b, a])
    }
    pub fn set_text_color(handle: i32, r: u8, g: u8, b: u8, a: u8) -> i32 {
        style(handle, SET_TEXT_COLOR, &[r, g, b, a])
    }
    pub fn set_backdrop(handle: i32, r: u8, g: u8, b: u8, a: u8) -> i32 {
        style(handle, SET_BACKDROP, &[r, g, b, a])
    }
    pub fn set_shown(handle: i32, shown: bool) -> i32 {
        style(handle, SET_SHOWN, &[u8::from(shown)])
    }
    pub fn set_value(handle: i32, value: i32, min: i32, max: i32) -> i32 {
        let mut blob = [0u8; 12];
        blob[..4].copy_from_slice(&value.to_le_bytes());
        blob[4..8].copy_from_slice(&min.to_le_bytes());
        blob[8..].copy_from_slice(&max.to_le_bytes());
        style(handle, SET_VALUE, &blob)
    }
    pub fn set_justify(handle: i32, horizontal: u8, vertical: u8) -> i32 {
        style(handle, SET_JUSTIFY, &[horizontal, vertical])
    }
    pub fn set_font_size(handle: i32, pixels: i32) -> i32 {
        style(handle, SET_FONT_SIZE, &pixels.to_le_bytes())
    }
    pub fn set_level(handle: i32, level: i32) -> i32 {
        style(handle, SET_LEVEL, &level.to_le_bytes())
    }
    pub fn set_strata(handle: i32, strata: u8) -> i32 {
        style(handle, SET_STRATA, &[strata])
    }
}
pub fn perf(kind: i32) -> i64 { unsafe { raw::perf_query(kind) } }

/// ʕ •ᴥ•ʔ✿ The source runs in the client Lua state, with that state's full 3.3.5 API ✿ ʕ •ᴥ•ʔ
pub fn lua_exec(source: &str) -> i32 { unsafe { raw::lua_exec(source.as_ptr(), source.len() as u32) } }
pub fn lua_load(relative_path: &str) -> i32 { unsafe { raw::lua_load(relative_path.as_ptr(), relative_path.len() as u32) } }
pub fn lua_load_addon(name: &str) -> i32 { unsafe { raw::lua_load_addon(name.as_ptr(), name.len() as u32) } }

/// ʕ •ᴥ•ʔ✿ Write actions are requests: the user confirms in an engine-owned dialog, the result arrives as a WASM_CONFIRM_RESULT event ✿ ʕ •ᴥ•ʔ
pub mod confirm {
    use crate::raw;
    pub fn chat_send(kind: i32, target: &str, message: &str) -> i32 {
        unsafe { raw::chat_send_request(kind, target.as_ptr(), target.len() as u32, message.as_ptr(), message.len() as u32) }
    }
    pub fn auction_bid(list: i32, index: i32, bid: i32) -> i32 {
        unsafe { raw::auction_bid_request(list, index, bid) }
    }
}

pub mod units {
    use crate::raw;
    #[derive(Debug, Clone, Copy, Default)]
    pub struct Unit {
        pub token_index: u32, pub flags: u32, pub level: u32, pub class_id: u32, pub race_id: u32,
        pub health: u32, pub health_max: u32, pub power_type: u32, pub power: u32, pub power_max: u32,
    }
    impl Unit {
        pub fn exists(&self) -> bool { self.flags & 1 != 0 }
        pub fn dead(&self) -> bool { self.flags & 2 != 0 }
        pub fn is_player(&self) -> bool { self.flags & 4 != 0 }
    }
    pub fn snapshot(tokens: &[&str]) -> Result<Vec<Unit>, i32> {
        let mut blob = Vec::new();
        for token in tokens { blob.extend_from_slice(token.as_bytes()); blob.push(0); }
        let mut buf = vec![0u8; 64 * 1024];
        let written = unsafe { raw::unit_snapshot(blob.as_ptr(), blob.len() as u32, buf.as_mut_ptr(), buf.len() as u32) };
        if written < 0 { return Err(written); }
        let bytes = &buf[..written as usize];
        if bytes.len() < 4 { return Err(-1); }
        let count = u32::from_le_bytes(bytes[0..4].try_into().unwrap()) as usize;
        let mut units = Vec::with_capacity(count);
        for i in 0..count {
            let o = 4 + i * 40;
            if o + 40 > bytes.len() { return Err(-1); }
            let f = |j: usize| u32::from_le_bytes(bytes[o + j * 4..o + j * 4 + 4].try_into().unwrap());
            units.push(Unit { token_index: f(0), flags: f(1), level: f(2), class_id: f(3), race_id: f(4),
                              health: f(5), health_max: f(6), power_type: f(7), power: f(8), power_max: f(9) });
        }
        Ok(units)
    }
    // ป •ᴥ•ʔ✿ packed: count u32, then tokenIndex u32, byteLen u16, UTF-8, no padding ✿ ป •ᴥ•ʔ
    pub fn names(tokens: &[&str]) -> Result<Vec<(u32, String)>, i32> {
        let mut blob = Vec::new();
        for token in tokens { blob.extend_from_slice(token.as_bytes()); blob.push(0); }
        let mut buf = vec![0u8; 64 * 1024];
        let written = unsafe { raw::unit_names(blob.as_ptr(), blob.len() as u32, buf.as_mut_ptr(), buf.len() as u32) };
        if written < 0 { return Err(written); }
        let bytes = &buf[..written as usize];
        if bytes.len() < 4 { return Err(-1); }
        let count = u32::from_le_bytes(bytes[0..4].try_into().unwrap()) as usize;
        if (bytes.len() - 4) / 6 < count { return Err(-1); }
        let mut names = Vec::with_capacity(count);
        let mut pos = 4;
        for _ in 0..count {
            if pos + 6 > bytes.len() { return Err(-1); }
            let token_index = u32::from_le_bytes(bytes[pos..pos + 4].try_into().unwrap());
            let len = u16::from_le_bytes(bytes[pos + 4..pos + 6].try_into().unwrap()) as usize;
            pos += 6;
            if pos + len > bytes.len() { return Err(-1); }
            names.push((token_index, String::from_utf8_lossy(&bytes[pos..pos + len]).into_owned()));
            pos += len;
        }
        Ok(names)
    }
}

pub mod auction {
    use crate::raw;
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum List { Browse = 0, Owner = 1, Bidder = 2 }
    #[derive(Debug, Clone, Copy, Default)]
    pub struct Auction {
        pub index: u32, pub item_id: u32, pub count: u32, pub min_bid: u32, pub min_increment: u32,
        pub buyout: u32, pub bid: u32, pub time_left: u32, pub quality: u32, pub flags: u32,
    }
    pub fn snapshot(list: List) -> Result<(u32, Vec<Auction>), i32> {
        let mut buf = vec![0u8; 64 * 1024];
        let written = unsafe { raw::auction_snapshot(list as i32, buf.as_mut_ptr(), buf.len() as u32) };
        if written < 0 { return Err(written); }
        let bytes = &buf[..written as usize];
        if bytes.len() < 12 { return Err(-1); }
        let count = u32::from_le_bytes(bytes[0..4].try_into().unwrap()) as usize;
        let total = u32::from_le_bytes(bytes[4..8].try_into().unwrap());
        let mut auctions = Vec::with_capacity(count);
        for i in 0..count {
            let o = 12 + i * 40;
            if o + 40 > bytes.len() { return Err(-1); }
            let f = |j: usize| u32::from_le_bytes(bytes[o + j * 4..o + j * 4 + 4].try_into().unwrap());
            auctions.push(Auction { index: f(0), item_id: f(1), count: f(2), min_bid: f(3), min_increment: f(4),
                                    buyout: f(5), bid: f(6), time_left: f(7), quality: f(8), flags: f(9) });
        }
        Ok((total, auctions))
    }
}

pub mod storage {
    use crate::raw;
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub enum Scope { Account = 0, Character = 1 }

    pub fn set(scope: Scope, key: &str, value: &[u8]) -> i32 {
        unsafe { raw::storage_set(scope as i32, key.as_ptr(), key.len() as u32, value.as_ptr(), value.len() as u32) }
    }
    pub fn set_str(scope: Scope, key: &str, value: &str) -> i32 { set(scope, key, value.as_bytes()) }
    pub fn len(scope: Scope, key: &str) -> i32 {
        unsafe { raw::storage_len(scope as i32, key.as_ptr(), key.len() as u32) }
    }
    pub fn get(scope: Scope, key: &str) -> Result<Vec<u8>, i32> {
        let size = len(scope, key);
        if size < 0 { return Err(size); }
        let mut buf = vec![0u8; size as usize];
        let read = unsafe { raw::storage_get(scope as i32, key.as_ptr(), key.len() as u32, buf.as_mut_ptr(), buf.len() as u32) };
        if read < 0 { return Err(read); }
        buf.truncate(read as usize);
        Ok(buf)
    }
    pub fn get_str(scope: Scope, key: &str) -> Result<String, i32> {
        get(scope, key).map(|v| String::from_utf8_lossy(&v).into_owned())
    }
    pub fn delete(scope: Scope, key: &str) -> i32 {
        unsafe { raw::storage_delete(scope as i32, key.as_ptr(), key.len() as u32) }
    }
    pub fn usage(scope: Scope) -> i64 { unsafe { raw::storage_usage(scope as i32) } }
}

#[derive(Debug, Clone, Copy, Default)]
pub struct Item { pub bag: u32, pub slot: u32, pub item_id: u32, pub count: u32 }

pub fn inventory() -> Result<Vec<Item>, i32> {
    let mut buf = vec![0u8; 64 * 1024];
    let written = unsafe { raw::inventory_snapshot(buf.as_mut_ptr(), buf.len() as u32) };
    if written < 0 { return Err(written); }
    let bytes = &buf[..written as usize];
    if bytes.len() < 4 { return Err(-1); }
    let count = u32::from_le_bytes(bytes[0..4].try_into().unwrap()) as usize;
    let mut items = Vec::with_capacity(count);
    for i in 0..count {
        let o = 4 + i * 16;
        if o + 16 > bytes.len() { return Err(-1); }
        let f = |j: usize| u32::from_le_bytes(bytes[o + j * 4..o + j * 4 + 4].try_into().unwrap());
        items.push(Item { bag: f(0), slot: f(1), item_id: f(2), count: f(3) });
    }
    Ok(items)
}

/// ʕ •ᴥ•ʔ✿ ABI v1.1 event payloads (WEV1), delivered to `wow_on_event_v2` ✿ ʕ •ᴥ•ʔ
#[derive(Debug, Clone, PartialEq)]
pub enum EventArg {
    Nil,
    Bool(bool),
    Number(f64),
    Text(String),
    Truncated(String, u32),
    Redacted,
}

pub fn decode_event(bytes: &[u8]) -> Option<(String, Vec<EventArg>)> {
    if bytes.len() < 18 || &bytes[0..4] != b"WEV1" { return None; }
    let name_len = u16::from_le_bytes(bytes[16..18].try_into().ok()?) as usize;
    if 18 + name_len > bytes.len() { return None; }
    let name = String::from_utf8_lossy(&bytes[18..18 + name_len]).into_owned();
    let mut pos = 18 + name_len;
    let mut args = Vec::new();
    while pos < bytes.len() {
        let tag = bytes[pos];
        pos += 1;
        match tag {
            0 => args.push(EventArg::Nil),
            1 => { if pos >= bytes.len() { return None; } args.push(EventArg::Bool(bytes[pos] != 0)); pos += 1; }
            2 => {
                if pos + 8 > bytes.len() { return None; }
                args.push(EventArg::Number(f64::from_le_bytes(bytes[pos..pos + 8].try_into().ok()?)));
                pos += 8;
            }
            3 | 4 => {
                if pos + 4 > bytes.len() { return None; }
                let orig = u32::from_le_bytes(bytes[pos..pos + 4].try_into().ok()?) as usize;
                pos += 4;
                let stored = if tag == 4 {
                    if pos + 4 > bytes.len() { return None; }
                    let n = u32::from_le_bytes(bytes[pos..pos + 4].try_into().ok()?) as usize;
                    pos += 4;
                    n
                } else { orig };
                if pos + stored > bytes.len() { return None; }
                let text = String::from_utf8_lossy(&bytes[pos..pos + stored]).into_owned();
                pos += stored;
                args.push(if tag == 4 { EventArg::Truncated(text, orig as u32) } else { EventArg::Text(text) });
            }
            5 => args.push(EventArg::Redacted),
            _ => return None,
        }
    }
    Some((name, args))
}

/// ʕ ◕ᴥ◕ ʔ✿ Export from your addon with `wow_addon!(on_load, on_event)` or forward manually ✿ ʕ ◕ᴥ◕ ʔ
#[macro_export]
macro_rules! wow_addon {
    ($on_load:path, $on_event:path) => {
        #[no_mangle]
        pub extern "C" fn wow_on_load() -> i32 { $on_load() }
        #[no_mangle]
        pub extern "C" fn wow_on_event(ptr: *const u8, len: u32) -> i32 {
            let bytes = unsafe { core::slice::from_raw_parts(ptr, len as usize) };
            $on_event(core::str::from_utf8(bytes).unwrap_or(""))
        }
    };
}

#[macro_export]
macro_rules! wow_addon_v2 {
    ($on_load:path, $on_event:path) => {
        #[no_mangle]
        pub extern "C" fn wow_on_load() -> i32 { $on_load() }
        #[no_mangle]
        pub extern "C" fn wow_on_event_v2(ptr: *const u8, len: u32) -> i32 {
            let bytes = unsafe { core::slice::from_raw_parts(ptr, len as usize) };
            match $crate::decode_event(bytes) {
                Some((name, args)) => $on_event(&name, &args),
                None => -1,
            }
        }
    };
}

#[no_mangle]
pub extern "C" fn wow_alloc(len: u32) -> *mut u8 {
    let mut buf = Vec::<u8>::with_capacity(len as usize);
    let ptr = buf.as_mut_ptr();
    core::mem::forget(buf);
    ptr
}

#[no_mangle]
pub extern "C" fn wow_free(ptr: *mut u8, len: u32) {
    if !ptr.is_null() {
        unsafe { drop(Vec::from_raw_parts(ptr, 0, len as usize)) };
    }
}

pub mod frame;
