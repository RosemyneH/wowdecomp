#include "VideoOptions.h"
#include "WowDecompVideoOptionsLua.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "WoW/Source/Glue/CWOWClientStatus.h"

void CGVideoOptions::InstallExtendedViewUiPatch() {
    CWOWClientStatus status("Logs\\FrameXML.log");

    if (SFile::FileExistsEx("Interface\\FrameXML\\WowDecompVideoOptions.lua", 1)) {
        FrameScript_ExecuteFile("Interface\\FrameXML\\WowDecompVideoOptions.lua", nullptr, nullptr, &status);
    } else {
        FrameScript_Execute(kWowDecompVideoOptionsLua, "Interface\\FrameXML\\WowDecompVideoOptions.lua", nullptr);
    }

    static const char* kAddonList = R"lua(
if WowDecompAddonListInstalled then return end
WowDecompAddonListInstalled = true
local function show(index)
    local parent = AddonList or UIParent or GlueParent
    if not parent or not GetAddOnInfo then return end
    local name, title, notes = GetAddOnInfo(index)
    if not notes or not string.find(notes, "WASM addon", 1, true) then
        if WowDecompAddonNote then WowDecompAddonNote:Hide() end
        return
    end
    if not WowDecompAddonNote then
        local panel = CreateFrame("Frame", "WowDecompAddonNote", parent)
        panel:SetWidth(268)
        panel:SetHeight(280)
        if AddonList then panel:SetPoint("TOPLEFT", AddonList, "TOPRIGHT", 8, -12) else panel:SetPoint("CENTER", 220, 0) end
        panel:SetBackdrop({
            bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            tile = true, tileSize = 16, edgeSize = 12,
            insets = { left = 3, right = 3, top = 3, bottom = 3 }
        })
        panel:SetBackdropColor(0.05, 0.05, 0.07, 0.94)
        panel:SetBackdropBorderColor(0.55, 0.42, 0.18, 1)
        panel.title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        panel.title:SetPoint("TOPLEFT", 14, -12)
        panel.title:SetWidth(240)
        panel.title:SetJustifyH("LEFT")
        panel.body = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        panel.body:SetPoint("TOPLEFT", 14, -34)
        panel.body:SetWidth(240)
        panel.body:SetJustifyH("LEFT")
        panel.body:SetJustifyV("TOP")
    end
    WowDecompAddonNote.title:SetText(title or name or "")
    WowDecompAddonNote.body:SetText(notes)
    WowDecompAddonNote:Show()
end
local function hookEntries()
    for i = 1, 40 do
        local entry = _G["AddonListEntry" .. i]
        if entry and not entry.wowDecompNote then
            entry.wowDecompNote = true
            entry:HookScript("OnEnter", function(self) show(self:GetID()) end)
        end
    end
end
if type(AddonList_Update) == "function" and not WowDecompAddonListWrapped then
    WowDecompAddonListWrapped = true
    local orig = AddonList_Update
    AddonList_Update = function(...)
        orig(...)
        hookEntries()
    end
end
hookEntries()
)lua";
    FrameScript_Execute(kAddonList, "Interface\\FrameXML\\WowDecompAddonList.lua", nullptr);
}
