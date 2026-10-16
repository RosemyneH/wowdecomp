#ifndef ENGINE_SOURCE_FRAMESCRIPT_FRAMESCRIPT_H
#define ENGINE_SOURCE_FRAMESCRIPT_FRAMESCRIPT_H

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/FrameScript/Types.h"
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Storm/H/stpl.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"

class CImVector;
struct MD5_CTX;
class CStatus;

struct EVENTLISTENERNODE : public TSLinkedNode<EVENTLISTENERNODE> {
    /* 0008 */ FrameScript_Object* m_listener;
};

struct FrameScript_EventObject : public TSHashObject<FrameScript_EventObject, HASHKEY_CONSTSTRI> {
    /* 0018 */ TSList<EVENTLISTENERNODE> m_listeners;
    /* 0024 */ TSList<EVENTLISTENERNODE> m_unregisterListeners;
    /* 0030 */ TSList<EVENTLISTENERNODE> m_registerListeners;
    /* 0040 */ int64_t m_timeUsed;
    /* 0048 */ uint32_t m_signalCount;
    /* 004C */ uint32_t m_pendingSignalCount;

    FrameScript_EventObject();
    ~FrameScript_EventObject();
};

extern lua_State* s_context;
extern TSHashTable<FrameScript_EventObject, HASHKEY_CONSTSTRI> s_scriptEventsHash;
extern double s_scriptTimeDivisor;
extern int32_t s_inErrorHandler;
extern int32_t s_errorHandlerRef;
extern int32_t s_errorHandlerFunc;
extern const luaL_Reg s_extraFuncs[];

int32_t FrameScript_CompileFunction(const char* name, const char* wrapper, const char* body, CStatus* status);
void FrameScript_CreateEvents(const char** names, uint32_t count);
void FrameScript_Destroy();
void FrameScript_Execute(const char* source, const char* filename, const char* taint);
void FrameScript_Execute(int32_t function, FrameScript_Object* object, int32_t argCount, const char* taint, FrameScript_EventObject* event);
int32_t FrameScript_ExecuteBuffer(const char* buffer, size_t bytes, const char* bufferName, CStatus* status, const char* wrapper);
int32_t FrameScript_ExecuteFile(const char* filename, const char* wrapper, MD5_CTX* md5, CStatus* status);
void FrameScript_Flush();
int32_t FrameScript_GetColor(lua_State* L, int32_t index, CImVector& color);
int32_t FrameScript_GetColorNoAlpha(lua_State* L, int32_t index, CImVector& color);
lua_State* FrameScript_GetContext();
const char* FrameScript_GetCurrentObject(lua_State* L, int32_t level);
char* FrameScript_GetCurrentFunction(lua_State* L, char* buffer, uint32_t bufferLen);
int32_t FrameScript_GetErrorHandlerReference();
void FrameScript_GetEventCPUUsage(const char* name, double* time, uint32_t* count);
TSList<EVENTLISTENERNODE>* FrameScript_GetEventListeners(const char* name);
uint32_t FrameScript_GetPluralIndex(int32_t count);
double FrameScript_GetScriptCPUUsage();
const char* FrameScript_GetText(const char* token, int32_t count, FRAMESCRIPT_GENDER gender);
int32_t FrameScript_GetTotalMemoryUsed();
int32_t FrameScript_GetVariable(const char* name, const char*& value);
int32_t FrameScript_HookWrapper(lua_State* L);
int32_t FrameScript_Initialize(int32_t profile);
using FrameScript_EventObserver = void (*)(uint32_t index, const char* eventName, lua_State* L, int32_t argCount);
using FrameScript_EventInterest = bool (*)(uint32_t index);
void FrameScript_SetEventObserver(FrameScript_EventObserver observer, FrameScript_EventInterest interest);
bool FrameScript_ObserverWants(uint32_t index);
int32_t FrameScript_LoadVariables(const char* filename, CStatus* status);
void FrameScript_MemoryCleanup();
void FrameScript_PackVariable(lua_State* L, const char* name, int32_t depth, char* buffer, uint32_t bufferLen);
int32_t FrameScript_PrintVariable(HOSFILE file, const char* name);
void FrameScript_RegisterFunction(const char* name, lua_CFunction function);
void FrameScript_RegisterScriptEvent(FrameScript_Object* object, FrameScript_EventObject* event);
void FrameScript_ReleaseFunction(int32_t function);
void FrameScript_ResetCPUUsage();
void FrameScript_SetPluralRule(PLURAL_RULE rule);
void FrameScript_SignalEvent(uint32_t index, const char* format, ...);
void FrameScript_SignalEvent(uint32_t index, lua_State* L, int32_t argCount);
char* FrameScript_Sprintf(lua_State* L, int32_t index, char* buffer, uint32_t bufferLen);
void FrameScript_UnregisterFunction(const char* name);
void FrameScript_UnregisterScriptEvent(FrameScript_Object* object, FrameScript_EventObject* event);
void SetDecimalConversion(int32_t convert);
void vFrameScript_SignalEvent(uint32_t index, const char* format, va_list args);

#endif
