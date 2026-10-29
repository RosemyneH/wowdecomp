use wow_addon_sdk::lua::{self, Val};
use wow_addon_sdk::*;

struct Window {
    who: String,
    root: i32,
    body: i32,
    edit: i32,
    lines: Vec<String>,
}

fn windows() -> &'static mut Vec<Window> {
    static mut WINDOWS: Vec<Window> = Vec::new();
    unsafe { &mut *std::ptr::addr_of_mut!(WINDOWS) }
}

fn arg_text(arg: Option<&EventArg<'_>>) -> String {
    match arg {
        Some(EventArg::Text(text) | EventArg::Truncated(text, _)) => text.to_string(),
        _ => String::new(),
    }
}

fn build_window(parent: i32, who: &str, slot: usize) -> Result<Window, i32> {
    let root = lua::frame("Frame", parent)?;
    let shift = slot as f64 * 18.0;
    lua::set_size(root, 280.0, 190.0)?;
    lua::set_point(root, "CENTER", parent, "CENTER", 180.0 + shift, 40.0 + shift)?;
    lua::backdrop(root, 0.06, 0.07, 0.09, 0.92)?;
    lua::method(root, "SetFrameStrata", &[Val::Str("DIALOG".into())])?;
    let title = lua::font_string(root)?;
    lua::set_point(title, "TOPLEFT", root, "TOPLEFT", 8.0, -6.0)?;
    lua::method(title, "SetTextColor", &[Val::Num(1.0), Val::Num(0.84), Val::Num(0.47), Val::Num(1.0)])?;
    lua::set_text(title, who)?;
    let body = lua::font_string(root)?;
    lua::set_point(body, "TOPLEFT", root, "TOPLEFT", 8.0, -26.0)?;
    lua::method(body, "SetJustifyH", &[Val::Str("LEFT".into())])?;
    lua::method(body, "SetJustifyV", &[Val::Str("TOP".into())])?;
    lua::set_text(body, "")?;
    let edit = lua::frame("EditBox", root)?;
    lua::set_size(edit, 264.0, 22.0)?;
    lua::set_point(edit, "BOTTOMLEFT", root, "BOTTOMLEFT", 8.0, 8.0)?;
    lua::method(edit, "SetFont", &[Val::Str(r"Fonts\FRIZQT__.TTF".into()), Val::Num(12.0), Val::Str("".into())])?;
    lua::method(edit, "SetAutoFocus", &[Val::Bool(false)])?;
    lua::method(edit, "EnableKeyboard", &[Val::Bool(true)])?;
    lua::method(edit, "EnableMouse", &[Val::Bool(true)])?;
    if lua::bind(edit, "OnEnterPressed") != 0 { return Err(-1); }
    if lua::bind(edit, "OnEscapePressed") != 0 { return Err(-1); }
    Ok(Window { who: who.to_string(), root, body, edit, lines: Vec::new() })
}

fn open(who: &str, line: &str) -> Result<(), i32> {
    let parent = lua::ui_parent()?;
    let wins = windows();
    if let Some(win) = wins.iter_mut().find(|win| win.who == who) {
        if win.lines.len() == 12 { win.lines.remove(0); }
        win.lines.push(line.to_string());
        lua::set_text(win.body, &win.lines.join("\n"))?;
        let _ = lua::method(win.root, "Show", &[]);
        return Ok(());
    }
    if wins.len() == 4 { wins.remove(0); }
    let slot = wins.len();
    let mut win = build_window(parent, who, slot)?;
    win.lines.push(line.to_string());
    lua::set_text(win.body, line)?;
    wins.push(win);
    Ok(())
}

fn on_load() -> i32 {
    let _ = lua::ui_parent();
    for event in ["CHAT_MSG_WHISPER", "CHAT_MSG_WHISPER_INFORM", "WASM_UI"] {
        let code = subscribe(event);
        if code != 0 { return code; }
    }
    let _ = print("WIM rust windows are up. wasmgrant port.wim chat.read");
    0
}

fn on_whisper(name: &str, args: &[EventArg<'_>]) -> i32 {
    if matches!(args.first(), Some(EventArg::Redacted)) || matches!(args.get(1), Some(EventArg::Redacted)) {
        let _ = print("Whispers stay hidden until chat.read is granted");
        return 0;
    }
    let who = {
        let text = arg_text(args.get(1));
        if text.is_empty() { "?".into() } else { text }
    };
    let message = arg_text(args.first());
    let line = if name.ends_with("INFORM") { format!("You: {message}") } else { format!("{who}: {message}") };
    match open(&who, &line) {
        Ok(()) => 0,
        Err(code) => code,
    }
}

fn on_ui(args: &[EventArg<'_>]) -> i32 {
    let script = match args.first() {
        Some(EventArg::Text(text)) => *text,
        _ => return 0,
    };
    let handle = match args.get(1) {
        Some(EventArg::Number(n)) => *n as i32,
        _ => return 0,
    };
    let Some(index) = windows().iter().position(|win| win.edit == handle) else { return 0 };
    if script == "OnEscapePressed" {
        let _ = lua::method(windows()[index].root, "Hide", &[]);
        return 0;
    }
    if script != "OnEnterPressed" { return 0; }
    let text = lua::get_text(windows()[index].edit).unwrap_or_default();
    let text = text.trim();
    if text.is_empty() { return 0; }
    let who = windows()[index].who.clone();
    let _ = lua::call("SendChatMessage", &[
        Val::Str(text.into()),
        Val::Str("WHISPER".into()),
        Val::Nil,
        Val::Str(who.clone()),
    ]);
    let _ = lua::set_text(windows()[index].edit, "");
    let line = format!("You: {text}");
    let _ = open(&who, &line);
    0
}

fn on_event(name: &str, args: &[EventArg<'_>]) -> i32 {
    match name {
        "CHAT_MSG_WHISPER" | "CHAT_MSG_WHISPER_INFORM" => on_whisper(name, args),
        "WASM_UI" => on_ui(args),
        _ => 0,
    }
}

wow_addon_v2!(on_load, on_event);
