//! ʕ •ᴥ•ʔ✿ AwesomeWotLK calls. Tables come back as maps, nameplate frames as tokens. ✿ ʕ •ᴥ•ʔ

use crate::lua::{self, Val};

pub fn call(name: &str, args: &[Val]) -> Result<Vec<Val>, i32> {
    lua::awesome_call(name, args)
}

fn present(vals: &[Val]) -> bool {
    match vals.first() {
        Some(Val::Num(n)) => *n != 0.0,
        Some(Val::Bool(v)) => *v,
        Some(Val::Str(_)) => true,
        _ => false,
    }
}

fn text_at(vals: &[Val], index: usize) -> String {
    match vals.get(index) { Some(Val::Str(s)) => s.clone(), _ => String::new() }
}

fn num_at(vals: &[Val], index: usize) -> f64 {
    match vals.get(index) { Some(Val::Num(n)) => *n, _ => 0.0 }
}

fn one_text(vals: &[Val]) -> Option<String> {
    match vals.first() { Some(Val::Str(s)) => Some(s.clone()), _ => None }
}

fn indexed(val: Option<&Val>) -> Vec<String> {
    let Some(Val::Map(fields)) = val else { return Vec::new(); };
    let mut rows = Vec::new();
    for (key, value) in fields {
        let Ok(index) = key.parse::<i64>() else { continue };
        if let Val::Str(text) = value { rows.push((index, text.clone())); }
    }
    rows.sort_by_key(|row| row.0);
    rows.into_iter().map(|row| row.1).collect()
}

fn map_field<'a>(val: &'a Val, key: &str) -> Option<&'a Val> {
    let Val::Map(fields) = val else { return None; };
    fields.iter().find(|(name, _)| name == key).map(|(_, value)| value)
}

pub fn is_window_focused() -> Result<bool, i32> {
    Ok(present(&call("IsWindowFocused", &[])?))
}

pub fn flash_window() -> Result<(), i32> { call("FlashWindow", &[])?; Ok(()) }
pub fn focus_window() -> Result<(), i32> { call("FocusWindow", &[])?; Ok(()) }

pub fn copy_clipboard(text: &str) -> Result<(), i32> {
    call("CopyToClipboard", &[Val::Str(text.into())])?;
    Ok(())
}

pub fn queue_interact(token: Option<&str>) -> Result<(), i32> {
    let args = match token {
        Some(token) => vec![Val::Str(token.into())],
        None => Vec::new(),
    };
    call("QueueInteract", &args)?;
    Ok(())
}

pub fn get_cvar(name: &str) -> Result<String, i32> {
    Ok(text_at(&call("GetCVar", &[Val::Str(name.into())])?, 0))
}

pub fn set_cvar(name: &str, value: &str) -> Result<(), i32> {
    call("SetCVar", &[Val::Str(name.into()), Val::Str(value.into())])?;
    Ok(())
}

pub fn name_plates() -> Result<Vec<String>, i32> {
    let vals = call("C_NamePlate.GetNamePlates", &[])?;
    Ok(indexed(vals.first()))
}

pub fn name_plate_for_unit(unit: &str) -> Result<Option<String>, i32> {
    Ok(one_text(&call("C_NamePlate.GetNamePlateForUnit", &[Val::Str(unit.into())])?))
}

pub fn name_plate_by_guid(guid: &str) -> Result<Option<String>, i32> {
    Ok(one_text(&call("C_NamePlate.GetNamePlateByGUID", &[Val::Str(guid.into())])?))
}

pub fn name_plate_token_by_guid(guid: &str) -> Result<Option<String>, i32> {
    Ok(one_text(&call("C_NamePlate.GetNamePlateTokenByGUID", &[Val::Str(guid.into())])?))
}

pub fn unit_is_controlled(unit: &str) -> Result<bool, i32> { Ok(present(&call("UnitIsControlled", &[Val::Str(unit.into())])?)) }
pub fn unit_is_disarmed(unit: &str) -> Result<bool, i32> { Ok(present(&call("UnitIsDisarmed", &[Val::Str(unit.into())])?)) }
pub fn unit_is_silenced(unit: &str) -> Result<bool, i32> { Ok(present(&call("UnitIsSilenced", &[Val::Str(unit.into())])?)) }

pub fn unit_occupations(unit: &str) -> Result<f64, i32> {
    Ok(num_at(&call("UnitOccupations", &[Val::Str(unit.into())])?, 0))
}

pub fn unit_owner(unit: &str) -> Result<(String, String), i32> {
    let vals = call("UnitOwner", &[Val::Str(unit.into())])?;
    Ok((text_at(&vals, 0), text_at(&vals, 1)))
}

pub fn unit_token_from_guid(guid: &str) -> Result<Option<String>, i32> {
    Ok(one_text(&call("UnitTokenFromGUID", &[Val::Str(guid.into())])?))
}

pub fn inventory_transmog(unit: &str, slot: f64) -> Result<(f64, f64), i32> {
    let vals = call("GetInventoryItemTransmog", &[Val::Str(unit.into()), Val::Num(slot)])?;
    Ok((num_at(&vals, 0), num_at(&vals, 1)))
}

pub fn spell_base_cooldown(spell_id: f64) -> Result<(f64, f64), i32> {
    let vals = call("GetSpellBaseCooldown", &[Val::Num(spell_id)])?;
    Ok((num_at(&vals, 0), num_at(&vals, 1)))
}

pub struct ItemInstant {
    pub id: f64,
    pub class_name: String,
    pub subclass_name: String,
    pub equip_loc: String,
    pub icon: String,
    pub class_id: f64,
    pub subclass_id: f64,
}

pub fn item_info_instant(item: &Val) -> Result<Option<ItemInstant>, i32> {
    let vals = call("GetItemInfoInstant", std::slice::from_ref(item))?;
    if vals.is_empty() { return Ok(None); }
    Ok(Some(ItemInstant {
        id: num_at(&vals, 0),
        class_name: text_at(&vals, 1),
        subclass_name: text_at(&vals, 2),
        equip_loc: text_at(&vals, 3),
        icon: text_at(&vals, 4),
        class_id: num_at(&vals, 5),
        subclass_id: num_at(&vals, 6),
    }))
}

pub struct Voice { pub id: f64, pub name: String }

pub fn tts_voices() -> Result<Vec<Voice>, i32> {
    let vals = call("C_VoiceChat.GetTtsVoices", &[])?;
    let Some(Val::Map(fields)) = vals.first() else { return Ok(Vec::new()); };
    let mut rows = Vec::new();
    for (key, value) in fields {
        let Ok(index) = key.parse::<i64>() else { continue };
        let id = map_field(value, "voiceID").and_then(|v| match v { Val::Num(n) => Some(*n), _ => None }).unwrap_or(0.0);
        let name = map_field(value, "name").and_then(|v| match v { Val::Str(s) => Some(s.clone()), _ => None }).unwrap_or_default();
        rows.push((index, Voice { id, name }));
    }
    rows.sort_by_key(|row| row.0);
    Ok(rows.into_iter().map(|row| row.1).collect())
}

pub fn speak_text(voice: f64, text: &str, destination: f64, rate: f64, volume: f64) -> Result<f64, i32> {
    let vals = call("C_VoiceChat.SpeakText", &[Val::Num(voice), Val::Str(text.into()), Val::Num(destination), Val::Num(rate), Val::Num(volume)])?;
    Ok(num_at(&vals, 0))
}

pub fn stop_speaking() -> Result<(), i32> { call("C_VoiceChat.StopSpeakingText", &[])?; Ok(()) }
pub fn speech_rate() -> Result<f64, i32> { Ok(num_at(&call("C_TTSSettings.GetSpeechRate", &[])?, 0)) }
pub fn speech_volume() -> Result<f64, i32> { Ok(num_at(&call("C_TTSSettings.GetSpeechVolume", &[])?, 0)) }
pub fn speech_voice_id() -> Result<f64, i32> { Ok(num_at(&call("C_TTSSettings.GetSpeechVoiceID", &[])?, 0)) }
pub fn voice_option_name() -> Result<String, i32> { Ok(text_at(&call("C_TTSSettings.GetVoiceOptionName", &[])?, 0)) }

pub fn set_speech_rate(rate: f64) -> Result<(), i32> { call("C_TTSSettings.SetSpeechRate", &[Val::Num(rate)])?; Ok(()) }
pub fn set_speech_volume(volume: f64) -> Result<(), i32> { call("C_TTSSettings.SetSpeechVolume", &[Val::Num(volume)])?; Ok(()) }
pub fn set_voice_option(id: f64) -> Result<(), i32> { call("C_TTSSettings.SetVoiceOption", &[Val::Num(id)])?; Ok(()) }
pub fn set_voice_option_by_name(name: &str) -> Result<(), i32> { call("C_TTSSettings.SetVoiceOptionByName", &[Val::Str(name.into())])?; Ok(()) }
pub fn set_default_tts() -> Result<(), i32> { call("C_TTSSettings.SetDefaultSettings", &[])?; Ok(()) }
pub fn refresh_voices() -> Result<(), i32> { call("C_TTSSettings.RefreshVoices", &[])?; Ok(()) }
