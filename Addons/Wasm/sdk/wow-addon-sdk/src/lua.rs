//! ʕ •ᴥ•ʔ✿ One call path into the client Lua state. Any 3.3.5 global or method, from Rust. ✿ ʕ •ᴥ•ʔ

use crate::raw;

#[derive(Clone, Debug, PartialEq)]
pub enum Val {
    Nil,
    Bool(bool),
    Num(f64),
    Str(String),
    Obj(i32),
    Map(Vec<(String, Val)>),
}

pub fn call(name: &str, args: &[Val]) -> Result<Vec<Val>, i32> {
    invoke(0, 0, name, args)
}

pub fn method(obj: i32, name: &str, args: &[Val]) -> Result<Vec<Val>, i32> {
    invoke(1, obj, name, args)
}

pub fn global(name: &str) -> Result<Val, i32> {
    let vals = invoke(2, 0, name, &[])?;
    Ok(vals.into_iter().next().unwrap_or(Val::Nil))
}

pub fn bind(obj: i32, script: &str) -> i32 {
    unsafe { raw::lua_bind(obj, script.as_ptr(), script.len() as u32) }
}

pub fn obj(vals: &[Val]) -> Result<i32, i32> {
    match vals.first() {
        Some(Val::Obj(handle)) => Ok(*handle),
        Some(_) => Err(-1),
        None => Err(-1),
    }
}

pub fn num(vals: &[Val]) -> f64 {
    match vals.first() {
        Some(Val::Num(value)) => *value,
        _ => 0.0,
    }
}

pub fn text(vals: &[Val]) -> String {
    match vals.first() {
        Some(Val::Str(value)) => value.clone(),
        _ => String::new(),
    }
}

pub fn ui_parent() -> Result<i32, i32> {
    match global("UIParent")? {
        Val::Obj(handle) => Ok(handle),
        _ => Err(-8),
    }
}

pub fn frame(kind: &str, parent: i32) -> Result<i32, i32> {
    obj(&call("CreateFrame", &[Val::Str(kind.into()), Val::Nil, Val::Obj(parent)])?)
}

pub fn font_string(parent: i32) -> Result<i32, i32> {
    obj(&method(parent, "CreateFontString", &[
        Val::Nil,
        Val::Str("OVERLAY".into()),
        Val::Str("GameFontHighlightSmall".into()),
    ])?)
}

pub fn set_size(frame: i32, width: f64, height: f64) -> Result<(), i32> {
    method(frame, "SetWidth", &[Val::Num(width)])?;
    method(frame, "SetHeight", &[Val::Num(height)])?;
    Ok(())
}

pub fn set_point(frame: i32, point: &str, relative: i32, rel_point: &str, x: f64, y: f64) -> Result<(), i32> {
    method(frame, "SetPoint", &[
        Val::Str(point.into()),
        if relative == 0 { Val::Nil } else { Val::Obj(relative) },
        Val::Str(rel_point.into()),
        Val::Num(x),
        Val::Num(y),
    ])?;
    Ok(())
}

pub fn set_text(widget: i32, text: &str) -> Result<(), i32> {
    method(widget, "SetText", &[Val::Str(text.into())])?;
    Ok(())
}

pub fn get_text(widget: i32) -> Result<String, i32> {
    Ok(text(&method(widget, "GetText", &[])?))
}

pub fn backdrop(frame: i32, r: f64, g: f64, b: f64, a: f64) -> Result<(), i32> {
    let spec = Val::Map(vec![
        ("bgFile".into(), Val::Str(r"Interface\ChatFrame\ChatFrameBackground".into())),
        ("edgeFile".into(), Val::Str(r"Interface\Tooltips\UI-Tooltip-Border".into())),
        ("tile".into(), Val::Bool(true)),
        ("tileSize".into(), Val::Num(16.0)),
        ("edgeSize".into(), Val::Num(12.0)),
        ("insets".into(), Val::Map(vec![
            ("left".into(), Val::Num(4.0)),
            ("right".into(), Val::Num(4.0)),
            ("top".into(), Val::Num(4.0)),
            ("bottom".into(), Val::Num(4.0)),
        ])),
    ]);
    method(frame, "SetBackdrop", &[spec])?;
    method(frame, "SetBackdropColor", &[Val::Num(r), Val::Num(g), Val::Num(b), Val::Num(a)])?;
    method(frame, "SetBackdropBorderColor", &[Val::Num(0.0), Val::Num(0.0), Val::Num(0.0), Val::Num(1.0)])?;
    Ok(())
}

fn invoke(op: u8, obj: i32, name: &str, args: &[Val]) -> Result<Vec<Val>, i32> {
    let blob = encode(op, obj, name, args);
    let mut out = vec![0u8; 16 * 1024];
    let wrote = unsafe { raw::lua_call(blob.as_ptr(), blob.len() as u32, out.as_mut_ptr(), out.len() as u32) };
    if wrote < 0 { return Err(wrote); }
    decode(&out[..wrote as usize])
}

fn encode(op: u8, obj: i32, name: &str, args: &[Val]) -> Vec<u8> {
    let mut out = Vec::new();
    out.push(op);
    out.extend_from_slice(&obj.to_le_bytes());
    out.extend_from_slice(&(name.len() as u16).to_le_bytes());
    out.extend_from_slice(name.as_bytes());
    for arg in args { write_val(&mut out, arg); }
    out
}

fn write_val(out: &mut Vec<u8>, val: &Val) {
    match val {
        Val::Nil => out.push(0),
        Val::Bool(value) => { out.push(1); out.push(u8::from(*value)); }
        Val::Num(value) => { out.push(2); out.extend_from_slice(&value.to_le_bytes()); }
        Val::Str(value) => {
            out.push(3);
            out.extend_from_slice(&(value.len() as u32).to_le_bytes());
            out.extend_from_slice(value.as_bytes());
        }
        Val::Obj(handle) => { out.push(4); out.extend_from_slice(&handle.to_le_bytes()); }
        Val::Map(fields) => {
            out.push(5);
            out.extend_from_slice(&(fields.len() as u16).to_le_bytes());
            for (key, value) in fields {
                out.extend_from_slice(&(key.len() as u16).to_le_bytes());
                out.extend_from_slice(key.as_bytes());
                write_val(out, value);
            }
        }
    }
}

fn decode(buf: &[u8]) -> Result<Vec<Val>, i32> {
    if buf.len() < 2 { return Err(-1); }
    let count = u16::from_le_bytes([buf[0], buf[1]]) as usize;
    let mut pos = 2;
    let mut vals = Vec::with_capacity(count);
    for _ in 0..count {
        let (val, next) = read_val(buf, pos, 0)?;
        pos = next;
        vals.push(val);
    }
    Ok(vals)
}

fn read_val(buf: &[u8], mut pos: usize, depth: u8) -> Result<(Val, usize), i32> {
    if pos >= buf.len() || depth > 3 { return Err(-1); }
    let tag = buf[pos];
    pos += 1;
    let val = match tag {
        0 => Val::Nil,
        1 => {
            if pos >= buf.len() { return Err(-1); }
            let value = buf[pos] != 0;
            pos += 1;
            Val::Bool(value)
        }
        2 => {
            if pos + 8 > buf.len() { return Err(-1); }
            let value = f64::from_le_bytes(buf[pos..pos + 8].try_into().map_err(|_| -1)?);
            pos += 8;
            Val::Num(value)
        }
        3 => {
            if pos + 4 > buf.len() { return Err(-1); }
            let n = u32::from_le_bytes(buf[pos..pos + 4].try_into().map_err(|_| -1)?) as usize;
            pos += 4;
            if pos + n > buf.len() { return Err(-1); }
            let value = String::from_utf8_lossy(&buf[pos..pos + n]).into_owned();
            pos += n;
            Val::Str(value)
        }
        4 => {
            if pos + 4 > buf.len() { return Err(-1); }
            let handle = i32::from_le_bytes(buf[pos..pos + 4].try_into().map_err(|_| -1)?);
            pos += 4;
            Val::Obj(handle)
        }
        _ => return Err(-1),
    };
    Ok((val, pos))
}
