#pragma once

static const char* kWowDecompAddonPanelLua = R"lua(
if WowDecompAddonPanelInstalled then return end
WowDecompAddonPanelInstalled = true

local glue = GlueParent and not UIParent
local parent = UIParent or GlueParent
if not parent or not GetNumAddOns then return end

local function button(parentFrame, text, width)
    local frame = CreateFrame("Button", nil, parentFrame)
    frame:SetWidth(width)
    frame:SetHeight(20)
    frame:SetBackdrop({
        bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 8, edgeSize = 8,
        insets = { left = 2, right = 2, top = 2, bottom = 2 }
    })
    frame:SetBackdropColor(0.14, 0.11, 0.08, 1)
    frame:SetBackdropBorderColor(0.72, 0.55, 0.22, 1)
    local label = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    label:SetPoint("CENTER")
    label:SetText(text)
    frame.label = label
    return frame
end

local panel = CreateFrame("Frame", "WowdecompAddonPanel", parent)
panel:SetWidth(460)
panel:SetHeight(460)
panel:SetPoint("CENTER", parent, "CENTER", 0, 0)
panel:SetFrameStrata("DIALOG")
panel:SetBackdrop({
    bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 }
})
panel:SetBackdropColor(0.05, 0.05, 0.07, 0.96)
panel:SetBackdropBorderColor(0.72, 0.55, 0.22, 1)
panel:EnableMouse(true)
panel:SetMovable(true)
panel:SetClampedToScreen(true)
panel:RegisterForDrag("LeftButton")
panel:SetScript("OnDragStart", function(self) self:StartMoving() end)
panel:SetScript("OnDragStop", function(self) self:StopMovingOrSizing() end)
panel:Hide()
if UISpecialFrames then tinsert(UISpecialFrames, "WowdecompAddonPanel") end

local title = panel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOPLEFT", 16, -12)
title:SetText("AddOns")
local who = panel:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
who:SetPoint("TOPLEFT", 16, -30)
who:SetWidth(280)
who:SetJustifyH("LEFT")
local note = panel:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
note:SetPoint("BOTTOMLEFT", 16, 12)
note:SetWidth(300)
note:SetJustifyH("LEFT")
note:SetText(glue and "Saved for the selected character. It loads on enter world." or "Reload re-runs that addon. Disable finishes on Reload UI.")

local search = CreateFrame("EditBox", nil, panel)
search:SetWidth(160)
search:SetHeight(20)
search:SetPoint("TOPRIGHT", -16, -12)
search:SetAutoFocus(false)
search:SetFontObject(GameFontHighlightSmall)
search:SetBackdrop({
    bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 8, edgeSize = 8,
    insets = { left = 3, right = 3, top = 3, bottom = 3 }
})
search:SetBackdropColor(0.02, 0.02, 0.03, 1)
search:SetBackdropBorderColor(0.45, 0.36, 0.16, 1)
search:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
search:SetTextInsets(6, 6, 2, 2)

local scroll = CreateFrame("ScrollFrame", nil, panel)
scroll:SetPoint("TOPLEFT", 12, -56)
scroll:SetPoint("BOTTOMRIGHT", -12, 40)
local child = CreateFrame("Frame", nil, scroll)
child:SetWidth(420)
child:SetHeight(1)
scroll:SetScrollChild(child)
scroll:EnableMouseWheel(true)
scroll:SetScript("OnMouseWheel", function(self, delta)
    local current = self:GetVerticalScroll() or 0
    local limit = math.max(0, child:GetHeight() - self:GetHeight())
    local nextScroll = current - delta * 28
    if nextScroll < 0 then nextScroll = 0 end
    if nextScroll > limit then nextScroll = limit end
    self:SetVerticalScroll(nextScroll)
end)

local rows = {}

local function CharacterName()
    if not GetCharacterInfo then return nil end
    local index = 1
    if CharacterSelect and CharacterSelect.selectedIndex and CharacterSelect.selectedIndex > 0 then
        index = CharacterSelect.selectedIndex
    end
    return GetCharacterInfo(index)
end

local function IsOn(index)
    if glue then
        local name = CharacterName()
        if not name or not GetAddOnEnableState then return false end
        local state = GetAddOnEnableState(name, index)
        return state and state > 0
    end
    local _, _, _, enabled = GetAddOnInfo(index)
    return enabled and true or false
end

local function Save()
    if SaveAddOns then SaveAddOns() end
end

local function SetOn(index, on)
    if glue then
        local name = CharacterName()
        if not name then return end
        if on then EnableAddOn(name, index) else DisableAddOn(name, index) end
        Save()
        return
    end
    if on then
        EnableAddOn(index)
        Save()
        if IsAddOnLoaded and not IsAddOnLoaded(index) and LoadAddOn then LoadAddOn(index) end
        if LoadWasmPackage then LoadWasmPackage(index) end
    else
        DisableAddOn(index)
        Save()
        if UnloadWasmPackage then UnloadWasmPackage(index) end
    end
end

local function ReloadOne(index)
    if glue or not ReloadAddOn then
        note:SetText("Saved. This character loads it on enter world.")
        return
    end
    if not IsOn(index) then
        note:SetText("Turn it on first. A disabled addon stays out until you enable it.")
        return
    end
    local ok, err = ReloadAddOn(index)
    if ok then
        note:SetText("Reloaded. Frames it already built stay until Reload UI.")
    else
        note:SetText(err or "Could not reload.")
    end
end

local function Layout()
    local query = string.lower(search:GetText() or "")
    local y = 0
    for i = 1, #rows do
        local row = rows[i]
        local match = query == "" or string.find(row.key, query, 1, true) or (row.titleKey and string.find(row.titleKey, query, 1, true))
        if match then
            row:ClearAllPoints()
            row:SetPoint("TOPLEFT", child, "TOPLEFT", 0, -y)
            row:Show()
            y = y + 28
        else
            row:Hide()
        end
    end
    child:SetHeight(y > 0 and y or 1)
end

local function Refresh()
    local count = GetNumAddOns()
    for i = 1, count do
        local row = rows[i]
        if not row then
            row = CreateFrame("Frame", nil, child)
            row:SetWidth(420)
            row:SetHeight(26)
            local box = CreateFrame("CheckButton", nil, row)
            box:SetWidth(18)
            box:SetHeight(18)
            box:SetPoint("LEFT", 2, 0)
            box:SetBackdrop({
                bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
                edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
                tile = true, tileSize = 8, edgeSize = 8,
                insets = { left = 2, right = 2, top = 2, bottom = 2 }
            })
            box:SetBackdropColor(0.08, 0.08, 0.08, 1)
            box:SetBackdropBorderColor(0.55, 0.42, 0.18, 1)
            local mark = box:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
            mark:SetPoint("CENTER", 0, 1)
            box.mark = mark
            box:SetScript("OnClick", function(self)
                local on = self:GetChecked()
                self.mark:SetText(on and "x" or "")
                SetOn(self:GetParent().index, on and true or false)
                Refresh()
            end)
            local nameText = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
            nameText:SetPoint("LEFT", box, "RIGHT", 8, 0)
            nameText:SetWidth(230)
            nameText:SetJustifyH("LEFT")
            local stateText = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
            stateText:SetPoint("LEFT", nameText, "RIGHT", 6, 0)
            stateText:SetWidth(70)
            stateText:SetJustifyH("LEFT")
            local reload = button(row, "Reload", 64)
            reload:SetPoint("RIGHT", -4, 0)
            reload:SetScript("OnClick", function(self) ReloadOne(self:GetParent().index) end)
            row.box = box
            row.nameText = nameText
            row.stateText = stateText
            rows[i] = row
        end
        local name, title = GetAddOnInfo(i)
        row.index = i
        row.key = string.lower(name or "")
        row.titleKey = string.lower(title or "")
        row.nameText:SetText(title or name or "")
        local on = IsOn(i)
        row.box:SetChecked(on)
        row.box.mark:SetText(on and "x" or "")
        local loaded = (not glue) and IsAddOnLoaded and IsAddOnLoaded(i)
        if loaded and on then row.stateText:SetText("|cff9dff9dloaded|r")
        elseif on then row.stateText:SetText("|cff9ec8ffon|r")
        else row.stateText:SetText("|cff9a9a9aoff|r") end
    end
    for i = count + 1, #rows do rows[i]:Hide() end
    Layout()
    if glue then
        local name = CharacterName()
        who:SetText(name and ("Character: " .. name) or "Select a character")
        panel.character = name
    else
        local name = UnitName and UnitName("player")
        who:SetText(name and ("Character: " .. name) or "This character")
    end
end

search:SetScript("OnTextChanged", function() Layout() end)

local enableAll = button(panel, "Enable all", 78)
enableAll:SetPoint("BOTTOMLEFT", 16, 32)
enableAll:SetScript("OnClick", function()
    if glue then
        local name = CharacterName()
        if name and EnableAllAddOns then EnableAllAddOns(name) end
    elseif EnableAllAddOns then
        EnableAllAddOns()
    end
    Save()
    Refresh()
end)
local disableAll = button(panel, "Disable all", 78)
disableAll:SetPoint("LEFT", enableAll, "RIGHT", 6, 0)
disableAll:SetScript("OnClick", function()
    if glue then
        local name = CharacterName()
        if name and DisableAllAddOns then DisableAllAddOns(name) end
    elseif DisableAllAddOns then
        DisableAllAddOns()
    end
    Save()
    Refresh()
end)
local reloadUi = button(panel, "Reload UI", 78)
reloadUi:SetPoint("LEFT", disableAll, "RIGHT", 6, 0)
if glue then reloadUi:Hide() end
reloadUi:SetScript("OnClick", function()
    Save()
    if ReloadUI then ReloadUI() end
end)
local close = button(panel, "Close", 64)
close:SetPoint("BOTTOMRIGHT", -16, 32)
close:SetScript("OnClick", function() panel:Hide() end)

panel:SetScript("OnShow", Refresh)
if glue then
    panel:SetScript("OnUpdate", function(self)
        if not self:IsShown() then return end
        local name = CharacterName()
        if name ~= self.character then Refresh() end
    end)
end

local launcher = button(parent, "AddOns", 72)
launcher:SetFrameStrata("HIGH")
if CharSelectEnterWorldButton then
    launcher:SetPoint("BOTTOMLEFT", CharSelectEnterWorldButton, "TOPLEFT", 0, 6)
else
    launcher:SetPoint("TOPLEFT", parent, "TOPLEFT", 18, -18)
end
launcher:SetScript("OnClick", function()
    if panel:IsShown() then panel:Hide() else panel:Show() end
end)

if SlashCmdList then
    SLASH_WOWADDONS1 = "/addons"
    SlashCmdList["WOWADDONS"] = function()
        if panel:IsShown() then panel:Hide() else panel:Show() end
    end
end
)lua";
