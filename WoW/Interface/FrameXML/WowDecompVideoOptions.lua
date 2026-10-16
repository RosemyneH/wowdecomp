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

local function WowDecomp_UpdateHorizonSlider()
    local slider = VideoOptionsEffectsPanelHorizonZScale;
    if not slider or not EffectsPanelOptions or not EffectsPanelOptions.horizonFarclipScale then
        return;
    end

    local minValue = BlizzardOptionsPanel_GetCVarMinSafe("horizonFarclipScale") or 3;
    local maxValue = BlizzardOptionsPanel_GetCVarMaxSafe("horizonFarclipScale") or 6;
    local step = (maxValue - minValue) / 9;

    slider:SetMinMaxValues(minValue, maxValue);
    slider:SetValueStep(step);
    EffectsPanelOptions.horizonFarclipScale.minValue = minValue;
    EffectsPanelOptions.horizonFarclipScale.maxValue = maxValue;
    EffectsPanelOptions.horizonFarclipScale.valueStep = step;

    if VideoOptionsEffectsPanelExtendedHorizonZClip and VideoOptionsEffectsPanelExtendedHorizonZClip:GetChecked() then
        BlizzardOptionsPanel_Slider_Enable(slider);
    else
        BlizzardOptionsPanel_Slider_Disable(slider);
    end
end

local function WowDecomp_OnPlaytestCVarChanged()
    WowDecomp_UpdateFarclipSlider();
    WowDecomp_UpdateGroundRadiusSlider();
    WowDecomp_UpdateHorizonSlider();
end

local function WowDecomp_RegisterCheckbox(parent, name, label, cvar, anchor, x, y, refresh)
    local button = CreateFrame("CheckButton", name, parent, "VideoOptionsSmallCheckButtonTemplate");
    button:SetPoint("TOPLEFT", anchor, "TOPLEFT", x, y);
    button.type = CONTROLTYPE_CHECKBOX;
    button.cvar = cvar;
    button.setFunc = refresh;

    local text = _G[button:GetName() .. "Text"];
    if text then
        text:SetText(label);
    end

    EffectsPanelOptions[cvar] = EffectsPanelOptions[cvar] or { text = label };
    BlizzardOptionsPanel_RegisterControl(button, parent);

    button:SetScript("OnClick", function(self)
        if self:GetChecked() then
            PlaySound("igMainMenuOptionCheckBoxOn");
        else
            PlaySound("igMainMenuOptionCheckBoxOff");
        end
        BlizzardOptionsPanel_CheckButton_OnClick(self);
        if refresh then
            refresh();
        end
        VideoOptionsFrameApply:Enable();
    end);

    return button;
end

local function WowDecomp_InstallEffectsPanelPlaytest(panel)
    if panel.wowDecompPlaytestInstalled then
        return;
    end
    panel.wowDecompPlaytestInstalled = true;

    EffectsPanelOptions.extendedView = { text = "Extended view (up to 10k)" };
    EffectsPanelOptions.extendedHorizonZClip = { text = "Extended horizon Z clip" };
    EffectsPanelOptions.distanceFog = { text = "Distance fog" };
    EffectsPanelOptions.horizonFarclipScale = {
        text = "Horizon Z scale",
        minValue = 3,
        maxValue = 6,
        valueStep = 0.33,
    };

    local viewDist = VideoOptionsEffectsPanelViewDistance;
    if not viewDist then
        return;
    end

    local section = CreateFrame("Frame", "VideoOptionsEffectsPanelWowDecompSection", panel);
    section:SetSize(380, 96);
    section:SetPoint("TOPLEFT", viewDist, "BOTTOMLEFT", -2, -18);

    local header = section:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall");
    header:SetPoint("TOPLEFT", section, "TOPLEFT", 4, -2);
    header:SetText("Map exploration (playtest)");
    if GameFontHighlightSmall then
        header:SetFontObject(GameFontHighlightSmall);
    end

    local anchor = section;
    WowDecomp_RegisterCheckbox(panel, "VideoOptionsEffectsPanelExtendedView", "Extended view distance", "extendedView", anchor, 8, -22, WowDecomp_OnPlaytestCVarChanged);
    WowDecomp_RegisterCheckbox(
        panel,
        "VideoOptionsEffectsPanelExtendedHorizonZClip",
        "Extended horizon Z clip",
        "extendedHorizonZClip",
        anchor,
        8,
        -46,
        WowDecomp_OnPlaytestCVarChanged
    );
    WowDecomp_RegisterCheckbox(panel, "VideoOptionsEffectsPanelDistanceFog", "Distance fog", "distanceFog", anchor, 220, -22, nil);

    local horizonSlider = CreateFrame("Slider", "VideoOptionsEffectsPanelHorizonZScale", panel, "OptionsSliderTemplate");
    horizonSlider:SetSize(112, 17);
    horizonSlider:SetPoint("TOPLEFT", anchor, "TOPLEFT", 220, -50);
    horizonSlider.type = CONTROLTYPE_SLIDER;
    horizonSlider.cvar = "horizonFarclipScale";
    horizonSlider.SetDisplayValue = horizonSlider.SetValue;
    horizonSlider.SetValue = function(self, value)
        self:SetDisplayValue(value);
        BlizzardOptionsPanel_SetCVarSafe(self.cvar, value);
    end;
    _G[horizonSlider:GetName() .. "Text"]:SetFontObject("GameFontNormalSmall");
    _G[horizonSlider:GetName() .. "Text"]:SetText("Horizon Z");
    BlizzardOptionsPanel_RegisterControl(horizonSlider, panel);
    BlizzardOptionsPanel_SetupDependentControl(VideoOptionsEffectsPanelExtendedHorizonZClip, horizonSlider);
    horizonSlider:SetScript("OnValueChanged", VideoOptionsEffectsPanelSlider_OnValueChanged);
end

if VideoOptionsEffectsPanel_OnLoad then
    local origEffectsOnLoad = VideoOptionsEffectsPanel_OnLoad;
    VideoOptionsEffectsPanel_OnLoad = function(self)
        origEffectsOnLoad(self);
        WowDecomp_InstallEffectsPanelPlaytest(self);
    end;
end

if VideoOptionsEffectsPanel_OnEvent then
    local origEffectsOnEvent = VideoOptionsEffectsPanel_OnEvent;
    VideoOptionsEffectsPanel_OnEvent = function(self, event, ...)
        origEffectsOnEvent(self, event, ...);
        if event == "PLAYER_ENTERING_WORLD" then
            WowDecomp_OnPlaytestCVarChanged();
        end
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
