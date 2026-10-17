#include "GuildRegistrar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/Ui/GameUI.h"
#include "WoW/Source/Ui/Types.h"
#include <cstring>
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/Game/ValidateName.h"
#include "WoW/Source/Object/CGPetition.h"
#include "WoW/Source/Object/Player.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"

// STATIC: 0x00C0E570
WOWGUID CGGuildRegistrar::m_registrar;

// STATIC: 0x00C0E558
GuildPetitionInfo CGGuildRegistrar::m_petition;

// VERIFY: 36f61f9c auto
// OFFSET: 0x00599DD0
void CGGuildRegistrar::EnterWorld() {
    CGGuildRegistrar::m_petition.m_index = 0;
    CGGuildRegistrar::m_petition.m_itemID = 0;
    CGGuildRegistrar::m_petition.m_displayID = 0;
    CGGuildRegistrar::m_petition.m_cost = 0;
    CGGuildRegistrar::m_petition.m_charterType = 0;
    CGGuildRegistrar::m_petition.m_requiredSignatures = 0;
}

// VERIFY: 02414e4e auto
// OFFSET: 0x00599E00
void CGGuildRegistrar::SetRegistrar(WOWGUID registrar, const GuildPetitionInfo* petition) {
    CGGameUI::SetInteractTarget(registrar);
    CGGuildRegistrar::m_petition.m_index = 0;
    CGGuildRegistrar::m_petition.m_itemID = 0;
    CGGuildRegistrar::m_petition.m_displayID = 0;
    CGGuildRegistrar::m_petition.m_cost = 0;
    CGGuildRegistrar::m_petition.m_charterType = 0;
    CGGuildRegistrar::m_petition.m_requiredSignatures = 0;
    CGGuildRegistrar::m_registrar = registrar;
    CGGuildRegistrar::m_petition = *petition;
    FrameScript_SignalEvent(SCRIPT_EVENT_GUILD_REGISTRAR_SHOW, nullptr);
}

// VERIFY: 6f5e7e79 auto
// OFFSET: 0x00599E90
void CGGuildRegistrar::CloseRegistrar() {
    if (!CGGuildRegistrar::m_registrar) {
        return;
    }

    CGGameUI::ClearInteractTarget(CGGuildRegistrar::m_registrar, 0);
    CGGuildRegistrar::m_registrar = WOWGUID();
    FrameScript_SignalEvent(SCRIPT_EVENT_GUILD_REGISTRAR_CLOSED, nullptr);
}

// VERIFY: 57b4dc65 auto
// OFFSET: 0x00599ED0
int32_t CGGuildRegistrar::ValidateAndReportGuildName(const char* name) {
    VALIDATE_NAME_RESULT error = ValidateGuildName(s_locale, name);

    if (!error) {
        return 1;
    }

    CGGameUI::DisplayNameError(error);
    return 0;
}

// VERIFY: e9dccf72 manual
// OFFSET: 0x0059A050
void CGGuildRegistrar::BuyGuildCharter(const char* name) {
    if (!name || !*name || !CGGuildRegistrar::m_registrar) {
        return;
    }

    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();

    if (!player) {
        return;
    }

    if (player->m_player->m_playerGuildid) {
        CGGameUI::DisplayError(ERR_ALREADY_IN_GUILD);
        return;
    }

    if (player->GetCoinage() < CGGuildRegistrar::m_petition.m_cost) {
        CGGameUI::DisplayError(ERR_NOT_ENOUGH_MONEY);
        return;
    }

    uint32_t buffer[sizeof(CGPetition) / sizeof(uint32_t)];
    memset(buffer, 0, sizeof(buffer));
    CGPetition* petition = reinterpret_cast<CGPetition*>(buffer);
    SStrCopy(petition->m_title, name, sizeof(petition->m_title));
    petition->m_muid = CGGuildRegistrar::m_petition.m_index;
    CGPlayer_C::BuyPetition(CGGuildRegistrar::m_registrar, petition);
}
