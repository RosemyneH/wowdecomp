use wow_addon_sdk::awesome;
use wow_addon_sdk::frame::{self, Frame};
use wow_addon_sdk::*;

struct Panel {
    status: Frame,
    rows: Vec<(Frame, &'static str)>,
    stack: Frame,
    distance: Frame,
    show: Frame,
    camera: Frame,
    voice: Frame,
    flash: Frame,
}

fn panel() -> &'static mut Option<Panel> {
    static mut PANEL: Option<Panel> = None;
    unsafe { &mut *std::ptr::addr_of_mut!(PANEL) }
}

fn label(parent: i32, text: &str, y: i32) -> Result<Frame, i32> {
    let line = Frame::font_string(parent)?;
    line.set_point(frame::TOPLEFT, parent, frame::TOPLEFT, 12, y);
    line.set_text_color(220, 210, 180, 255);
    line.set_text(text);
    Ok(line)
}

fn button(parent: i32, text: &str, x: i32, y: i32) -> Result<Frame, i32> {
    let button = Frame::button(parent)?;
    button.set_size(86, 22);
    button.set_point(frame::TOPLEFT, parent, frame::TOPLEFT, x, y);
    button.set_backdrop(36, 32, 24, 230);
    button.set_text(text);
    button.set_mouse(true);
    button.bind("click");
    Ok(button)
}

fn build() -> Result<(), i32> {
    let root = Frame::new(0)?;
    root.set_size(300, 220);
    root.set_point(frame::TOPRIGHT, 0, frame::TOPRIGHT, -24, -80);
    root.set_backdrop(18, 16, 14, 230);
    root.set_strata(frame::STRATA_HIGH);
    root.set_mouse(true);
    let title = Frame::font_string(root.handle())?;
    title.set_point(frame::TOPLEFT, root.handle(), frame::TOPLEFT, 12, -8);
    title.set_text_color(255, 214, 120, 255);
    title.set_text("Awesome");
    let status = label(root.handle(), "grant awesome.api", -28)?;
    let names = ["nameplateDistance", "nameplateStacking", "cameraFov", "cameraIndirectVisibility", "showPlayer", "ttsVolume"];
    let mut rows = Vec::new();
    for (i, name) in names.iter().enumerate() {
        rows.push((label(root.handle(), name, -48 - i as i32 * 16)?, *name));
    }
    *panel() = Some(Panel {
        status,
        rows,
        stack: button(root.handle(), "Stack", 12, -160)?,
        distance: button(root.handle(), "Distance", 104, -160)?,
        show: button(root.handle(), "Player", 196, -160)?,
        camera: button(root.handle(), "Camera", 12, -186)?,
        voice: button(root.handle(), "Voice", 104, -186)?,
        flash: button(root.handle(), "Flash", 196, -186)?,
    });
    Ok(())
}

fn toggle(name: &str) {
    let current = awesome::get_cvar(name).unwrap_or_else(|_| "0".into());
    let next = if current == "0" { "1" } else { "0" };
    let _ = awesome::set_cvar(name, next);
}

fn on_click(handle: i32) {
    let Some(ui) = panel() else { return };
    if handle == ui.stack.handle() { toggle("nameplateStacking"); }
    else if handle == ui.show.handle() { toggle("showPlayer"); }
    else if handle == ui.camera.handle() { toggle("cameraIndirectVisibility"); }
    else if handle == ui.distance.handle() {
        let current = awesome::get_cvar("nameplateDistance").ok().and_then(|text| text.parse::<i32>().ok()).unwrap_or(41);
        let next = if current >= 80 { 20 } else { current + 10 };
        let _ = awesome::set_cvar("nameplateDistance", &next.to_string());
    } else if handle == ui.voice.handle() {
        let _ = awesome::speak_text(1.0, "Awesome WotLK", 1.0, 0.0, 100.0);
    } else if handle == ui.flash.handle() {
        let _ = awesome::flash_window();
    }
}

fn refresh() {
    let Some(ui) = panel() else { return };
    let focused = awesome::is_window_focused().ok();
    let plates = awesome::name_plates().ok();
    let silenced = awesome::unit_is_silenced("player").ok();
    let status = match focused {
        None => "grant awesome.api".to_string(),
        Some(focused) => format!(
            "{}  plates {}  {}",
            if focused { "focused" } else { "behind" },
            plates.map(|list| list.len()).unwrap_or(0),
            if silenced == Some(true) { "silenced" } else { "clear" }
        ),
    };
    ui.status.set_text(&status);
    for (line, name) in ui.rows.clone() {
        let value = awesome::get_cvar(name).unwrap_or_else(|code| code.to_string());
        line.set_text(&format!("{name} {value}"));
    }
}

fn on_load() -> i32 {
    if let Err(code) = build() { return code; }
    for event in ["CHAT_MSG_WHISPER", "NAME_PLATE_UNIT_ADDED", "PLAYER_ENTERING_WORLD", "WASM_UI"] {
        let _ = subscribe(event);
    }
    0
}

fn on_event(name: &str, args: &[EventArg]) -> i32 {
    match name {
        "CHAT_MSG_WHISPER" => { let _ = awesome::flash_window(); }
        "NAME_PLATE_UNIT_ADDED" | "PLAYER_ENTERING_WORLD" => refresh(),
        "WASM_UI" => {
            let script = match args.first() { Some(EventArg::Text(text)) => text.as_str(), _ => return 0 };
            let handle = match args.get(1) { Some(EventArg::Number(n)) => *n as i32, _ => return 0 };
            if script == "click" { on_click(handle); refresh(); }
        }
        _ => {}
    }
    0
}

wow_addon_v2!(on_load, on_event);

#[no_mangle]
pub extern "C" fn wow_on_update() -> i32 {
    refresh();
    let _ = awesome::unit_occupations("player");
    let _ = awesome::spell_base_cooldown(768.0);
    0
}
