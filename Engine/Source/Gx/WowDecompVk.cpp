#include "Engine/Source/Gx/WowDecompVk.h"
#include "Engine/Source/Storm/H/storm.h"

static int32_t s_wireframe;
static int32_t s_unlit;
static int32_t s_fxaa;
static int32_t s_sharpen;
static float s_msdfRange = 4.0f;
static float s_msdfSharp = 1.0f;
static int32_t s_msdfView;
static int32_t s_reload;
static char s_status[512] = "Built-in Vulkan shaders. Drop SPIR-V in Shaders/Vulkan and reload.";

void WowDecompVk_SetWireframe(int32_t enabled) {
    s_wireframe = enabled != 0;
}

int32_t WowDecompVk_Wireframe() {
    return s_wireframe;
}

void WowDecompVk_SetUnlit(int32_t enabled) {
    s_unlit = enabled != 0;
}

int32_t WowDecompVk_Unlit() {
    return s_unlit;
}

void WowDecompVk_SetFxaa(int32_t enabled) {
    s_fxaa = enabled != 0;
}

int32_t WowDecompVk_Fxaa() {
    return s_fxaa;
}

void WowDecompVk_SetSharpen(int32_t amount) {
    if (amount < 0) {
        amount = 0;
    } else if (amount > 4) {
        amount = 4;
    }

    s_sharpen = amount;
}

int32_t WowDecompVk_Sharpen() {
    return s_sharpen;
}

void WowDecompVk_SetMsdfRange(float range) {
    if (range < 1.0f) {
        range = 1.0f;
    } else if (range > 8.0f) {
        range = 8.0f;
    }

    s_msdfRange = range;
}

float WowDecompVk_MsdfRange() {
    return s_msdfRange;
}

void WowDecompVk_SetMsdfSharp(float sharp) {
    if (sharp < 0.25f) {
        sharp = 0.25f;
    } else if (sharp > 4.0f) {
        sharp = 4.0f;
    }

    s_msdfSharp = sharp;
}

float WowDecompVk_MsdfSharp() {
    return s_msdfSharp;
}

void WowDecompVk_SetMsdfView(int32_t view) {
    if (view < 0) {
        view = 0;
    } else if (view > 2) {
        view = 2;
    }

    s_msdfView = view;
}

int32_t WowDecompVk_MsdfView() {
    return s_msdfView;
}

void WowDecompVk_RequestShaderReload() {
    s_reload = 1;
    WowDecompVk_SetStatus("Reload queued.");
}

int32_t WowDecompVk_ConsumeShaderReload() {
    int32_t pending = s_reload;
    s_reload = 0;
    return pending;
}

const char* WowDecompVk_Status() {
    return s_status;
}

void WowDecompVk_SetStatus(const char* status) {
    if (!status) {
        s_status[0] = '\0';
        return;
    }

    SStrCopy(s_status, status, sizeof(s_status));
}
