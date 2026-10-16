local function WowDecomp_ApplyUIFont(fontString, preferHighlight)
    if not fontString then
        return;
    end

    local fontGlobal;
    if preferHighlight and GameFontHighlightSmall then
        fontGlobal = GameFontHighlightSmall;
    elseif GameFontNormalSmall then
        fontGlobal = GameFontNormalSmall;
    elseif GameFontHighlightSmall then
        fontGlobal = GameFontHighlightSmall;
    end

    if fontGlobal then
        fontString:SetFontObject(fontGlobal);
        return;
    end

    local samples = {
        VideoOptionsEffectsPanelSubText,
        VideoOptionsEffectsPanelTitle,
    };
    local viewText = VideoOptionsEffectsPanelViewDistance and _G[VideoOptionsEffectsPanelViewDistance:GetName() .. "Text"];
    if viewText then
        tinsert(samples, viewText);
    end

    for _, sample in ipairs(samples) do
        if sample and sample.GetFont then
            local path, height, flags = sample:GetFont();
            if path and height and fontString:SetFont(path, height, flags) then
                return;
            end
        end
    end

    fontString:SetFont("Fonts\\FRIZQT__.TTF", 10);
end

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

    if WowDecomp_PlaytestCheckboxValue(VideoOptionsEffectsPanelExtendedHorizonZClip) then
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

local function WowDecomp_ReanchorShadersBelow(anchor)
    local shaders = VideoOptionsEffectsPanelShaders;
    if shaders and anchor then
        shaders:ClearAllPoints();
        shaders:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", 14, -10);
    end
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

    EffectsPanelOptions.extendedView = { text = "Extended view (up to 30k)" };
    EffectsPanelOptions.extendedHorizonZClip = { text = "Extended horizon Z clip" };
    EffectsPanelOptions.distanceFog = { text = "Distance fog" };
    EffectsPanelOptions.horizonFarclipScale = {
        text = "Horizon Z scale",
        minValue = 3,
        maxValue = 6,
        valueStep = 0.33,
    };

    local section = CreateFrame("Frame", "VideoOptionsEffectsPanelWowDecompSection", panel);
    section:SetSize(380, 92);
    section:SetPoint("TOPLEFT", shadowSlider, "BOTTOMLEFT", -2, -18);

    local header = section:CreateFontString(nil, "OVERLAY");
    WowDecomp_ApplyUIFont(header, true);
    header:SetPoint("TOPLEFT", section, "TOPLEFT", 4, -2);
    header:SetText("Map exploration (playtest)");

    WowDecomp_RegisterCheckbox(
        panel,
        "VideoOptionsEffectsPanelExtendedView",
        "Extended view distance",
        "extendedView",
        section,
        8,
        -22,
        WowDecomp_OnPlaytestCVarChanged
    );
    WowDecomp_RegisterCheckbox(
        panel,
        "VideoOptionsEffectsPanelExtendedHorizonZClip",
        "Extended horizon Z clip",
        "extendedHorizonZClip",
        section,
        8,
        -46,
        WowDecomp_OnPlaytestCVarChanged
    );
    WowDecomp_RegisterCheckbox(panel, "VideoOptionsEffectsPanelDistanceFog", "Distance fog", "distanceFog", section, 220, -22, nil);

    local horizonSlider = CreateFrame("Slider", "VideoOptionsEffectsPanelHorizonZScale", panel, "OptionsSliderTemplate");
    horizonSlider:SetSize(112, 17);
    horizonSlider:SetPoint("TOPLEFT", section, "TOPLEFT", 220, -50);
    horizonSlider.type = CONTROLTYPE_SLIDER;
    horizonSlider.cvar = "horizonFarclipScale";
    horizonSlider.SetDisplayValue = horizonSlider.SetValue;
    horizonSlider.SetValue = function(self, value)
        self:SetDisplayValue(value);
        BlizzardOptionsPanel_SetCVarSafe(self.cvar, value);
    end;

    local horizonText = _G[horizonSlider:GetName() .. "Text"];
    if horizonText then
        WowDecomp_ApplyUIFont(horizonText, false);
        horizonText:SetText("Horizon Z");
    end

    BlizzardOptionsPanel_RegisterControl(horizonSlider, panel);
    BlizzardOptionsPanel_SetupDependentControl(VideoOptionsEffectsPanelExtendedHorizonZClip, horizonSlider);
    horizonSlider:SetScript("OnValueChanged", VideoOptionsEffectsPanelSlider_OnValueChanged);
    WowDecomp_SetupRegisteredControl(horizonSlider, panel, EffectsPanelOptions.horizonFarclipScale);

    WowDecomp_ReanchorShadersBelow(section);
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
