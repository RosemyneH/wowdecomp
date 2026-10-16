use details_meter::{Arg, Ingest, Meter, Mode, Query, Report};
use wow_addon_sdk::frame::{self, Frame};
use std::collections::HashMap;

const ROWS: usize = 16;
const WIDTH: i32 = 250;
const ROW_H: i32 = 18;

struct Row {
    button: Frame,
    bar: Frame,
    name: Frame,
    value: Frame,
    shown: bool,
    actor: String,
    last_name: String,
    last_value: String,
    last_fill: i32,
    last_color: [u8; 4],
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Page { Current, Overall, Past(usize) }

pub struct Window {
    meter: Meter,
    title: Frame,
    footer: Frame,
    modes: Vec<(Frame, Mode)>,
    segment: Frame,
    segment_text: Frame,
    reset: Frame,
    back: Frame,
    rows: Vec<Row>,
    page: Page,
    mode: Mode,
    spell: Option<String>,
    follow: bool,
    blocked: bool,
    dirty: bool,
    classes: HashMap<String, [u8; 3]>,
    last_title: String,
    last_footer: String,
    last_segment: String,
    spell_open: bool,
    painted_mode: Option<Mode>,
}

impl Window {
    pub fn build() -> Result<Self, i32> {
        let root = Frame::new(0)?;
        apply(root.set_size(WIDTH, 372))?;
        apply(root.set_point(frame::TOPLEFT, 0, frame::TOPLEFT, 8, -150))?;
        apply(root.set_backdrop(12, 12, 14, 220))?;
        apply(root.set_strata(frame::STRATA_HIGH))?;
        apply(root.set_level(40))?;
        let title = label(root.handle(), 8, -6)?;
        apply(title.set_font_size(13))?;
        apply(title.set_text("Details"))?;
        apply(title.set_text_color(255, 210, 90, 255))?;
        let mut modes = Vec::new();
        for (index, mode) in [Mode::Damage, Mode::Healing, Mode::Taken, Mode::Healed].into_iter().enumerate() {
            let (button, _) = chip(root.handle(), 4 + index as i32 * 61, -24, 58, mode.label())?;
            modes.push((button, mode));
        }
        let (segment, segment_text) = chip(root.handle(), 4, -42, 160, "Current")?;
        let (reset, _) = chip(root.handle(), 168, -42, 78, "Reset")?;
        let (back, _) = chip(root.handle(), 4, -42, 242, "Back")?;
        apply(back.set_shown(false))?;
        let mut rows = Vec::with_capacity(ROWS);
        for index in 0..ROWS { rows.push(row(root.handle(), index)?); }
        let footer = label(root.handle(), 8, 6)?;
        apply(footer.set_point(frame::BOTTOMLEFT, 0, frame::BOTTOMLEFT, 8, 6))?;
        apply(footer.set_font_size(10))?;
        apply(footer.set_text_color(180, 180, 180, 255))?;
        Ok(Self {
            meter: Meter::new(),
            title, footer, modes, segment, segment_text, reset, back, rows,
            page: Page::Current,
            mode: Mode::Damage,
            spell: None,
            follow: true,
            blocked: false,
            dirty: true,
            classes: HashMap::new(),
            last_title: String::new(),
            last_footer: String::new(),
            last_segment: String::new(),
            spell_open: false,
            painted_mode: None,
        })
    }

    pub fn in_combat(&self) -> bool { self.meter.in_combat() }
    pub fn is_dirty(&self) -> bool { self.dirty }

    pub fn set_class(&mut self, name: String, rgb: [u8; 3]) {
        if !name.is_empty() { self.classes.insert(name, rgb); }
    }

    pub fn mark_blocked(&mut self) {
        if self.blocked { return; }
        self.blocked = true;
        self.dirty = true;
    }

    pub fn ingest(&mut self, args: &[Arg]) -> bool {
        self.blocked = false;
        match self.meter.ingest(args) {
            Ingest::Started => {
                if self.follow { self.page = Page::Current; self.spell = None; }
                self.dirty = true;
                true
            }
            Ingest::Updated => { self.dirty = true; false }
            Ingest::Ignored => false,
        }
    }

    pub fn click(&mut self, handle: i32) {
        if let Some(mode) = self.modes.iter().find(|(button, _)| button.handle() == handle).map(|(_, mode)| *mode) {
            self.mode = mode;
            self.dirty = true;
            return;
        }
        if handle == self.segment.handle() { self.cycle(); self.dirty = true; return; }
        if handle == self.reset.handle() {
            self.meter.reset();
            self.page = Page::Current;
            self.spell = None;
            self.follow = true;
            self.dirty = true;
            return;
        }
        if handle == self.back.handle() { self.spell = None; self.dirty = true; return; }
        if self.spell.is_some() { return; }
        let actor = self.rows.iter().find(|row| row.button.handle() == handle).map(|row| row.actor.clone());
        if let Some(actor) = actor {
            if !actor.is_empty() { self.spell = Some(actor); self.dirty = true; }
        }
    }

    pub fn tick(&mut self, now: f64, group_locked: bool) {
        if self.meter.in_combat() && self.meter.consider_end(now, group_locked) {
            if self.follow && self.meter.history_len() > 0 {
                self.page = Page::Past(0);
                self.spell = None;
            }
            self.dirty = true;
        }
        if !self.dirty { return; }
        self.paint(now);
        self.dirty = false;
    }

    fn cycle(&mut self) {
        self.page = match self.page {
            Page::Current => { self.follow = false; Page::Overall }
            Page::Overall if self.meter.history_len() > 0 => { self.follow = false; Page::Past(0) }
            Page::Overall => { self.follow = true; Page::Current }
            Page::Past(index) if index + 1 < self.meter.history_len() => { self.follow = false; Page::Past(index + 1) }
            Page::Past(_) => { self.follow = true; Page::Current }
        };
        self.spell = None;
    }

    fn paint(&mut self, now: f64) {
        let query = match self.page {
            Page::Current => Query::Current,
            Page::Overall => Query::Overall,
            Page::Past(index) => Query::Past(index),
        };
        let spell = self.spell.clone();
        let mode = self.mode;
        let report = self.meter.query(query, mode, spell.as_deref(), now);
        let title = match spell.as_deref() {
            Some(name) => format!("Details: {}", clip(name, 18)),
            None => "Details".to_string(),
        };
        write_text(&mut self.last_title, self.title, &title);
        let segment = match self.page {
            Page::Current => "Current".to_string(),
            Page::Overall => "Overall".to_string(),
            Page::Past(index) => format!("Fight {}", index + 1),
        };
        write_text(&mut self.last_segment, self.segment_text, &segment);
        let open = spell.is_some();
        if open != self.spell_open {
            let _ = self.segment.set_shown(!open);
            let _ = self.reset.set_shown(!open);
            let _ = self.back.set_shown(open);
            self.spell_open = open;
        }
        if self.painted_mode != Some(mode) {
            for (button, button_mode) in &self.modes {
                let color: [u8; 4] = if *button_mode == mode { [92, 64, 28, 240] } else { [22, 22, 26, 230] };
                let _ = button.set_backdrop(color[0], color[1], color[2], color[3]);
            }
            self.painted_mode = Some(mode);
        }
        let top = report.lines.first().map(|line| line.total).unwrap_or(0);
        let colors: Vec<[u8; 4]> = report.lines.iter().map(|line| bar_color(&self.classes, &line.name, mode, spell.is_some())).collect();
        let blocked = self.blocked;
        for (index, row) in self.rows.iter_mut().enumerate() {
            if let Some(line) = report.lines.get(index) {
                if !row.shown { let _ = row.button.set_shown(true); row.shown = true; }
                row.actor = if spell.is_some() { String::new() } else { line.name.clone() };
                let name = format!("{}. {}", index + 1, clip(&line.name, 16));
                let share = if report.total == 0 { 0 } else { line.total * 100 / report.total };
                let value = format!("{}  {share}%", compact(line.total));
                if name != row.last_name { let _ = row.name.set_text(&name); row.last_name = name; }
                if value != row.last_value { let _ = row.value.set_text(&value); row.last_value = value; }
                let fill = if top == 0 { 0 } else { (line.total * 1000 / top) as i32 };
                if fill != row.last_fill { let _ = row.bar.set_value(fill, 0, 1000); row.last_fill = fill; }
                let color = colors[index];
                if color != row.last_color {
                    let _ = row.bar.set_color(color[0], color[1], color[2], color[3]);
                    row.last_color = color;
                }
            } else if row.shown {
                let _ = row.button.set_shown(false);
                row.shown = false;
                row.actor.clear();
            }
        }
        write_text(&mut self.last_footer, self.footer, &footer_text(blocked, &report, mode));
    }
}

fn footer_text(blocked: bool, report: &Report, mode: Mode) -> String {
    if blocked { return "grant combat.read".to_string(); }
    if report.total == 0 { return "waiting for combat".to_string(); }
    let per = (report.total as f64 / report.elapsed.max(1.0)) as u64;
    let clock = if report.elapsed >= 60.0 {
        let secs = report.elapsed as u64;
        format!("{}:{:02}", secs / 60, secs % 60)
    } else {
        format!("{:.0}s", report.elapsed.max(0.0))
    };
    format!("{clock}   {}   {}", compact(per), mode.label())
}

fn bar_color(classes: &HashMap<String, [u8; 3]>, name: &str, mode: Mode, spell: bool) -> [u8; 4] {
    if !spell {
        if let Some(rgb) = classes.get(name) { return [rgb[0], rgb[1], rgb[2], 255]; }
    }
    let rgb = match mode {
        Mode::Damage => [196, 92, 48],
        Mode::Healing => [64, 176, 96],
        Mode::Taken => [176, 64, 64],
        Mode::Healed => [72, 156, 196],
    };
    [rgb[0], rgb[1], rgb[2], 255]
}

fn compact(n: u64) -> String {
    if n >= 1_000_000_000 { format!("{:.1}B", n as f64 / 1e9) }
    else if n >= 10_000_000 { format!("{:.0}M", n as f64 / 1e6) }
    else if n >= 1_000_000 { format!("{:.1}M", n as f64 / 1e6) }
    else if n >= 10_000 { format!("{:.0}K", n as f64 / 1e3) }
    else if n >= 1_000 { format!("{:.1}K", n as f64 / 1e3) }
    else { n.to_string() }
}

fn clip(text: &str, max: usize) -> &str {
    if text.len() <= max { return text; }
    let mut end = max;
    while end > 0 && !text.is_char_boundary(end) { end -= 1; }
    &text[..end]
}

fn write_text(last: &mut String, frame: Frame, text: &str) {
    if last == text { return; }
    let _ = frame.set_text(text);
    *last = text.to_string();
}

fn apply(code: i32) -> Result<(), i32> {
    if code < 0 { Err(code) } else { Ok(()) }
}

fn label(parent: i32, x: i32, y: i32) -> Result<Frame, i32> {
    let text = Frame::font_string(parent)?;
    apply(text.set_point(frame::TOPLEFT, 0, frame::TOPLEFT, x, y))?;
    apply(text.set_justify(frame::JUSTIFY_LEFT, frame::JUSTIFY_MIDDLE))?;
    Ok(text)
}

fn chip(parent: i32, x: i32, y: i32, width: i32, caption: &str) -> Result<(Frame, Frame), i32> {
    let button = Frame::button(parent)?;
    apply(button.set_size(width, 16))?;
    apply(button.set_point(frame::TOPLEFT, 0, frame::TOPLEFT, x, y))?;
    apply(button.set_backdrop(22, 22, 26, 230))?;
    apply(button.bind("click"))?;
    let text = Frame::font_string(button.handle())?;
    apply(text.set_point(frame::CENTER, 0, frame::CENTER, 0, 0))?;
    apply(text.set_font_size(10))?;
    apply(text.set_justify(frame::JUSTIFY_CENTER, frame::JUSTIFY_MIDDLE))?;
    apply(text.set_text(caption))?;
    apply(text.set_text_color(230, 230, 230, 255))?;
    Ok((button, text))
}

fn row(parent: i32, index: usize) -> Result<Row, i32> {
    let button = Frame::button(parent)?;
    apply(button.set_size(WIDTH - 8, ROW_H - 2))?;
    apply(button.set_point(frame::TOPLEFT, 0, frame::TOPLEFT, 4, -62 - index as i32 * ROW_H))?;
    apply(button.set_backdrop(8, 8, 10, 160))?;
    apply(button.bind("click"))?;
    apply(button.set_shown(false))?;
    let bar = Frame::status_bar(button.handle())?;
    apply(bar.set_size(WIDTH - 8, ROW_H - 4))?;
    apply(bar.set_point(frame::LEFT, 0, frame::LEFT, 0, 0))?;
    apply(bar.set_mouse(false))?;
    let name = Frame::font_string(button.handle())?;
    apply(name.set_point(frame::LEFT, 0, frame::LEFT, 4, 0))?;
    apply(name.set_font_size(10))?;
    apply(name.set_justify(frame::JUSTIFY_LEFT, frame::JUSTIFY_MIDDLE))?;
    apply(name.set_text_color(255, 255, 255, 255))?;
    let value = Frame::font_string(button.handle())?;
    apply(value.set_point(frame::RIGHT, 0, frame::RIGHT, -4, 0))?;
    apply(value.set_font_size(10))?;
    apply(value.set_justify(frame::JUSTIFY_RIGHT, frame::JUSTIFY_MIDDLE))?;
    apply(value.set_text_color(255, 255, 255, 255))?;
    Ok(Row {
        button, bar, name, value,
        shown: false,
        actor: String::new(),
        last_name: String::new(),
        last_value: String::new(),
        last_fill: -1,
        last_color: [0, 0, 0, 0],
    })
}
