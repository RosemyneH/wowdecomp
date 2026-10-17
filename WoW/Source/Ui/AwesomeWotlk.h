#pragma once
#include <cstdint>
#include "WoW/Source/Object/WGUID.h"

struct lua_State;
class CGNamePlateFrame;

void AwesomeWotlkRegister();
void AwesomeWotlkUnregister();
void AwesomeWotlkService();
void AwesomeWotlkOnNamePlateAdded(CGNamePlateFrame* frame);
void AwesomeWotlkOnNamePlateRemoved(CGNamePlateFrame* frame);
const char* AwesomeWotlkConsumeNameplateToken(const char* token, WOWGUID& guid);
void AwesomeWotlkNoteGroundTarget(const char* kind);
void AwesomeWotlkAfterCast(int32_t spellId, bool succeeded);
const char* AwesomeWotlkTokenForPointer(const void* pointer);
bool AwesomeWotlkIsCall(const char* name);
bool AwesomeWotlkIsCVar(const char* name);
