local stockHooked

function WowdecompProfile_AddLines(tip)
    if not tip or not GetProfilerZones then
        return
    end

    local frameMs, zones = GetProfilerZones()

    if not frameMs or not zones then
        return
    end

    tip:AddLine(" ")
    tip:AddDoubleLine("Frame", format("%.1f ms", frameMs), 1, 0.82, 0, 1, 1, 1)

    table.sort(zones, function(a, b)
        return a.selfMs > b.selfMs
    end)

    for i = 1, #zones do
        local zone = zones[i]

        if zone.calls > 0 then
            tip:AddDoubleLine(zone.name, format("%.1f ms", zone.selfMs), 0.9, 0.9, 0.9, 1, 1, 1)
        end
    end
end

local function HookStock()
    if stockHooked or type(MainMenuBarPerformanceBarFrame_OnEnter) ~= "function" then
        return
    end

    stockHooked = true
    hooksecurefunc("MainMenuBarPerformanceBarFrame_OnEnter", function()
        WowdecompProfile_AddLines(GameTooltip)
        GameTooltip:Show()
    end)
end

local frame = CreateFrame("Frame")
frame:RegisterEvent("PLAYER_LOGIN")
frame:SetScript("OnEvent", HookStock)
HookStock()
