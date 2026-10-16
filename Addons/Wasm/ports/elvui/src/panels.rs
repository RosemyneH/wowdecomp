use std::sync::OnceLock;

use wow_addon_sdk::frame::{self, Frame};
use wow_addon_sdk::perf;

static MEMORY: OnceLock<Frame> = OnceLock::new();

fn check(code: i32) -> Result<(), i32> {
    if code < 0 { Err(code) } else { Ok(()) }
}

fn host(result: Result<Frame, i32>) -> Result<Frame, i32> {
    result.map_err(|code| if code < 0 { code } else { -1 })
}

// ʕ •ᴥ•ʔ✿ perf(0) is bytes; show integer KB, or a dash when the counter is missing ✿ ʕ •ᴥ•ʔ
fn memory_label() -> String {
    let bytes = perf(0);
    if bytes < 0 {
        "mem --".to_string()
    } else {
        format!("mem {}K", bytes / 1024)
    }
}

pub fn build() -> i32 {
    match layout() {
        Ok(()) => 0,
        Err(code) => code,
    }
}

fn layout() -> Result<(), i32> {
    let bar = host(Frame::new(0))?;
    check(bar.set_size(420, 22))?;
    check(bar.set_point(frame::TOP, 0, frame::TOP, 0, -8))?;
    check(bar.set_backdrop(10, 10, 12, 210))?;
    check(bar.set_strata(frame::STRATA_DIALOG))?;
    let parent = bar.handle();

    let left = host(Frame::font_string(parent))?;
    check(left.set_point(frame::TOPLEFT, parent, frame::TOPLEFT, 8, -4))?;
    check(left.set_font_size(11))?;
    check(left.set_justify(frame::JUSTIFY_LEFT, frame::JUSTIFY_MIDDLE))?;
    check(left.set_text_color(255, 214, 120, 255))?;
    check(left.set_text("ElvUI"))?;

    let center = host(Frame::font_string(parent))?;
    check(center.set_point(frame::TOP, parent, frame::TOP, 0, -4))?;
    check(center.set_font_size(11))?;
    check(center.set_justify(frame::JUSTIFY_CENTER, frame::JUSTIFY_MIDDLE))?;
    check(center.set_text_color(220, 220, 220, 255))?;
    check(center.set_text(&memory_label()))?;
    let _ = MEMORY.set(center);

    let right = host(Frame::font_string(parent))?;
    check(right.set_point(frame::TOPRIGHT, parent, frame::TOPRIGHT, -8, -4))?;
    check(right.set_font_size(11))?;
    check(right.set_justify(frame::JUSTIFY_RIGHT, frame::JUSTIFY_MIDDLE))?;
    check(right.set_text_color(220, 220, 220, 255))?;
    check(right.set_text("3.3.5"))?;
    Ok(())
}

pub fn refresh() -> i32 {
    let Some(text) = MEMORY.get() else { return 0 };
    text.set_text(&memory_label())
}
