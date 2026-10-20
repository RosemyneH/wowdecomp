#include "VideoOptions.h"
#include "WowDecompAddonPanelLua.h"
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
    if WowDecompAddonNote and not WowDecompAddonNote.title then
        WowDecompAddonNote:Hide()
        WowDecompAddonNote = nil
    end
    if not WowDecompAddonNote then
        local panel = CreateFrame("Frame", "WowDecompAddonNote", parent)
        panel:SetWidth(268)
        panel:SetHeight(280)
        local anchor = AddonListBackground or AddonList
        if anchor then panel:SetPoint("TOPLEFT", anchor, "TOPRIGHT", 8, -48) else panel:SetPoint("CENTER", 220, 0) end
        panel:SetBackdrop({
            bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
            edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            tile = true, tileSize = 16, edgeSize = 12,
            insets = { left = 3, right = 3, top = 3, bottom = 3 }
        })
        panel:SetBackdropColor(0.05, 0.05, 0.07, 0.94)
        panel:SetBackdropBorderColor(0.55, 0.42, 0.18, 1)
        local function label(kind)
            local text = panel:CreateFontString(nil, "OVERLAY")
            local font = kind == "title" and (GlueFontNormal or GameFontNormal or GlueFontNormalSmall) or (GlueFontHighlightSmall or GameFontHighlightSmall or GlueFontNormalSmall)
            if font then text:SetFontObject(font) else text:SetFont("Fonts\\FRIZQT__.TTF", kind == "title" and 12 or 10) end
            return text
        end
        panel.title = label("title")
        panel.title:SetPoint("TOPLEFT", 14, -12)
        panel.title:SetWidth(240)
        panel.title:SetJustifyH("LEFT")
        panel.body = label("body")
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
if AddonList and AddonList.HookScript then
    AddonList:HookScript("OnHide", function()
        if WowDecompAddonNote then WowDecompAddonNote:Hide() end
    end)
end
)lua";
    FrameScript_Execute(kAddonList, "Interface\\FrameXML\\WowDecompAddonList.lua", nullptr);
    FrameScript_Execute(kWowDecompAddonPanelLua, "Interface\\FrameXML\\WowDecompAddonPanel.lua", nullptr);
}
