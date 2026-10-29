#pragma once

void WasmAddonClient_Initialize();
void WasmAddonClient_Shutdown();
void WasmAddonClient_RegisterScripts();
void WasmAddonClient_UnregisterScripts();
void WasmAddonClient_LoadWorldAddOns();
void WasmAddonClient_ServiceConfirms();
void WasmAddonClient_OnScriptReset();
void WasmAddonClient_UnloadWorldAddOns();
bool WasmAddonClient_LoadPackage(const char* name);
bool WasmAddonClient_UnloadPackage(const char* name);
bool WasmAddonClient_ReloadPackage(const char* name);
void WasmAddonClient_BeginEventBurst();
void WasmAddonClient_EndEventBurst();

struct WasmAddonEventBurst {
    WasmAddonEventBurst() { WasmAddonClient_BeginEventBurst(); }
    ~WasmAddonEventBurst() { WasmAddonClient_EndEventBurst(); }
};
