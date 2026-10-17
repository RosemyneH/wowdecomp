#include "Engine/Source/Gx/WowDecompVk.h"
#include "Engine/Source/Storm/H/storm.h"

static int32_t s_wireframe;
static int32_t s_unlit;
static int32_t s_fxaa;
static int32_t s_sharpen;
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
