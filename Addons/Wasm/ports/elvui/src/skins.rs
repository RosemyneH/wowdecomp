use wow_addon_sdk::lua::{self, Val};

const FRAMES: [&str; 14] = [
    "CharacterFrame", "SpellBookFrame", "FriendsFrame", "QuestLogFrame",
    "LootFrame", "MerchantFrame", "TradeFrame", "BankFrame",
    "GuildFrame", "TaxiFrame", "TabardFrame", "DressUpFrame",
    "WorldMapFrame", "MailFrame",
];

pub fn enable() {
    for name in FRAMES {
        let Ok(Val::Obj(frame)) = lua::global(name) else { continue };
        let _ = lua::backdrop(frame, 0.05, 0.05, 0.06, 0.94);
        let _ = lua::method(frame, "SetFrameStrata", &[Val::Str("HIGH".into())]);
    }
}
