use wow_addon_sdk::storage::{self, Scope};

const KEY: &str = "wim.history";
const VERSION: &str = "v1";
const MAX_NAME: usize = 48;
const MAX_LINE: usize = 80;
const MAX_LINES: usize = 12;
const MAX_BLOB: usize = 8 * 1024;

// ʕ •ᴥ•ʔ✿ Vec keeps insertion order; partners() walks it front to back ✿ ʕ •ᴥ•ʔ
struct State {
    partners: Vec<(String, Vec<String>)>,
}

// ʕ •ᴥ•ʔ✿ wasm32 is single-threaded; this helper is the only access ✿ ʕ •ᴥ•ʔ
fn state() -> &'static mut State {
    static mut STATE: State = State {
        partners: Vec::new(),
    };
    unsafe { &mut *std::ptr::addr_of_mut!(STATE) }
}

pub fn load() {
    let Ok(blob) = storage::get_str(Scope::Character, KEY) else {
        return;
    };
    let Some(partners) = decode(&blob) else {
        return;
    };
    state().partners = partners;
}

pub fn save() {
    let blob = encode(&state().partners);
    let _ = storage::set_str(Scope::Character, KEY, &blob);
}

pub fn push(who: &str, line: &str) {
    remember(&mut state().partners, clamp_name(who), clamp_line(line));
}

pub fn transcript(who: &str) -> String {
    let who = clamp_name(who);
    match state().partners.iter().find(|(name, _)| name == &who) {
        Some((_, lines)) => lines.join("\n"),
        None => String::new(),
    }
}

pub fn partners() -> Vec<String> {
    state()
        .partners
        .iter()
        .map(|(name, _)| name.clone())
        .collect()
}

fn remember(partners: &mut Vec<(String, Vec<String>)>, name: String, line: String) {
    if let Some((_, lines)) = partners.iter_mut().find(|(existing, _)| existing == &name) {
        if lines.len() == MAX_LINES {
            lines.remove(0);
        }
        lines.push(line);
    } else {
        partners.push((name, vec![line]));
    }
}

fn clamp_name(text: &str) -> String {
    text.chars()
        .take(MAX_NAME)
        .map(|c| {
            if c == '\t' || c == '\n' || c == '\r' {
                ' '
            } else {
                c
            }
        })
        .collect()
}

fn clamp_line(text: &str) -> String {
    text.chars()
        .take(MAX_LINE)
        .map(|c| if c == '\n' || c == '\r' { ' ' } else { c })
        .collect()
}

fn decode(blob: &str) -> Option<Vec<(String, Vec<String>)>> {
    let mut rows = blob
        .split('\n')
        .map(|row| row.trim_end_matches('\r'))
        .filter(|row| !row.is_empty());
    if rows.next()? != VERSION {
        return None;
    }
    let mut partners = Vec::new();
    for row in rows {
        let Some((name, line)) = row.split_once('\t') else {
            continue;
        };
        remember(&mut partners, clamp_name(name), clamp_line(line));
    }
    Some(partners)
}

// ʕ •ᴥ•ʔ✿ Whole partners fall off the front until the blob is within 8KB ✿ ʕ •ᴥ•ʔ
fn encode(partners: &[(String, Vec<String>)]) -> String {
    let mut start = 0;
    while start < partners.len() {
        let blob = render(&partners[start..]);
        if blob.len() <= MAX_BLOB {
            return blob;
        }
        start += 1;
    }
    render(&[])
}

fn render(partners: &[(String, Vec<String>)]) -> String {
    let mut out = String::from(VERSION);
    out.push('\n');
    for (name, lines) in partners {
        for line in lines {
            out.push_str(name);
            out.push('\t');
            out.push_str(line);
            out.push('\n');
        }
    }
    out
}
