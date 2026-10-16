#if defined(WOWDECOMP_LINUX)

#include "WoW/Source/Ui/UILCD/LCD.h"

CEzLcd* CGLCD::m_lcd;
int32_t CGLCD::m_chatAlertLines;
uint8_t CGLCD::initComplete;
CGPlayer_C* CGLCD::m_player;
uint8_t CGLCD::m_inWorld;
TSHashTable<LCDHANDLE, HASHKEY_CONSTSTRI> CGLCD::m_handles;
TSList<MAPDATA, TSGetLink<MAPDATA> > CGLCD::m_mapData;
TSList<POIIcon, TSGetLink<POIIcon> > CGLCD::m_POIIcons;

void CGLCD::Initialize() {
}

void CGLCD::Shutdown() {
    CGLCD::m_handles.Clear();
    CGLCD::m_mapData.Clear();
    CGLCD::m_POIIcons.Clear();
    CGLCD::initComplete = 0;
    CGLCD::m_chatAlertLines = 0;
}

void CGLCD::Login() {
}

void CGLCD::Logout() {
}

void CGLCD::EnterWorld() {
}

void CGLCD::LeaveWorld() {
}

void CGLCD::ShowBattlefieldScreen() {
}

void CGLCD::ClearWaitQueueScreen() {
}

void CGLCD::HideBattlefieldAlert() {
}

void CGLCD::OnBattlefieldReady(const BattlefieldStatus* status) {
}

void CGLCD::OnBattlefieldActive() {
}

void CGLCD::IdleDisconnectWarning() {
}

void CGLCD::OnWhisper(const char* name) {
}

void CGLCD::AuctionOutbid(const char* name) {
}

void CGLCD::AuctionExpired(const char* name) {
}

void CGLCD::AuctionSold(const char* name) {
}

void CGLCD::AuctionWon(const char* name) {
}

#endif
