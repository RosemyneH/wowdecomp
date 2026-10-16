mod history;

use wow_addon_sdk::confirm;
use wow_addon_sdk::frame::{self, Frame};
use wow_addon_sdk::*;

const MAX_WINDOWS: usize = 4;
const NAME_CAP: usize = 64;
const TEXT_CAP: usize = 72;

struct Window {
    who: String,
    root: Frame,
    title: Frame,
    body: Frame,
    edit: Frame,
}

struct Notice {
    root: Frame,
    label: Frame,
}

fn windows() -> &'static mut Vec<Window> {
    static mut WINDOWS: Vec<Window> = Vec::new();
    unsafe { &mut *std::ptr::addr_of_mut!(WINDOWS) }
}

fn notice() -> &'static mut Option<Notice> {
    static mut NOTICE: Option<Notice> = None;
    unsafe { &mut *std::ptr::addr_of_mut!(NOTICE) }
}

fn check(code: i32) -> Result<(), i32> {
    if code < 0 { Err(code) } else { Ok(()) }
}

// ʕ •ᴥ•ʔ✿ chat_send rejects '|', controls, and bidi; whisper targets cap at 64 bytes, messages at 72 ✿ ʕ •ᴥ•ʔ
fn chat_char(ch: char) -> bool {
    let cp = ch as u32;
    cp >= 0x20 && cp != 0x7F && ch != '|'
        && !(0x202A..=0x202E).contains(&cp)
        && !(0x2066..=0x2069).contains(&cp)
        && !(0x200B..=0x200D).contains(&cp)
        && cp != 0xFEFF
}

fn clip(text: &str, max: usize) -> String {
    let mut out = String::new();
    for ch in text.chars() {
        if !chat_char(ch) { continue; }
        if out.len() + ch.len_utf8() > max { break; }
        out.push(ch);
    }
    out
}

fn arg_text(arg: Option<&EventArg>) -> &str {
    match arg {
        Some(EventArg::Text(text) | EventArg::Truncated(text, _)) => text,
        _ => "",
    }
}

fn partner(arg: Option<&EventArg>) -> String {
    let who = clip(arg_text(arg), NAME_CAP);
    if who.is_empty() { "?".to_string() } else { who }
}

fn hidden(args: &[EventArg]) -> bool {
    matches!(args.first(), Some(EventArg::Redacted)) || matches!(args.get(1), Some(EventArg::Redacted))
}

fn refresh(win: &Window) -> Result<(), i32> {
    check(win.title.set_text(&win.who))?;
    check(win.body.set_text(&history::transcript(&win.who)))?;
    check(win.root.set_shown(true))?;
    Ok(())
}

fn build_window(who: &str, slot: usize) -> Result<Window, i32> {
    let root = Frame::new(0)?;
    let shift = slot as i32 * 18;
    check(root.set_size(280, 190))?;
    check(root.set_point(frame::CENTER, 0, frame::CENTER, 180 + shift, 40 + shift))?;
    check(root.set_backdrop(16, 18, 24, 230))?;
    check(root.set_strata(frame::STRATA_DIALOG))?;
    check(root.set_mouse(true))?;

    let title = Frame::font_string(root.handle())?;
    check(title.set_point(frame::TOPLEFT, 0, frame::TOPLEFT, 8, -6))?;
    check(title.set_size(264, 16))?;
    check(title.set_font_size(13))?;
    check(title.set_justify(frame::JUSTIFY_LEFT, frame::JUSTIFY_TOP))?;
    check(title.set_text_color(255, 214, 120, 255))?;
    check(title.set_text(who))?;

    let body = Frame::font_string(root.handle())?;
    check(body.set_point(frame::TOPLEFT, 0, frame::TOPLEFT, 8, -26))?;
    check(body.set_size(264, 132))?;
    check(body.set_font_size(11))?;
    check(body.set_justify(frame::JUSTIFY_LEFT, frame::JUSTIFY_TOP))?;
    check(body.set_text_color(230, 230, 230, 255))?;
    check(body.set_text(&history::transcript(who)))?;

    let edit = Frame::edit_box(root.handle())?;
    check(edit.set_size(268, 22))?;
    check(edit.set_point(frame::BOTTOMLEFT, 0, frame::BOTTOMLEFT, 6, 6))?;
    check(edit.set_text(""))?;
    check(edit.set_mouse(true))?;
    check(edit.bind("enter_pressed"))?;
    check(edit.bind("escape"))?;
    check(root.set_shown(true))?;
    Ok(Window { who: who.to_string(), root, title, body, edit })
}

fn open_or_update(who: &str) -> Result<(), i32> {
    let wins = windows();
    if let Some(pos) = wins.iter().position(|w| w.who == who) {
        refresh(&wins[pos])?;
        if pos + 1 != wins.len() {
            let win = wins.remove(pos);
            wins.push(win);
        }
        return Ok(());
    }
    if wins.len() == MAX_WINDOWS {
        wins[0].who = who.to_string();
        check(wins[0].edit.set_text(""))?;
        refresh(&wins[0])?;
        let win = wins.remove(0);
        wins.push(win);
        return Ok(());
    }
    let slot = wins.len();
    wins.push(build_window(who, slot)?);
    Ok(())
}

fn build_notice() -> Result<Notice, i32> {
    let root = Frame::new(0)?;
    check(root.set_size(360, 40))?;
    check(root.set_point(frame::CENTER, 0, frame::CENTER, 0, 80))?;
    check(root.set_backdrop(16, 18, 24, 230))?;
    check(root.set_strata(frame::STRATA_DIALOG))?;
    check(root.set_mouse(true))?;
    let label = Frame::font_string(root.handle())?;
    check(label.set_point(frame::CENTER, 0, frame::CENTER, 0, 0))?;
    check(label.set_size(340, 24))?;
    check(label.set_font_size(12))?;
    check(label.set_justify(frame::JUSTIFY_CENTER, frame::JUSTIFY_MIDDLE))?;
    check(label.set_text_color(255, 214, 120, 255))?;
    Ok(Notice { root, label })
}

fn show_notice() -> i32 {
    if notice().is_none() {
        match build_notice() {
            Ok(built) => *notice() = Some(built),
            Err(code) => return code,
        }
    }
    let Some(note) = notice().as_ref() else { return 0 };
    let wrote = note.label.set_text("Whispers stay hidden until chat.read is granted");
    if wrote < 0 { return wrote; }
    let shown = note.root.set_shown(true);
    if shown < 0 { shown } else { 0 }
}

fn hide_notice() {
    if let Some(note) = notice().as_ref() {
        let _ = note.root.set_shown(false);
    }
}

fn on_load() -> i32 {
    history::load();
    // ʕ •ᴥ•ʔ✿ Roster stays closed; the next whisper opens a window ✿ ʕ •ᴥ•ʔ
    let _ = history::partners();
    for event in ["CHAT_MSG_WHISPER", "CHAT_MSG_WHISPER_INFORM", "WASM_UI"] {
        let _ = subscribe(event);
    }
    let _ = print("WIM is up. wasmgrant port.wim chat.read");
    0
}

fn on_whisper(name: &str, args: &[EventArg]) -> i32 {
    if hidden(args) {
        let _ = show_notice();
        return 0;
    }
    let who = partner(args.get(1));
    let message = clip(arg_text(args.first()), TEXT_CAP);
    let line = if name == "CHAT_MSG_WHISPER_INFORM" {
        format!("You: {message}")
    } else {
        format!("{who}: {message}")
    };
    history::push(&who, &line);
    history::save();
    match open_or_update(&who) {
        Ok(()) => {
            hide_notice();
            0
        }
        Err(code) => code,
    }
}

fn ui_signal(args: &[EventArg]) -> Option<(&str, i32)> {
    let event = match args.first() {
        Some(EventArg::Text(text) | EventArg::Truncated(text, _)) => text.as_str(),
        _ => return None,
    };
    let handle = match args.get(1) {
        Some(EventArg::Number(n)) => *n as i32,
        _ => return None,
    };
    Some((event, handle))
}

fn on_ui(args: &[EventArg]) -> i32 {
    let Some((event, handle)) = ui_signal(args) else { return 0 };
    let Some(index) = windows().iter().position(|w| w.edit.handle() == handle) else { return 0 };
    if event == "escape" {
        return windows()[index].root.set_shown(false);
    }
    if event != "enter_pressed" { return 0; }
    let raw = match windows()[index].edit.get_text() {
        Ok(text) => text,
        Err(code) => return code,
    };
    if raw.is_empty() || raw.contains('|') { return 0; }
    let text = clip(&raw, TEXT_CAP);
    if text.is_empty() { return 0; }
    let who = windows()[index].who.clone();
    let _ = confirm::chat_send(7, &who, &text);
    history::push(&who, &format!("You: {text}"));
    let cleared = windows()[index].edit.set_text("");
    history::save();
    if cleared < 0 { return cleared; }
    match refresh(&windows()[index]) {
        Ok(()) => 0,
        Err(code) => code,
    }
}

fn on_event(name: &str, args: &[EventArg]) -> i32 {
    match name {
        "CHAT_MSG_WHISPER" | "CHAT_MSG_WHISPER_INFORM" => on_whisper(name, args),
        "WASM_UI" => on_ui(args),
        _ => 0,
    }
}

wow_addon_v2!(on_load, on_event);
