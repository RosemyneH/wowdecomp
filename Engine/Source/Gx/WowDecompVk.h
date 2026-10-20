#ifndef ENGINE_SOURCE_GX_WOWDECOMPVK_H
#define ENGINE_SOURCE_GX_WOWDECOMPVK_H

#include <cstdint>

void WowDecompVk_SetWireframe(int32_t enabled);
int32_t WowDecompVk_Wireframe();
void WowDecompVk_SetUnlit(int32_t enabled);
int32_t WowDecompVk_Unlit();
void WowDecompVk_SetFxaa(int32_t enabled);
int32_t WowDecompVk_Fxaa();
void WowDecompVk_SetSharpen(int32_t amount);
int32_t WowDecompVk_Sharpen();
void WowDecompVk_SetMsdfRange(float range);
float WowDecompVk_MsdfRange();
void WowDecompVk_SetMsdfSharp(float sharp);
float WowDecompVk_MsdfSharp();
void WowDecompVk_SetMsdfView(int32_t view);
int32_t WowDecompVk_MsdfView();
void WowDecompVk_RequestShaderReload();
int32_t WowDecompVk_ConsumeShaderReload();
const char* WowDecompVk_Status();
void WowDecompVk_SetStatus(const char* status);

#endif
