local frame = CreateFrame("Frame", "WowdecompPing", UIParent)
frame:SetSize(88, 22)
frame:SetPoint("TOP", UIParent, "TOP", 0, -8)
frame:SetFrameStrata("TOOLTIP")
frame:EnableMouse(true)
frame:SetMovable(true)
frame:SetClampedToScreen(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetBackdrop({
    bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 },
})
frame:SetBackdropColor(0.04, 0.05, 0.06, 0.85)
frame:SetBackdropBorderColor(0.72, 0.55, 0.22, 1)

local label = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
label:SetPoint("CENTER")
label:SetText("ping --")

local elapsed = 0
frame:SetScript("OnUpdate", function(self, dt)
    elapsed = elapsed + dt
    if elapsed < 1 then return end
    elapsed = 0
    if not GetNetStats then
        label:SetText("ping --")
        return
    end
    local _, _, latency = GetNetStats()
    latency = latency or 0
    local color = latency <= 0 and "|cff9a9a9a" or latency < 80 and "|cff9dff9d" or latency < 160 and "|cffffe08a" or "|cffff8080"
    label:SetText(color .. (latency <= 0 and "ping --" or ("ping " .. latency .. " ms")) .. "|r")
end)

local function say(text)
    if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage("|cff8ec8ffWowdecomp|r " .. text) end
end

local function openSurface(spec, width, height)
    if not WaylandFrame_Open then
        say("this client has no Wayland frames")
        return
    end
    local opened, err = WaylandFrame_Open(spec, width or 960, height or 540)
    if not opened then
        say(err or "frame failed")
        return
    end
    local id = tonumber((opened:GetName() or ""):match("(%d+)$"))
    if id and WaylandFrame_Raise then WaylandFrame_Raise(id) end
    return opened
end

SLASH_WOWCHROME1 = "/chrome"
SLASH_WOWCHROME2 = "/frame"
SlashCmdList["WOWCHROME"] = function(msg)
    msg = msg and msg:gsub("^%s+", ""):gsub("%s+$", "") or ""
    if msg == "" or msg == "yt" or msg == "youtube" then
        openSurface("https://www.youtube.com", 960, 540)
        return
    end
    local url, w, h = msg:match("^(%S+)%s*(%d*)%s*(%d*)$")
    if not url then
        say("/chrome [url] [width] [height]")
        return
    end
    if url:sub(1, 4) ~= "http" then url = "https://" .. url end
    openSurface(url, tonumber(w), tonumber(h))
end

SLASH_WOWAPP1 = "/app"
SlashCmdList["WOWAPP"] = function(msg)
    msg = msg and msg:gsub("^%s+", ""):gsub("%s+$", "") or ""
    if msg == "" then
        say("/app foot    or    /app /usr/bin/chromium --app=https://example.com")
        return
    end
    local spec, w, h = msg, nil, nil
    local body, width, height = msg:match("^(.-)%s+(%d+)%s+(%d+)$")
    if body then spec, w, h = body, tonumber(width), tonumber(height) end
    openSurface("app:" .. spec, w, h)
end

SLASH_WOWPING1 = "/ping"
SlashCmdList["WOWPING"] = function()
    if frame:IsShown() then frame:Hide() else frame:Show() end
    if GetNetStats then
        local inbound, outbound, latency = GetNetStats()
        say(string.format("ping %d ms   in %.1f KB/s   out %.1f KB/s", latency or 0, inbound or 0, outbound or 0))
    end
end

local bangs = {
    yt = "https://www.youtube.com/results?search_query=%s",
    g = "https://www.google.com/search?q=%s",
    ddg = "https://duckduckgo.com/?q=%s",
    w = "https://en.wikipedia.org/wiki/Special:Search?search=%s",
    gh = "https://github.com/search?q=%s",
    wh = "https://www.wowhead.com/wotlk/search?q=%s",
}

local function encode(text)
    return (text:gsub("[^%w%-%._~]", function(c) return string.format("%%%02X", c:byte()) end))
end

local function usage()
    WowdecompDeskDB = WowdecompDeskDB or {}
    WowdecompDeskDB.usage = WowdecompDeskDB.usage or {}
    return WowdecompDeskDB.usage
end

-- Exact, prefix, word start, substring, then letters in order.
local function score(name, query)
    if query == "" then return 1 end
    name = name:lower()
    if name == query then return 1000 end
    if name:sub(1, #query) == query then return 800 end
    local at = name:find(query, 1, true)
    if at then
        local before = name:sub(at - 1, at - 1)
        return (before == " " or before == "-" or before == "_" or before == ".") and 600 or 400
    end
    local pos, gaps = 0, 0
    for i = 1, #query do
        local found = name:find(query:sub(i, i), pos + 1, true)
        if not found then return nil end
        if pos > 0 then gaps = gaps + (found - pos - 1) end
        pos = found
    end
    return math.max(200 - gaps * 5, 50)
end

local ROWS = 9
local ROW_H = 22
local launcher = CreateFrame("Frame", "WowdecompLauncher", UIParent)
launcher:SetSize(480, 40 + ROWS * ROW_H)
launcher:SetPoint("TOP", UIParent, "TOP", 0, -140)
launcher:SetFrameStrata("DIALOG")
launcher:SetBackdrop({
    bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12,
    insets = { left = 3, right = 3, top = 3, bottom = 3 },
})
launcher:SetBackdropColor(0.04, 0.05, 0.06, 0.95)
launcher:SetBackdropBorderColor(0.72, 0.55, 0.22, 1)
launcher:EnableMouse(true)
launcher:EnableMouseWheel(true)
launcher:Hide()
tinsert(UISpecialFrames, "WowdecompLauncher")

local input = CreateFrame("EditBox", "WowdecompLauncherInput", launcher)
input:SetPoint("TOPLEFT", 10, -8)
input:SetPoint("TOPRIGHT", -10, -8)
input:SetHeight(24)
input:SetFontObject(ChatFontNormal)
input:SetAutoFocus(false)
input:SetTextInsets(6, 6, 0, 0)
local inputBg = input:CreateTexture(nil, "BACKGROUND")
inputBg:SetAllPoints()
inputBg:SetTexture(1, 1, 1, 0.06)

local rows = {}
local results = {}
local selected = 1
local offset = 0

local function launch(entry)
    if not entry then return end
    launcher:Hide()
    if entry.key then
        local counts = usage()
        counts[entry.key] = (counts[entry.key] or 0) + 1
    end
    if entry.kind == "frame" then
        if not WaylandFrame_Raise(entry.id) then say("that window is gone") end
    elseif entry.kind == "url" then
        openSurface(entry.target, 960, 540)
    else
        openSurface("app:" .. entry.target, 960, 540)
    end
end

local function paint()
    if selected > #results then selected = #results end
    if selected < 1 then selected = 1 end
    if selected <= offset then offset = selected - 1 end
    if selected > offset + ROWS then offset = selected - ROWS end
    for i = 1, ROWS do
        local row = rows[i]
        local entry = results[offset + i]
        if entry then
            row.entry = entry
            row.name:SetText(entry.label)
            row.detail:SetText(entry.detail or "")
            if offset + i == selected then row.selected:Show() else row.selected:Hide() end
            row:Show()
        else
            row.entry = nil
            row:Hide()
        end
    end
end

local function refresh()
    local query = (input:GetText() or ""):gsub("^%s+", ""):gsub("%s+$", "")
    local lower = query:lower()
    local counts = usage()
    results = {}

    local bang, rest = query:match("^!(%w+)%s+(.+)$")
    if bang and bangs[bang:lower()] then
        local url = bangs[bang:lower()]:format(encode(rest))
        tinsert(results, { kind = "url", label = "!" .. bang:lower() .. " " .. rest, detail = "search", target = url, rank = 5000 })
    end
    if query:match("^https?://%S+$") or query:match("^[%w%-]+%.[%w%-%.]+[%S]*$") then
        local url = query:match("^https?://") and query or ("https://" .. query)
        tinsert(results, { kind = "url", label = url, detail = "open page", target = url, rank = 4000 })
    end
    local raw = query:match("^>%s*(.+)$")
    if raw then
        tinsert(results, { kind = "app", label = raw, detail = "run command", target = raw, rank = 4500 })
    end

    if WaylandFrame_List then
        for _, live in ipairs(WaylandFrame_List()) do
            local title = live.title ~= "" and live.title or ("window " .. live.id)
            local s = score(title, lower)
            if s then tinsert(results, { kind = "frame", id = live.id, label = title, detail = "open window", rank = s + 3000 }) end
        end
    end

    if not raw and WaylandFrame_Apps then
        for _, app in ipairs(WaylandFrame_Apps()) do
            local s = score(app.name, lower)
            if not s and lower ~= "" then
                s = score(app.exec, lower)
                s = s and s / 2
            end
            if s then
                local key = "app:" .. app.id
                local used = counts[key] or 0
                tinsert(results, { kind = "app", key = key, label = app.name, detail = app.comment, target = app.exec, rank = s + math.min(used, 50) * 20 })
            end
        end
    end

    if query ~= "" and not raw and #results == 0 then
        tinsert(results, { kind = "app", label = query, detail = "run command", target = query, rank = 0 })
    end

    table.sort(results, function(a, b)
        if a.rank ~= b.rank then return a.rank > b.rank end
        return a.label:lower() < b.label:lower()
    end)
    selected, offset = 1, 0
    paint()
end

local function move(delta)
    if #results == 0 then return end
    selected = selected + delta
    if selected < 1 then selected = #results end
    if selected > #results then selected = 1 end
    paint()
end

for i = 1, ROWS do
    local row = CreateFrame("Button", nil, launcher)
    row:SetHeight(ROW_H)
    row:SetPoint("TOPLEFT", 8, -36 - (i - 1) * ROW_H)
    row:SetPoint("TOPRIGHT", -8, -36 - (i - 1) * ROW_H)
    row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    row.selected = row:CreateTexture(nil, "BACKGROUND")
    row.selected:SetAllPoints()
    row.selected:SetTexture(0.72, 0.55, 0.22, 0.25)
    row.name = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    row.name:SetPoint("LEFT", 6, 0)
    row.name:SetJustifyH("LEFT")
    row.detail = row:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    row.detail:SetPoint("RIGHT", -6, 0)
    row.detail:SetPoint("LEFT", row.name, "RIGHT", 12, 0)
    row.detail:SetJustifyH("RIGHT")
    row:SetScript("OnClick", function(self) launch(self.entry) end)
    rows[i] = row
end

input:SetScript("OnTextChanged", refresh)
input:SetScript("OnEnterPressed", function() launch(results[selected]) end)
input:SetScript("OnEscapePressed", function() launcher:Hide() end)
input:SetScript("OnTabPressed", function() move(IsShiftKeyDown() and -1 or 1) end)
launcher:SetScript("OnMouseWheel", function(_, delta) move(-delta) end)
launcher:SetScript("OnShow", function()
    input:SetText("")
    input:SetFocus()
    refresh()
end)
launcher:SetScript("OnHide", function() input:ClearFocus() end)

local function toggleLauncher(msg)
    if msg and msg:match("^%s*rescan%s*$") then
        if WaylandFrame_Apps then
            say(#WaylandFrame_Apps(true) .. " apps found")
        end
        return
    end
    if launcher:IsShown() then launcher:Hide() else launcher:Show() end
end

SLASH_WOWLAUNCH1 = "/launch"
SLASH_WOWLAUNCH2 = "/apps"
SLASH_WOWLAUNCH3 = "/br"
SlashCmdList["WOWLAUNCH"] = toggleLauncher

frame:RegisterEvent("PLAYER_LOGIN")
frame:SetScript("OnEvent", function()
    say("/launch finds apps and open windows. /chrome [url] opens a page. /app <program> runs a Wayland client. /ping toggles this readout.")
end)
