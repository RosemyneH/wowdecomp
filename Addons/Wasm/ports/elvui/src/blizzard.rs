use crate::elv as core;

const FRAMES: [&str; 12] = [
    "PlayerFrame", "TargetFrame", "FocusFrame", "PetFrame",
    "PartyMemberFrame1", "PartyMemberFrame2", "PartyMemberFrame3", "PartyMemberFrame4",
    "CastingBarFrame", "Boss1TargetFrame", "Boss2TargetFrame", "Boss3TargetFrame",
];

pub fn enable() {
    for name in FRAMES { core::hide(name); }
}
