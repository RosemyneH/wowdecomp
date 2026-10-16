#ifndef WOW_SOURCE_UI_WASMCONFIRMUI_H
#define WOW_SOURCE_UI_WASMCONFIRMUI_H

#include <cstdint>
#include <string>
#include <vector>

namespace WasmAddon {
struct ConfirmRequest;
}

struct WasmConfirmUiResult {
    uint32_t id = 0;
    bool accepted = false;
};

void WasmConfirmUi_Show(const WasmAddon::ConfirmRequest& request, const std::vector<std::string>& lines);
void WasmConfirmUi_Hide(uint32_t id);
void WasmConfirmUi_Tick(uint64_t nowMs);
void WasmConfirmUi_NoteChoice(uint32_t id, bool accepted);
uint32_t WasmConfirmUi_ActiveId();
bool WasmConfirmUi_Poll(WasmConfirmUiResult& out);
const std::vector<std::string>& WasmConfirmUi_Lines();

#endif
