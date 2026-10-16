#include "WoW/Source/Ui/WasmConfirmOverlay.h"
#include "WoW/Source/Ui/WasmConfirmUi.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Event/Event.h"
#include "Engine/Source/Gxu/GxuFont.h"
#include "Engine/Source/Scrn/Scrn.h"
#include "Engine/Source/Services/TextBlock.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CImVector.h"
#include <string>
#include <vector>

// ʕ •ᴥ•ʔ✿ Scrn list paints low z first. Sys messages are z 10 flags 7; this sits above them at z 11. ✿ ʕ •ᴥ•ʔ

namespace {

HLAYER s_layer;
CGxStringBatch* s_batch;
CGxFont* s_font;
float s_fontHeight;
std::vector<std::string> s_cached;
std::vector<CGxString*> s_strings;

void ClearStrings() {
    for (CGxString*& string : s_strings) {
        if (string) GxuFontDestroyString(string);
    }
    s_strings.clear();
    s_cached.clear();
}

void Paint(void*, const RECTF* rect, const RECTF*, float) {
    const std::vector<std::string>& lines = WasmConfirmUi_Lines();
    if (lines.empty()) {
        if (!s_strings.empty()) ClearStrings();
        return;
    }

    if (lines != s_cached) {
        ClearStrings();
        s_cached = lines;
        const CImVector color(0xFF, 0xFF, 0xFF, 0xFF);
        s_strings.resize(lines.size());
        for (size_t i = 0; i < lines.size(); ++i) {
            const float y = rect->top - 0.38f - static_cast<float>(i) * (s_fontHeight * 1.35f);
            const C3Vector position((rect->left + rect->right) * 0.5f, y, 1.0f);
            GxuFontCreateString(s_font, lines[i].c_str(), 0.0f, position, 1.0f, s_fontHeight, 0.0f, s_strings[i], GxVJ_Middle, GxHJ_Center, 0x1F44, color, -0.45f, 1.0f);
        }
    }

    for (size_t i = 0; i < s_strings.size(); ++i) {
        if (!s_strings[i]) continue;
        const float y = rect->top - 0.38f - static_cast<float>(i) * (s_fontHeight * 1.35f);
        GxuFontSetStringPosition(s_strings[i], C3Vector((rect->left + rect->right) * 0.5f, y, 1.0f));
        GxuFontAddToBatch(s_batch, s_strings[i]);
    }
    GxuFontRenderBatch(s_batch);
    GxuFontClearBatch(s_batch);
}

int32_t OnKeyDown(const void* data, void*) {
    uint32_t id = WasmConfirmUi_ActiveId();
    if (!id) return 1;

    const EVENT_DATA_KEY* key = static_cast<const EVENT_DATA_KEY*>(data);
    if (key->m_key == KEY_Y || key->m_key == KEY_ENTER) {
        WasmConfirmUi_NoteChoice(id, true);
        return 0;
    }
    if (key->m_key == KEY_N || key->m_key == KEY_ESCAPE) {
        WasmConfirmUi_NoteChoice(id, false);
        return 0;
    }
    return 0;
}

} // namespace

void WasmConfirmOverlay_Initialize() {
    if (s_layer) return;

    s_batch = GxuFontCreateBatch(false, false);
    if (!s_batch) return;

    s_font = TextBlockGetFontPtr(ScrnGetStockFont(STOCK_PERFFONT));
    s_fontHeight = ScrnGetStockFontHeight(STOCK_PERFFONT);
    if (!s_font || s_fontHeight == 0.0f) {
        GxuFontDestroyBatch(s_batch);
        s_batch = nullptr;
        return;
    }

    const RECTF rect = { 0.0f, 0.0f, 1.0f, 1.0f };
    ScrnLayerCreate(&rect, 11.0f, 7, nullptr, Paint, &s_layer);
    if (!s_layer) {
        GxuFontDestroyBatch(s_batch);
        s_batch = nullptr;
        return;
    }

    EventRegisterEx(EVENT_ID_KEYDOWN, OnKeyDown, nullptr, 9.0f);
    EventRegisterEx(EVENT_ID_KEYDOWN_REPEATING, OnKeyDown, nullptr, 9.0f);
}

void WasmConfirmOverlay_Shutdown() {
    ClearStrings();
    if (s_batch) {
        GxuFontDestroyBatch(s_batch);
        s_batch = nullptr;
    }
    if (s_layer) {
        EventUnregister(EVENT_ID_KEYDOWN, OnKeyDown);
        EventUnregister(EVENT_ID_KEYDOWN_REPEATING, OnKeyDown);
        HandleClose(reinterpret_cast<HOBJECT>(s_layer));
        s_layer = nullptr;
    }
    s_font = nullptr;
    s_fontHeight = 0.0f;
}
