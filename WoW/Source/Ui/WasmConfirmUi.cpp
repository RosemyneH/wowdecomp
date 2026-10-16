#include "WoW/Source/Ui/WasmConfirmUi.h"
#include "Engine/Source/WasmAddon/WasmTypes.h"

// ʕ •ᴥ•ʔ✿ Intended Scrn layer: z-order 11, flags 7, priority-9 input. Paint is unwired; this file holds the confirm state machine and text lines. ✿ ʕ •ᴥ•ʔ

namespace {

constexpr uint64_t kArmMs = 750;

struct Overlay {
    bool visible = false;
    uint32_t id = 0;
    uint64_t shownMs = 0;
    uint64_t nowMs = 0;
    std::vector<std::string> lines;
    bool choiceReady = false;
    WasmConfirmUiResult choice;
};

Overlay g_overlay;
const std::vector<std::string> kNoLines;

void ClearChoice() {
    g_overlay.choiceReady = false;
    g_overlay.choice = {};
}

} // namespace

void WasmConfirmUi_Show(const WasmAddon::ConfirmRequest& request, const std::vector<std::string>& lines) {
    g_overlay.visible = true;
    g_overlay.id = request.id;
    g_overlay.shownMs = request.shownMs ? request.shownMs : g_overlay.nowMs;
    g_overlay.lines = lines;
    ClearChoice();
}

void WasmConfirmUi_Hide(uint32_t id) {
    if (!g_overlay.visible || g_overlay.id != id) return;
    g_overlay.visible = false;
    g_overlay.lines.clear();
}

void WasmConfirmUi_Tick(uint64_t nowMs) {
    g_overlay.nowMs = nowMs;
}

uint32_t WasmConfirmUi_ActiveId() {
    return g_overlay.visible ? g_overlay.id : 0;
}

void WasmConfirmUi_NoteChoice(uint32_t id, bool accepted) {
    if (!g_overlay.visible || g_overlay.id != id || g_overlay.choiceReady) return;
    if (g_overlay.nowMs < g_overlay.shownMs + kArmMs) return;
    g_overlay.choice.id = id;
    g_overlay.choice.accepted = accepted;
    g_overlay.choiceReady = true;
}

bool WasmConfirmUi_Poll(WasmConfirmUiResult& out) {
    if (!g_overlay.choiceReady) {
        out = {};
        return false;
    }
    out = g_overlay.choice;
    ClearChoice();
    return true;
}

const std::vector<std::string>& WasmConfirmUi_Lines() {
    return g_overlay.visible ? g_overlay.lines : kNoLines;
}
