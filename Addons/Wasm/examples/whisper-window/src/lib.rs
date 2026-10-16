use std::sync::atomic::{AtomicI32, Ordering};
use wow_addon_sdk::ui::*;
use wow_addon_sdk::*;

static ROOT: AtomicI32 = AtomicI32::new(0);
static BODY: AtomicI32 = AtomicI32::new(0);

fn log() -> &'static mut Vec<String> {
    static mut LOG: Vec<String> = Vec::new();
    unsafe { &mut *std::ptr::addr_of_mut!(LOG) }
}

fn check(code: i32) -> Result<(), i32> {
    if code < 0 { Err(code) } else { Ok(()) }
}

fn clip(text: &str, max: usize) -> String {
    let mut out = String::new();
    for ch in text.chars() {
        if out.len() + ch.len_utf8() > max { break; }
        out.push(ch);
    }
    out
}

fn render() -> i32 {
    let body = BODY.load(Ordering::Relaxed);
    let text = if log().is_empty() { "No whispers yet".to_string() } else { log().join("\n") };
    ui_set_text(body, &text)
}

fn remember(line: String) {
    let lines = log();
    if lines.len() == 8 { lines.remove(0); }
    lines.push(line);
}

fn arg_text(arg: Option<&EventArg>) -> Option<String> {
    match arg {
        Some(EventArg::Text(text) | EventArg::Truncated(text, _)) => Some(clip(text, 72)),
        Some(EventArg::Redacted) => None,
        _ => Some(String::new()),
    }
}

fn on_load() -> i32 {
    let root = spawn(0, "frame");
    if root <= 0 { return root; }
    ROOT.store(root, Ordering::Relaxed);
    if let Err(code) = (|| {
        check(set_size(root, 280, 168))?;
        check(set_point(root, BOTTOMRIGHT, 0, BOTTOMRIGHT, -24, 220))?;
        check(set_backdrop(root, 16, 18, 24, 230))?;
        check(set_strata(root, STRATA_DIALOG))?;
        let title = spawn(root, "fontstring");
        if title <= 0 { return Err(title); }
        check(set_point(title, TOPLEFT, 0, TOPLEFT, 10, -8))?;
        check(set_font_size(title, 13))?;
        check(set_text_color(title, 255, 214, 120, 255))?;
        check(ui_set_text(title, "Whispers"))?;
        let body = spawn(root, "fontstring");
        if body <= 0 { return Err(body); }
        BODY.store(body, Ordering::Relaxed);
        check(set_point(body, TOPLEFT, 0, TOPLEFT, 10, -28))?;
        check(set_size(body, 256, 128))?;
        check(set_font_size(body, 11))?;
        check(set_justify(body, JUSTIFY_LEFT, JUSTIFY_TOP))?;
        check(set_text_color(body, 230, 230, 230, 255))?;
        check(ui_set_text(body, "No whispers yet"))?;
        Ok(())
    })() { return code; }
    for event in ["CHAT_MSG_WHISPER", "CHAT_MSG_WHISPER_INFORM"] {
        let code = subscribe(event);
        if code != 0 { return code; }
    }
    let _ = print("Whisper window is up. wasmgrant example.whisper-window chat.read");
    0
}

fn on_event(name: &str, args: &[EventArg]) -> i32 {
    if name != "CHAT_MSG_WHISPER" && name != "CHAT_MSG_WHISPER_INFORM" { return 0; }
    let outgoing = name.ends_with("INFORM");
    let message = arg_text(args.first());
    let who = arg_text(args.get(1));
    let line = match (message, who) {
        (Some(message), Some(who)) if outgoing => format!("To {}: {}", if who.is_empty() { "?" } else { &who }, message),
        (Some(message), Some(who)) => format!("{}: {}", if who.is_empty() { "?" } else { &who }, message),
        _ => "Whispers stay hidden until chat.read is granted".to_string(),
    };
    remember(line);
    render()
}

wow_addon_v2!(on_load, on_event);
