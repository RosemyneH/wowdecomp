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
    if not opened then say(err or "frame failed") end
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

frame:RegisterEvent("PLAYER_LOGIN")
frame:SetScript("OnEvent", function()
    say("/chrome [url] opens a page in a frame. /app <program> runs a Wayland client. /ping toggles this readout.")
end)
