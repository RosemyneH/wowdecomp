pub use crate::ui::{
    TOPLEFT, TOP, TOPRIGHT, LEFT, CENTER, RIGHT, BOTTOMLEFT, BOTTOM, BOTTOMRIGHT,
    JUSTIFY_LEFT, JUSTIFY_CENTER, JUSTIFY_RIGHT, JUSTIFY_TOP, JUSTIFY_MIDDLE, JUSTIFY_BOTTOM,
    STRATA_HIGH, STRATA_DIALOG, STRATA_MEDIUM,
};

const SET_ALPHA: i32 = 12;
const SET_MOUSE: i32 = 13;
const SET_TEXTURE: i32 = 14;

#[derive(Clone, Copy, Debug)]
pub struct Frame {
    handle: i32,
}

impl Frame {
    pub fn spawn(parent: i32, kind: &str) -> Result<Self, i32> {
        let handle = crate::ui::spawn(parent, kind);
        if handle <= 0 { Err(handle) } else { Ok(Frame { handle }) }
    }
    // ป •ᴥ•ʔ✿ host kinds are concatenated lowercase: fontstring, statusbar, editbox, scrollframe ✿ ป •ᴥ•ʔ
    pub fn new(parent: i32) -> Result<Self, i32> { Self::spawn(parent, "frame") }
    pub fn button(parent: i32) -> Result<Self, i32> { Self::spawn(parent, "button") }
    pub fn status_bar(parent: i32) -> Result<Self, i32> { Self::spawn(parent, "statusbar") }
    pub fn font_string(parent: i32) -> Result<Self, i32> { Self::spawn(parent, "fontstring") }
    pub fn texture(parent: i32) -> Result<Self, i32> { Self::spawn(parent, "texture") }
    pub fn edit_box(parent: i32) -> Result<Self, i32> { Self::spawn(parent, "editbox") }
    pub fn scroll_frame(parent: i32) -> Result<Self, i32> { Self::spawn(parent, "scrollframe") }

    pub fn handle(&self) -> i32 { self.handle }
    pub fn set_size(&self, w: i32, h: i32) -> i32 { crate::ui::set_size(self.handle, w, h) }
    pub fn set_point(&self, point: u8, relative_handle: i32, rel_point: u8, x: i32, y: i32) -> i32 {
        crate::ui::set_point(self.handle, point, relative_handle, rel_point, x, y)
    }
    pub fn set_color(&self, r: u8, g: u8, b: u8, a: u8) -> i32 { crate::ui::set_color(self.handle, r, g, b, a) }
    pub fn set_text_color(&self, r: u8, g: u8, b: u8, a: u8) -> i32 { crate::ui::set_text_color(self.handle, r, g, b, a) }
    pub fn set_backdrop(&self, r: u8, g: u8, b: u8, a: u8) -> i32 { crate::ui::set_backdrop(self.handle, r, g, b, a) }
    pub fn set_shown(&self, shown: bool) -> i32 { crate::ui::set_shown(self.handle, shown) }
    pub fn set_value(&self, value: i32, min: i32, max: i32) -> i32 { crate::ui::set_value(self.handle, value, min, max) }
    pub fn set_justify(&self, h: u8, v: u8) -> i32 { crate::ui::set_justify(self.handle, h, v) }
    pub fn set_font_size(&self, pixels: i32) -> i32 { crate::ui::set_font_size(self.handle, pixels) }
    pub fn set_level(&self, level: i32) -> i32 { crate::ui::set_level(self.handle, level) }
    pub fn set_strata(&self, strata: u8) -> i32 { crate::ui::set_strata(self.handle, strata) }
    // ป •ᴥ•ʔ✿ op 12 one le i32 (0..=1000); op 13 one u8; op 14 raw path bytes ✿ ป •ᴥ•ʔ
    pub fn set_alpha_millis(&self, alpha: i32) -> i32 {
        let blob = alpha.to_le_bytes();
        unsafe { crate::raw::ui_style(self.handle, SET_ALPHA, blob.as_ptr(), blob.len() as u32) }
    }
    pub fn set_mouse(&self, enabled: bool) -> i32 {
        let blob = [u8::from(enabled)];
        unsafe { crate::raw::ui_style(self.handle, SET_MOUSE, blob.as_ptr(), blob.len() as u32) }
    }
    pub fn set_texture(&self, path: &str) -> i32 {
        unsafe { crate::raw::ui_style(self.handle, SET_TEXTURE, path.as_ptr(), path.len() as u32) }
    }
    pub fn set_text(&self, text: &str) -> i32 { crate::ui_set_text(self.handle, text) }
    pub fn bind(&self, event: &str) -> i32 {
        unsafe { crate::raw::ui_bind(self.handle, event.as_ptr(), event.len() as u32) }
    }
    // ป •ᴥ•ʔ✿ one 1024-byte read; -7 does not report how many bytes were needed ✿ ป •ᴥ•ʔ
    pub fn get_text(&self) -> Result<String, i32> {
        let mut buf = [0u8; 1024];
        let n = unsafe { crate::raw::ui_get_text(self.handle, buf.as_mut_ptr(), buf.len() as u32) };
        if n < 0 { return Err(n); }
        let n = (n as usize).min(buf.len());
        Ok(String::from_utf8_lossy(&buf[..n]).into_owned())
    }
}
