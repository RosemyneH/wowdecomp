-- ʕ •ᴥ•ʔ✿ Read-only inspector panel; permission changes happen through the developer console ✿ ʕ •ᴥ•ʔ
local frame = CreateFrame("Frame", "WasmInspectorFrame", UIParent)
frame:SetSize(560, 380)
frame:SetPoint("CENTER")
frame:SetMovable(true)
frame:SetClampedToScreen(true)
tinsert(UISpecialFrames, "WasmInspectorFrame")
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetBackdrop({
    bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
    tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 8, right = 8, top = 8, bottom = 8 },
})
frame:Hide()

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -16)
title:SetText("WASM Addon Inspector")

local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -4, -4)

local scroll = CreateFrame("ScrollingMessageFrame", nil, frame)
scroll:SetPoint("TOPLEFT", 16, -44)
scroll:SetPoint("BOTTOMRIGHT", -16, 44)
scroll:SetFontObject(GameFontNormalSmall)
scroll:SetJustifyH("LEFT")
scroll:SetMaxLines(512)
scroll:SetFading(false)
scroll:EnableMouseWheel(true)
scroll:SetScript("OnMouseWheel", function(self, delta)
    if delta > 0 then self:ScrollUp() else self:ScrollDown() end
end)

local function Refresh()
    scroll:Clear()
    if not WasmAddonInspectorText then
        scroll:AddMessage("WASM runtime bindings unavailable.")
        return
    end
    if WasmAddon_GetNumAddOns then
        scroll:AddMessage(("Structured bindings: %d WASM addon(s) known."):format(WasmAddon_GetNumAddOns()))
    end
    for line in (WasmAddonInspectorText() .. "\n"):gmatch("(.-)\n") do
        scroll:AddMessage(line)
    end
    scroll:AddMessage("Grant/revoke (only with -console, in the developer console): wasmgrant <id> <permission> / wasmdeny <id> <permission>")
end

local refresh = CreateFrame("Button", nil, frame, "UIPanelButtonTemplate")
refresh:SetSize(90, 22)
refresh:SetPoint("BOTTOMLEFT", 16, 12)
refresh:SetText("Refresh")
refresh:SetScript("OnClick", Refresh)

frame:SetScript("OnShow", Refresh)
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:SetScript("OnEvent", function(self, event)
    if event == "PLAYER_ENTERING_WORLD" and self:IsShown() then Refresh() end
end)

SLASH_WASMINSPECTOR1 = "/wasm"
SlashCmdList["WASMINSPECTOR"] = function()
    if frame:IsShown() then frame:Hide() else frame:Show() end
end
