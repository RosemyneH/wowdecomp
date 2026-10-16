//! ʕ •ᴥ•ʔ✿ WotLK combat-log meter. One event in, no Lua on this path ✿ ʕ •ᴥ•ʔ
//!
//! Host payloads drop the event name, so arg 0 is the timestamp and arg 1 is the
//! token (`SWING_DAMAGE`, …). 3.3.5 has no raid-flag fields between the unit and the spell.

use std::collections::{HashMap, VecDeque};

const GROUP: u32 = 0x1 | 0x2 | 0x4;
const FRIENDLY: u32 = 0x10;
const PET: u32 = 0x1000 | 0x2000;
const HISTORY: usize = 25;
const ACTORS: usize = 80;
const SPELLS: usize = 48;
const IDLE_SECONDS: f64 = 5.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Arg<'a> {
    Nil,
    Num(f64),
    Text(&'a str),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Mode {
    Damage = 0,
    Healing = 1,
    Taken = 2,
    Healed = 3,
}

impl Mode {
    fn index(self) -> usize { self as usize }
    pub fn label(self) -> &'static str {
        match self {
            Mode::Damage => "Damage",
            Mode::Healing => "Healing",
            Mode::Taken => "Taken",
            Mode::Healed => "Healed",
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Query {
    Current,
    Overall,
    Past(usize),
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Ingest {
    Ignored,
    Updated,
    Started,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Line {
    pub name: String,
    pub total: u64,
    pub hits: u32,
    pub crits: u32,
}

#[derive(Clone, Debug, Default)]
pub struct Report {
    pub lines: Vec<Line>,
    pub total: u64,
    pub elapsed: f64,
}

#[derive(Clone, Default)]
struct Spell { total: u64, hits: u32, crits: u32 }

#[derive(Clone, Default)]
struct Actor {
    group: bool,
    totals: [u64; 4],
    spells: [HashMap<String, Spell>; 4],
}

#[derive(Clone, Default)]
struct Segment {
    actors: HashMap<String, Actor>,
    start: f64,
    end: f64,
}

#[derive(Clone)]
pub struct Meter {
    current: Segment,
    overall: Segment,
    history: VecDeque<Segment>,
    pets: HashMap<String, String>,
    in_combat: bool,
    saw_lock: bool,
    last_activity: f64,
    overall_seconds: f64,
}

impl Default for Meter {
    fn default() -> Self { Self::new() }
}

impl Meter {
    pub fn new() -> Self {
        Self {
            current: Segment::default(),
            overall: Segment::default(),
            history: VecDeque::new(),
            pets: HashMap::new(),
            in_combat: false,
            saw_lock: false,
            last_activity: 0.0,
            overall_seconds: 0.0,
        }
    }

    pub fn in_combat(&self) -> bool { self.in_combat }
    pub fn history_len(&self) -> usize { self.history.len() }

    pub fn reset(&mut self) { *self = Self::new(); }

    pub fn ingest(&mut self, args: &[Arg]) -> Ingest {
        if args.len() < 8 { return Ingest::Ignored; }
        let time = match args[0] { Arg::Num(n) => n, _ => 0.0 };
        let token = match args[1] { Arg::Text(t) => t, _ => return Ingest::Ignored };
        match token {
            "SPELL_SUMMON" | "SPELL_CREATE" => {
                self.note_summon(args);
                Ingest::Ignored
            }
            "SWING_DAMAGE" => self.damage(args, time, 8, "Melee"),
            "RANGE_DAMAGE" => self.damage(args, time, 11, text_or(args, 9, "Auto Shot")),
            "SPELL_DAMAGE" | "SPELL_PERIODIC_DAMAGE" | "SPELL_BUILDING_DAMAGE" | "DAMAGE_SHIELD" | "DAMAGE_SPLIT" => {
                self.damage(args, time, 11, text_or(args, 9, "Spell"))
            }
            "ENVIRONMENTAL_DAMAGE" => self.damage(args, time, 9, text_or(args, 8, "Environment")),
            "SWING_MISSED" => self.absorb(args, time, 8, "Melee"),
            "RANGE_MISSED" => self.absorb(args, time, 11, text_or(args, 9, "Auto Shot")),
            "SPELL_MISSED" | "SPELL_PERIODIC_MISSED" | "SPELL_BUILDING_MISSED" | "DAMAGE_SHIELD_MISSED" => {
                self.absorb(args, time, 11, text_or(args, 9, "Spell"))
            }
            "SPELL_HEAL" | "SPELL_PERIODIC_HEAL" => self.heal(args, time, text_or(args, 9, "Heal")),
            _ => Ingest::Ignored,
        }
    }

    // ʕ ◕ᴥ◕ ʔ✿ Close the segment once the group is out of combat and the log has been quiet ✿ ʕ ◕ᴥ◕ ʔ
    pub fn consider_end(&mut self, now: f64, group_locked: bool) -> bool {
        if !self.in_combat || now <= 0.0 { return false; }
        if group_locked { self.saw_lock = true; return false; }
        if !self.saw_lock && now - self.last_activity < IDLE_SECONDS { return false; }
        self.end_segment(now);
        true
    }

    pub fn end_segment(&mut self, now: f64) {
        if !self.in_combat { return; }
        let end = if now >= self.current.start { now } else { self.last_activity.max(self.current.start) };
        let elapsed = (end - self.current.start).max(0.0);
        self.current.end = end;
        self.overall_seconds += elapsed;
        if segment_has_data(&self.current) {
            self.history.push_front(self.current.clone());
            if self.history.len() > HISTORY { self.history.pop_back(); }
        }
        self.current = Segment::default();
        self.in_combat = false;
    }

    pub fn query(&self, query: Query, mode: Mode, actor: Option<&str>, now: f64) -> Report {
        let Some(segment) = self.segment(query) else { return Report::default() };
        let elapsed = self.elapsed(query, now);
        let mode_i = mode.index();
        if let Some(name) = actor {
            let Some(who) = segment.actors.get(name) else { return Report { elapsed, ..Report::default() } };
            let mut lines: Vec<Line> = who.spells[mode_i].iter().map(|(spell, row)| Line {
                name: spell.clone(),
                total: row.total,
                hits: row.hits,
                crits: row.crits,
            }).collect();
            lines.sort_by(|a, b| b.total.cmp(&a.total).then(a.name.cmp(&b.name)));
            let total = who.totals[mode_i];
            return Report { lines, total, elapsed };
        }
        let mut lines: Vec<Line> = segment.actors.iter().filter(|(_, actor)| actor.group && actor.totals[mode_i] > 0).map(|(name, actor)| {
            let spells = &actor.spells[mode_i];
            Line {
                name: name.clone(),
                total: actor.totals[mode_i],
                hits: spells.values().map(|s| s.hits).fold(0, u32::saturating_add),
                crits: spells.values().map(|s| s.crits).fold(0, u32::saturating_add),
            }
        }).collect();
        lines.sort_by(|a, b| b.total.cmp(&a.total).then(a.name.cmp(&b.name)));
        let total = lines.iter().fold(0u64, |sum, line| sum.saturating_add(line.total));
        Report { lines, total, elapsed }
    }

    fn damage(&mut self, args: &[Arg], time: f64, suffix: usize, spell: &str) -> Ingest {
        let Some((amount, crit)) = damage_hit(args, suffix) else { return Ingest::Ignored };
        self.record_damage(args, time, spell, amount, crit)
    }

    fn absorb(&mut self, args: &[Arg], time: f64, suffix: usize, spell: &str) -> Ingest {
        let Some(amount) = absorb_amount(args, suffix) else { return Ingest::Ignored };
        self.record_damage(args, time, spell, amount, false)
    }

    fn record_damage(&mut self, args: &[Arg], time: f64, spell: &str, amount: u64, crit: bool) -> Ingest {
        let (src_guid, src_name, src_flags) = unit(args, 2);
        let (_dst_guid, dst_name, dst_flags) = unit(args, 5);
        let src_group = src_flags & GROUP != 0;
        let dst_group = dst_flags & GROUP != 0;
        let both_friendly = src_flags & FRIENDLY != 0 && dst_flags & FRIENDLY != 0;
        let (owner, pet) = self.credited(src_guid, src_name, src_flags);
        let credit_done = src_group && !owner.is_empty();
        let credit_taken = dst_group && !dst_name.is_empty();
        if !credit_done && !credit_taken { return Ingest::Ignored; }
        let mut started = false;
        if !self.in_combat {
            if both_friendly { return Ingest::Ignored; }
            self.begin(time);
            started = true;
        }
        if credit_done {
            let label = if pet { format!("{src_name}: {spell}") } else { spell.to_string() };
            self.add(&owner, true, Mode::Damage, &label, amount, crit);
        }
        if credit_taken {
            self.add(dst_name, true, Mode::Taken, spell, amount, crit);
        }
        self.last_activity = time;
        if started { Ingest::Started } else { Ingest::Updated }
    }

    fn heal(&mut self, args: &[Arg], time: f64, spell: &str) -> Ingest {
        if !self.in_combat { return Ingest::Ignored; }
        let Some((effective, crit)) = heal_hit(args, 11) else { return Ingest::Ignored };
        let (src_guid, src_name, src_flags) = unit(args, 2);
        let (_dst_guid, dst_name, dst_flags) = unit(args, 5);
        if dst_flags & FRIENDLY == 0 { return Ingest::Ignored; }
        let mut wrote = false;
        if src_flags & GROUP != 0 {
            let (owner, pet) = self.credited(src_guid, src_name, src_flags);
            if !owner.is_empty() {
                let label = if pet { format!("{src_name}: {spell}") } else { spell.to_string() };
                self.add(&owner, true, Mode::Healing, &label, effective, crit);
                wrote = true;
            }
        }
        if dst_flags & GROUP != 0 && !dst_name.is_empty() {
            self.add(dst_name, true, Mode::Healed, spell, effective, crit);
            wrote = true;
        }
        if !wrote { return Ingest::Ignored; }
        self.last_activity = time;
        Ingest::Updated
    }

    fn note_summon(&mut self, args: &[Arg]) {
        let (src_guid, src_name, src_flags) = unit(args, 2);
        let (dst_guid, _dst_name, dst_flags) = unit(args, 5);
        if dst_guid.is_empty() || src_name.is_empty() { return; }
        if dst_flags & PET == 0 && dst_flags != 0 { return; }
        let (owner, _) = self.credited(src_guid, src_name, src_flags);
        if owner.is_empty() { return; }
        self.pets.insert(dst_guid.to_string(), owner);
    }

    fn credited(&self, guid: &str, name: &str, flags: u32) -> (String, bool) {
        if flags & PET != 0 {
            if let Some(owner) = self.pets.get(guid) {
                return (owner.clone(), true);
            }
        }
        (name.to_string(), false)
    }

    fn begin(&mut self, time: f64) {
        self.in_combat = true;
        self.saw_lock = false;
        self.current = Segment { start: time, ..Segment::default() };
        self.last_activity = time;
    }

    fn add(&mut self, name: &str, group: bool, mode: Mode, spell: &str, amount: u64, crit: bool) {
        if name.is_empty() || amount == 0 { return; }
        add_to(&mut self.current, name, group, mode, spell, amount, crit);
        add_to(&mut self.overall, name, group, mode, spell, amount, crit);
    }

    fn segment(&self, query: Query) -> Option<&Segment> {
        match query {
            Query::Current => Some(&self.current),
            Query::Overall => Some(&self.overall),
            Query::Past(index) => self.history.get(index),
        }
    }

    fn elapsed(&self, query: Query, now: f64) -> f64 {
        match query {
            Query::Current if self.in_combat && self.current.start > 0.0 => (now - self.current.start).max(0.0),
            Query::Current if self.current.end > self.current.start => self.current.end - self.current.start,
            Query::Current => 0.0,
            Query::Overall => {
                let live = if self.in_combat && self.current.start > 0.0 { (now - self.current.start).max(0.0) } else { 0.0 };
                self.overall_seconds + live
            }
            Query::Past(index) => self.history.get(index).map(|seg| (seg.end - seg.start).max(0.0)).unwrap_or(0.0),
        }
    }
}

fn add_to(seg: &mut Segment, name: &str, group: bool, mode: Mode, spell: &str, amount: u64, crit: bool) {
    if seg.actors.len() >= ACTORS && !seg.actors.contains_key(name) { return; }
    let actor = seg.actors.entry(name.to_string()).or_default();
    if group { actor.group = true; }
    let index = mode.index();
    actor.totals[index] = actor.totals[index].saturating_add(amount);
    let spells = &mut actor.spells[index];
    if spells.len() >= SPELLS && !spells.contains_key(spell) { return; }
    let row = spells.entry(spell.to_string()).or_default();
    row.total = row.total.saturating_add(amount);
    row.hits = row.hits.saturating_add(1);
    if crit { row.crits = row.crits.saturating_add(1); }
}

fn segment_has_data(seg: &Segment) -> bool {
    seg.actors.values().any(|actor| actor.totals.iter().any(|n| *n > 0))
}

fn unit<'a>(args: &'a [Arg], at: usize) -> (&'a str, &'a str, u32) {
    (text(args, at), text(args, at + 1), flag(args, at + 2))
}

fn text<'a>(args: &'a [Arg], index: usize) -> &'a str {
    match args.get(index) { Some(Arg::Text(value)) => value, _ => "" }
}

fn text_or<'a>(args: &'a [Arg], index: usize, fallback: &'a str) -> &'a str {
    let value = text(args, index);
    if value.is_empty() { fallback } else { value }
}

fn flag(args: &[Arg], index: usize) -> u32 {
    match args.get(index) { Some(Arg::Num(n)) if *n > 0.0 => *n as u32, _ => 0 }
}

fn num_at(args: &[Arg], index: usize) -> Option<f64> {
    match args.get(index) { Some(Arg::Num(n)) => Some(*n), _ => None }
}

fn damage_hit(args: &[Arg], suffix: usize) -> Option<(u64, bool)> {
    let amount = num_at(args, suffix)?.max(0.0);
    let absorbed = num_at(args, suffix + 5).unwrap_or(0.0).max(0.0);
    let total = (amount + absorbed) as u64;
    if total == 0 { return None; }
    let crit = num_at(args, suffix + 6).unwrap_or(0.0) > 0.0;
    Some((total, crit))
}

fn heal_hit(args: &[Arg], suffix: usize) -> Option<(u64, bool)> {
    let amount = num_at(args, suffix)?.max(0.0);
    let over = num_at(args, suffix + 1).unwrap_or(0.0).max(0.0);
    let absorbed = num_at(args, suffix + 2).unwrap_or(0.0).max(0.0);
    let effective = (amount + absorbed - over).max(0.0) as u64;
    if effective == 0 { return None; }
    let crit = num_at(args, suffix + 3).unwrap_or(0.0) > 0.0;
    Some((effective, crit))
}

fn absorb_amount(args: &[Arg], suffix: usize) -> Option<u64> {
    let kind = match args.get(suffix) { Some(Arg::Text(value)) => *value, _ => return None };
    if kind != "ABSORB" { return None; }
    let first = num_at(args, suffix + 1);
    let second = num_at(args, suffix + 2);
    let amount = match (first, second) {
        (Some(offhand), Some(missed)) if offhand <= 1.0 => missed,
        (Some(missed), _) => missed,
        (None, Some(missed)) => missed,
        _ => return None,
    };
    if amount <= 0.0 { None } else { Some(amount as u64) }
}

#[cfg(test)]
mod tests {
    use super::*;

    const PLAYER: u32 = 0x1 | 0x10 | 0x100 | 0x400;
    const MOB: u32 = 0x40 | 0x200 | 0x800;
    const PET_FLAGS: u32 = 0x1 | 0x10 | 0x100 | 0x1000;

    fn n(v: f64) -> Arg<'static> { Arg::Num(v) }

    fn swing<'a>(src: &'a str, sf: u32, dst: &'a str, df: u32, amount: f64, absorbed: f64, crit: bool) -> Vec<Arg<'a>> {
        vec![
            n(100.0), Arg::Text("SWING_DAMAGE"), Arg::Text("S"), Arg::Text(src), n(sf as f64),
            Arg::Text("D"), Arg::Text(dst), n(df as f64),
            n(amount), n(0.0), n(1.0), n(0.0), n(0.0), n(absorbed), if crit { n(1.0) } else { Arg::Nil },
        ]
    }

    fn spell<'a>(token: &'a str, spell_name: &'a str, src: &'a str, sf: u32, dst: &'a str, df: u32, tail: &[Arg<'a>]) -> Vec<Arg<'a>> {
        let mut args = vec![
            n(100.0), Arg::Text(token), Arg::Text("S"), Arg::Text(src), n(sf as f64),
            Arg::Text("D"), Arg::Text(dst), n(df as f64),
            n(133.0), Arg::Text(spell_name), n(4.0),
        ];
        args.extend_from_slice(tail);
        args
    }

    #[test]
    fn swing_opens_a_fight_and_counts_absorb() {
        let mut meter = Meter::new();
        assert_eq!(meter.ingest(&swing("Emma", PLAYER, "Boss", MOB, 1000.0, 250.0, true)), Ingest::Started);
        let report = meter.query(Query::Current, Mode::Damage, None, 104.0);
        assert_eq!(report.total, 1250);
        assert_eq!(report.lines[0].name, "Emma");
        assert_eq!(report.lines[0].crits, 1);
        let taken = meter.query(Query::Current, Mode::Taken, None, 104.0);
        assert_eq!(taken.total, 0);
        let spells = meter.query(Query::Current, Mode::Damage, Some("Emma"), 104.0);
        assert_eq!(spells.lines[0].name, "Melee");
    }

    #[test]
    fn friendly_fire_does_not_open_combat() {
        let mut meter = Meter::new();
        assert_eq!(meter.ingest(&swing("Emma", PLAYER, "Tank", PLAYER, 50.0, 0.0, false)), Ingest::Ignored);
        assert!(!meter.in_combat());
    }

    #[test]
    fn full_absorb_counts_as_damage() {
        let mut meter = Meter::new();
        let args = vec![
            n(10.0), Arg::Text("SWING_MISSED"), Arg::Text("S"), Arg::Text("Emma"), n(PLAYER as f64),
            Arg::Text("D"), Arg::Text("Boss"), n(MOB as f64),
            Arg::Text("ABSORB"), Arg::Nil, n(400.0),
        ];
        assert_eq!(meter.ingest(&args), Ingest::Started);
        assert_eq!(meter.query(Query::Current, Mode::Damage, None, 12.0).total, 400);
    }

    #[test]
    fn pet_damage_lands_on_the_owner() {
        let mut meter = Meter::new();
        let summon = vec![
            n(1.0), Arg::Text("SPELL_SUMMON"), Arg::Text("PlayerGuid"), Arg::Text("Emma"), n(PLAYER as f64),
            Arg::Text("PetGuid"), Arg::Text("Wolf"), n(PET_FLAGS as f64),
            n(9.0), Arg::Text("Call Pet"), n(1.0),
        ];
        assert_eq!(meter.ingest(&summon), Ingest::Ignored);
        let bite = vec![
            n(2.0), Arg::Text("SWING_DAMAGE"), Arg::Text("PetGuid"), Arg::Text("Wolf"), n(PET_FLAGS as f64),
            Arg::Text("D"), Arg::Text("Boss"), n(MOB as f64),
            n(80.0), n(0.0), n(1.0), Arg::Nil, Arg::Nil, Arg::Nil, Arg::Nil,
        ];
        assert_eq!(meter.ingest(&bite), Ingest::Started);
        let report = meter.query(Query::Current, Mode::Damage, None, 3.0);
        assert_eq!(report.lines.len(), 1);
        assert_eq!(report.lines[0].name, "Emma");
        assert_eq!(report.lines[0].total, 80);
        let spells = meter.query(Query::Current, Mode::Damage, Some("Emma"), 3.0);
        assert_eq!(spells.lines[0].name, "Wolf: Melee");
    }

    #[test]
    fn heal_waits_for_combat_and_drops_overheal() {
        let mut meter = Meter::new();
        let heal = spell("SPELL_HEAL", "Flash Heal", "Emma", PLAYER, "Tank", PLAYER, &[n(1000.0), n(200.0), n(50.0), n(1.0)]);
        assert_eq!(meter.ingest(&heal), Ingest::Ignored);
        meter.ingest(&swing("Emma", PLAYER, "Boss", MOB, 10.0, 0.0, false));
        let again = spell("SPELL_HEAL", "Flash Heal", "Emma", PLAYER, "Tank", PLAYER, &[n(1000.0), n(200.0), n(50.0), n(1.0)]);
        assert_eq!(meter.ingest(&again), Ingest::Updated);
        assert_eq!(meter.query(Query::Current, Mode::Healing, None, 101.0).total, 850);
        assert_eq!(meter.query(Query::Current, Mode::Healed, None, 101.0).lines[0].name, "Tank");
    }

    #[test]
    fn segments_archive_and_overall_sums_them() {
        let mut meter = Meter::new();
        let mut first = swing("Emma", PLAYER, "Boss", MOB, 100.0, 0.0, false);
        first[0] = n(10.0);
        meter.ingest(&first);
        assert!(!meter.consider_end(14.0, false));
        assert!(!meter.consider_end(16.0, true));
        assert!(meter.in_combat());
        assert!(meter.consider_end(16.0, false));
        assert!(!meter.in_combat());
        let mut second = swing("Emma", PLAYER, "Boss", MOB, 200.0, 0.0, false);
        second[0] = n(30.0);
        meter.ingest(&second);
        assert_eq!(meter.query(Query::Current, Mode::Damage, None, 32.0).total, 200);
        assert_eq!(meter.query(Query::Past(0), Mode::Damage, None, 32.0).total, 100);
        assert_eq!(meter.query(Query::Overall, Mode::Damage, None, 32.0).total, 300);
        assert_eq!(meter.history_len(), 1);
    }

    #[test]
    fn dropping_group_combat_closes_the_segment() {
        let mut meter = Meter::new();
        let mut hit = swing("Emma", PLAYER, "Boss", MOB, 100.0, 0.0, false);
        hit[0] = n(10.0);
        meter.ingest(&hit);
        assert!(!meter.consider_end(11.0, true));
        assert!(meter.consider_end(11.0, false));
        assert!(!meter.in_combat());
        assert_eq!(meter.history_len(), 1);
    }

    #[test]
    fn boss_hits_record_damage_taken() {
        let mut meter = Meter::new();
        meter.ingest(&swing("Boss", MOB, "Emma", PLAYER, 500.0, 0.0, false));
        let taken = meter.query(Query::Current, Mode::Taken, None, 101.0);
        assert_eq!(taken.lines[0].name, "Emma");
        assert_eq!(taken.total, 500);
        assert_eq!(meter.query(Query::Current, Mode::Damage, None, 101.0).total, 0);
    }
}
