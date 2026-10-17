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

    if entry.tooltip then
        control.tooltipText = entry.tooltip;
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

    if WowDecomp_PlaytestCheckboxValue(VideoOptionsAdvancedPanelFullView) then
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

local function WowDecomp_RefreshShaderStatus()
    local status = _G["VideoOptionsAdvancedPanelShaderStatus"];
    if not status then
        return;
    end
    if GetVulkanShaderStatus then
        status:SetText(GetVulkanShaderStatus());
    end
end

local function WowDecomp_OnPlaytestCVarChanged()
    WowDecomp_UpdateFarclipSlider();
    WowDecomp_UpdateGroundRadiusSlider();
    WowDecomp_RefreshShaderStatus();
end

local function WowDecomp_RegisterCheckbox(panel, parent, name, label, cvar, tooltip, anchor, x, y, refresh)
    local button = CreateFrame("CheckButton", name, parent, "VideoOptionsSmallCheckButtonTemplate");
    button.type = CONTROLTYPE_CHECKBOX;
    button.cvar = cvar;
    button.tooltipText = tooltip;

    local entry = { text = label, tooltip = tooltip };
    panel.options[cvar] = entry;
    BlizzardOptionsPanel_RegisterControl(button, panel);
    WowDecomp_SetupRegisteredControl(button, panel, entry);
    button:SetPoint("TOPLEFT", anchor, "TOPLEFT", x, y);

    button:SetScript("OnClick", function(self)
        if self:GetChecked() then
            PlaySound("igMainMenuOptionCheckBoxOn");
        else
            PlaySound("igMainMenuOptionCheckBoxOff");
        end
        BlizzardOptionsPanel_CheckButton_OnClick(self);
        WowDecomp_ApplyPlaytestCVar(self);
        if VideoOptionsFrameApply then
            VideoOptionsFrameApply:Enable();
        end
        if refresh then
            refresh();
        end
    end);

    return button;
end

WowDecompAdvancedOptions = {
    extendedView = { text = "Full view", tooltip = "Unlocks the far view used for map-wide playtests and disables the view distance slider." },
    vkWireframe = { text = "Wireframe", tooltip = "Draws depth-tested world geometry as lines through the Vulkan shaders." },
    vkUnlit = { text = "Unlit", tooltip = "Turns off fixed-function lighting in the Vulkan vertex shader." },
};

local function WowDecomp_InstallOnAdvanced()
    local panel = VideoOptionsAdvancedEffectsPanel;

    if not panel or panel.wowDecompInstalled then
        return;
    end

    panel.wowDecompInstalled = true;
    panel.options = panel.options or {};

    for key, entry in pairs(WowDecompAdvancedOptions) do
        panel.options[key] = entry;
    end

    local anchor = VideoOptionsAdvancedEffectsPanelAerialFog
        or VideoOptionsAdvancedEffectsPanelPostAA
        or VideoOptionsAdvancedEffectsPanelAmbientOcclusion
        or VideoOptionsAdvancedEffectsPanelSunshafts
        or panel;

    local fullView = WowDecomp_RegisterCheckbox(
        panel,
        panel,
        "VideoOptionsAdvancedPanelFullView",
        "Full view",
        "extendedView",
        WowDecompAdvancedOptions.extendedView.tooltip,
        anchor,
        anchor == panel and 16 or 0,
        anchor == panel and -80 or -28,
        WowDecomp_OnPlaytestCVarChanged
    );

    local wireframe = WowDecomp_RegisterCheckbox(
        panel,
        panel,
        "VideoOptionsAdvancedPanelWireframe",
        "Wireframe",
        "vkWireframe",
        WowDecompAdvancedOptions.vkWireframe.tooltip,
        fullView,
        0,
        -28,
        nil
    );

    local unlit = WowDecomp_RegisterCheckbox(
        panel,
        panel,
        "VideoOptionsAdvancedPanelUnlit",
        "Unlit",
        "vkUnlit",
        WowDecompAdvancedOptions.vkUnlit.tooltip,
        wireframe,
        0,
        -28,
        nil
    );

    local reload = CreateFrame("Button", "VideoOptionsAdvancedPanelReloadShaders", panel, "UIPanelButtonTemplate");
    reload:SetWidth(148);
    reload:SetHeight(22);
    reload:SetPoint("TOPLEFT", unlit, "BOTTOMLEFT", 0, -8);
    reload:SetText("Reload shaders");
    reload:SetScript("OnClick", function()
        PlaySound("igMainMenuOptionCheckBoxOn");
        if ReloadVulkanShaders then
            ReloadVulkanShaders();
        end
        WowDecomp_RefreshShaderStatus();
    end);

    local font = GameFontHighlightSmall and "GameFontHighlightSmall" or (OptionsFontHighlightSmall and "OptionsFontHighlightSmall" or nil);
    local status = panel:CreateFontString("VideoOptionsAdvancedPanelShaderStatus", "ARTWORK", font);
    status:SetPoint("TOPLEFT", reload, "BOTTOMLEFT", 0, -8);
    status:SetWidth(348);
    status:SetJustifyH("LEFT");
    status:SetJustifyV("TOP");
    status:SetText(GetVulkanShaderStatus and GetVulkanShaderStatus() or "");

    panel:HookScript("OnUpdate", function(self, elapsed)
        self.wowDecompStatusTimer = (self.wowDecompStatusTimer or 0) + elapsed;
        if self.wowDecompStatusTimer < 0.25 then
            return;
        end
        self.wowDecompStatusTimer = 0;
        WowDecomp_RefreshShaderStatus();
    end);
end

if VideoOptionsFrame then
    VideoOptionsFrame:HookScript("OnShow", function()
        WowDecomp_InstallOnAdvanced();
        WowDecomp_OnPlaytestCVarChanged();
    end);
end

WowDecomp_InstallOnAdvanced();
