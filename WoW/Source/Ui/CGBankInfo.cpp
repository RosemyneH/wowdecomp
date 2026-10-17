#include "CGBankInfo.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/Types.h"
#include "WoW/Source/Ui/GameUI.h"

// STATIC: 0x00BEB9A0
WOWGUID CGBankInfo::m_banker;

// VERIFY: 2510a390 auto
// OFFSET: 0x0057B8B0
static int32_t BankUpdateHandler(WOWGUID guid, uint32_t fieldOffset, uint32_t fieldSize, const void* oldValue, void* param) {
    FrameScript_SignalEvent(SCRIPT_EVENT_PLAYERBANKBAGSLOTS_CHANGED, nullptr);
    return 1;
}

// VERIFY: e46cf018 auto
// OFFSET: 0x0057BA60
void CGBankInfo::EnterWorld() {
    ClntObjMgrSetObjMirrorHandler(ClntObjMgrGetActivePlayer(), ID_PLAYER, 0x1A, 1, BankUpdateHandler, nullptr, HANDLER_PRIORITY_NORMAL, 0);
}

// VERIFY: 082418b8 auto
// OFFSET: 0x0057B8D0
void CGBankInfo::SetBanker(const WOWGUID& banker) {
    if (banker) {
        CGGameUI::SetInteractTarget(banker);
        CGBankInfo::m_banker = banker;
        FrameScript_SignalEvent(SCRIPT_EVENT_BANKFRAME_OPENED, nullptr);
        return;
    }

    FrameScript_SignalEvent(SCRIPT_EVENT_BANKFRAME_CLOSED, nullptr);
    CGGameUI::ClearInteractTarget(CGBankInfo::m_banker, 0);
    CGBankInfo::m_banker = WOWGUID();
}

// VERIFY: adbb2d02 auto
// OFFSET: 0x0057BA90
void CGBankInfo::LeaveWorld() {
    ClntObjMgrUnsetObjMirrorHandler(ClntObjMgrGetActivePlayer(), ID_PLAYER, 0x1A, BankUpdateHandler, nullptr);
}
