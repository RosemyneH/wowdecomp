local function WowDecomp_PlaytestCheckboxValue(checkButton)
    if not checkButton then
        return false;
    end
    if checkButton.newValue ~= nil then
        return checkButton.newValue == "1";
    end
    return checkButton:GetChecked();
end

local function WowDecomp_ApplyPlaytestCVar(checkButton)
    if not checkButton or not checkButton.cvar then
        return;
    end
    local setting = checkButton:GetChecked() and "1" or "0";
    SetCVar(checkButton.cvar, setting);
    checkButton.value = setting;
    checkButton.newValue = nil;
end

local function WowDecomp_SetupRegisteredControl(control, panel, entry)
    if not control or not panel or not entry then
        return;
    end

    local name = control:GetName();
    if name and entry.text then
        local text = _G[name .. "Text"];
        if text then
            text:SetText(_G[entry.text] or entry.text);
        end
    end

    if control.type == CONTROLTYPE_SLIDER and entry.minValue and entry.maxValue then
        control:SetMinMaxValues(entry.minValue, entry.maxValue);
        if entry.valueStep then
            control:SetValueStep(entry.valueStep);
        end
    end

    BlizzardOptionsPanel_SetupControl(control);
    BlizzardOptionsPanel_RefreshControl(control);
end

local function WowDecomp_UpdateFarclipSlider()
    local slider = VideoOptionsEffectsPanelViewDistance;
    if not slider then
        return;
    end

    local minValue = BlizzardOptionsPanel_GetCVarMinSafe("farclip") or 177;
    local maxValue = BlizzardOptionsPanel_GetCVarMaxSafe("farclip") or 1277;
    local step = (maxValue - minValue) / 10;

    slider:SetMinMaxValues(minValue, maxValue);
    slider:SetValueStep(step);

    if EffectsPanelOptions and EffectsPanelOptions.farclip then
        EffectsPanelOptions.farclip.minValue = minValue;
        EffectsPanelOptions.farclip.maxValue = maxValue;
        EffectsPanelOptions.farclip.valueStep = step;
    end

    if WowDecomp_PlaytestCheckboxValue(VideoOptionsEffectsPanelFullView) then
        BlizzardOptionsPanel_Slider_Disable(slider);
    else
        BlizzardOptionsPanel_Slider_Enable(slider);
    end
end

local function WowDecomp_UpdateGroundRadiusSlider()
    local slider = VideoOptionsEffectsPanelClutterRadius;
    if not slider or not EffectsPanelOptions or not EffectsPanelOptions.groundEffectDist then
        return;
    end

    local minValue = BlizzardOptionsPanel_GetCVarMinSafe("groundEffectDist") or 70;
    local maxValue = BlizzardOptionsPanel_GetCVarMaxSafe("groundEffectDist") or 140;
    local step = (maxValue - minValue) / 7;

    slider:SetMinMaxValues(minValue, maxValue);
    slider:SetValueStep(step);
    EffectsPanelOptions.groundEffectDist.minValue = minValue;
    EffectsPanelOptions.groundEffectDist.maxValue = maxValue;
    EffectsPanelOptions.groundEffectDist.valueStep = step;
end

local function WowDecomp_OnPlaytestCVarChanged()
    WowDecomp_UpdateFarclipSlider();
    WowDecomp_UpdateGroundRadiusSlider();
end

local function WowDecomp_RegisterCheckbox(parent, name, label, cvar, anchor, x, y, refresh)
    local button = CreateFrame("CheckButton", name, parent, "VideoOptionsSmallCheckButtonTemplate");
    button:SetPoint("TOPLEFT", anchor, "TOPLEFT", x, y);
    button.type = CONTROLTYPE_CHECKBOX;
    button.cvar = cvar;

    local text = _G[button:GetName() .. "Text"];
    if text then
        text:SetText(label);
    end

    local entry = { text = label };
    EffectsPanelOptions[cvar] = entry;
    BlizzardOptionsPanel_RegisterControl(button, parent);
    WowDecomp_SetupRegisteredControl(button, parent, entry);

    button:SetScript("OnClick", function(self)
        if self:GetChecked() then
            PlaySound("igMainMenuOptionCheckBoxOn");
        else
            PlaySound("igMainMenuOptionCheckBoxOff");
        end
        BlizzardOptionsPanel_CheckButton_OnClick(self);
        WowDecomp_ApplyPlaytestCVar(self);
        if refresh then
            refresh();
        end
    end);

    return button;
end

local function WowDecomp_ShiftShadersDown()
    local shaders = VideoOptionsEffectsPanelShaders;
    if not shaders or shaders.wowDecompShifted then
        return;
    end

    local point, relativeTo, relativePoint, x, y = shaders:GetPoint(1);
    if not point then
        return;
    end

    shaders.wowDecompShifted = true;
    shaders:ClearAllPoints();
    shaders:SetPoint(point, relativeTo, relativePoint, x or 0, (y or 0) - 22);
end

local function WowDecomp_InstallEffectsPanelPlaytest(panel)
    if panel.wowDecompPlaytestInstalled then
        return;
    end

    local shadowSlider = VideoOptionsEffectsPanelShadowQuality;
    local viewDist = VideoOptionsEffectsPanelViewDistance;
    if not shadowSlider or not viewDist then
        return;
    end

    panel.wowDecompPlaytestInstalled = true;
    EffectsPanelOptions.extendedView = { text = "Full view" };

    local fullView = WowDecomp_RegisterCheckbox(
        panel,
        "VideoOptionsEffectsPanelFullView",
        "Full view",
        "extendedView",
        shadowSlider,
        0,
        0,
        WowDecomp_OnPlaytestCVarChanged
    );
    fullView:ClearAllPoints();
    fullView:SetPoint("TOPLEFT", shadowSlider, "BOTTOMLEFT", 0, -6);

    WowDecomp_ShiftShadersDown();
    WowDecomp_OnPlaytestCVarChanged();
end

if VideoOptionsEffectsPanel_OnLoad then
    local origEffectsOnLoad = VideoOptionsEffectsPanel_OnLoad;
    VideoOptionsEffectsPanel_OnLoad = function(self)
        origEffectsOnLoad(self);
        WowDecomp_InstallEffectsPanelPlaytest(self);
    end;
end

if VideoOptionsEffectsPanel then
    WowDecomp_InstallEffectsPanelPlaytest(VideoOptionsEffectsPanel);
end

if VideoOptionsFrame then
    VideoOptionsFrame:HookScript("OnShow", function()
        if VideoOptionsEffectsPanel and not VideoOptionsEffectsPanel.wowDecompPlaytestInstalled then
            WowDecomp_InstallEffectsPanelPlaytest(VideoOptionsEffectsPanel);
        end
        WowDecomp_OnPlaytestCVarChanged();
    end);
end
