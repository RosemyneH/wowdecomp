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
    vkFxaa = { text = "Anti-aliasing", tooltip = "Smooths jagged edges with a fullscreen shader after the frame is drawn." },
    vkSharpen = { text = "Sharpen", minValue = 0, maxValue = 4, valueStep = 1, tooltip = "Crisps the image. Zero is off. Useful after anti-aliasing." },
    vkWireframe = { text = "Wireframe", tooltip = "Draws depth-tested world geometry as lines through the Vulkan shaders." },
    vkUnlit = { text = "Unlit", tooltip = "Turns off fixed-function lighting in the Vulkan vertex shader." },
};

local function WowDecomp_Font(large)
    if large then
        if GameFontNormalLarge then
            return "GameFontNormalLarge";
        end
        if GlueFontNormalLarge then
            return "GlueFontNormalLarge";
        end
    end
    if GameFontHighlightSmall then
        return "GameFontHighlightSmall";
    end
    if GlueFontHighlightSmall then
        return "GlueFontHighlightSmall";
    end
    return nil;
end

local function WowDecomp_InstallAdvanced()
    if VideoOptionsAdvancedPanel or not VideoOptionsFramePanelContainer or not VideoOptionsPanel_OnLoad then
        return;
    end

    local panel = CreateFrame("Frame", "VideoOptionsAdvancedPanel", VideoOptionsFramePanelContainer);
    panel:Hide();
    panel.name = "Advanced";
    panel.options = WowDecompAdvancedOptions;
    VideoOptionsPanel_OnLoad(panel);

    local titleFont = WowDecomp_Font(true);
    local smallFont = WowDecomp_Font(false);
    local title = titleFont and panel:CreateFontString(nil, "ARTWORK", titleFont);

    if title then
        title:SetPoint("TOPLEFT", panel, "TOPLEFT", 16, -16);
        title:SetJustifyH("LEFT");
        title:SetText("Advanced");
    end

    if smallFont and title then
        local subText = panel:CreateFontString(nil, "ARTWORK", smallFont);
        subText:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8);
        subText:SetPoint("RIGHT", panel, "RIGHT", -32, 0);
        subText:SetJustifyH("LEFT");
        subText:SetJustifyV("TOP");
        subText:SetHeight(32);
        subText:SetText("Playtest settings. Full view, image effects, and Vulkan shader experiments.");
    end

    local view = CreateFrame("Frame", "VideoOptionsAdvancedPanelView", panel, "OptionsBoxTemplate");
    view:SetWidth(380);
    view:SetHeight(44);
    view:SetPoint("TOPLEFT", panel, "TOPLEFT", 16, -72);
    _G[view:GetName() .. "Title"]:SetText("View");

    WowDecomp_RegisterCheckbox(
        panel,
        view,
        "VideoOptionsAdvancedPanelFullView",
        "Full view",
        "extendedView",
        WowDecompAdvancedOptions.extendedView.tooltip,
        view,
        12,
        -10,
        WowDecomp_OnPlaytestCVarChanged
    );

    local image = CreateFrame("Frame", "VideoOptionsAdvancedPanelImage", panel, "OptionsBoxTemplate");
    image:SetWidth(380);
    image:SetHeight(96);
    image:SetPoint("TOPLEFT", view, "BOTTOMLEFT", 0, -28);
    _G[image:GetName() .. "Title"]:SetText("Image");

    local antiAlias = WowDecomp_RegisterCheckbox(
        panel,
        image,
        "VideoOptionsAdvancedPanelAntiAlias",
        "Anti-aliasing",
        "vkFxaa",
        WowDecompAdvancedOptions.vkFxaa.tooltip,
        image,
        12,
        -8,
        nil
    );
    antiAlias:SetHitRectInsets(0, -140, 0, 0);

    local sharpen = CreateFrame("Slider", "VideoOptionsAdvancedPanelSharpen", image, "OptionsSliderTemplate");
    sharpen:SetWidth(200);
    sharpen:SetPoint("TOPLEFT", image, "TOPLEFT", 16, -52);
    sharpen.type = CONTROLTYPE_SLIDER;
    sharpen.cvar = "vkSharpen";
    sharpen.tooltipText = WowDecompAdvancedOptions.vkSharpen.tooltip;
    _G[sharpen:GetName() .. "Low"]:SetText("Off");
    _G[sharpen:GetName() .. "High"]:SetText("High");
    sharpen:SetScript("OnValueChanged", function(self, value)
        if not self.wowDecompReady then
            return;
        end
        BlizzardOptionsPanel_Slider_OnValueChanged(self, math.floor(value + 0.5));
        if VideoOptionsFrameApply then
            VideoOptionsFrameApply:Enable();
        end
    end);
    BlizzardOptionsPanel_RegisterControl(sharpen, panel);
    WowDecomp_SetupRegisteredControl(sharpen, panel, WowDecompAdvancedOptions.vkSharpen);
    sharpen.wowDecompReady = true;

    local shaders = CreateFrame("Frame", "VideoOptionsAdvancedPanelShaders", panel, "OptionsBoxTemplate");
    shaders:SetWidth(380);
    shaders:SetHeight(150);
    shaders:SetPoint("TOPLEFT", image, "BOTTOMLEFT", 0, -28);
    _G[shaders:GetName() .. "Title"]:SetText("Shaders");

    local wireframe = WowDecomp_RegisterCheckbox(
        panel,
        shaders,
        "VideoOptionsAdvancedPanelWireframe",
        "Wireframe",
        "vkWireframe",
        WowDecompAdvancedOptions.vkWireframe.tooltip,
        shaders,
        12,
        -10,
        nil
    );

    local unlit = WowDecomp_RegisterCheckbox(
        panel,
        shaders,
        "VideoOptionsAdvancedPanelUnlit",
        "Unlit",
        "vkUnlit",
        WowDecompAdvancedOptions.vkUnlit.tooltip,
        wireframe,
        0,
        -28,
        nil
    );

    local reload = CreateFrame("Button", "VideoOptionsAdvancedPanelReloadShaders", shaders, "UIPanelButtonTemplate");
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

    if smallFont then
        local status = shaders:CreateFontString("VideoOptionsAdvancedPanelShaderStatus", "ARTWORK", smallFont);
        status:SetPoint("TOPLEFT", reload, "BOTTOMLEFT", 0, -6);
        status:SetWidth(348);
        status:SetJustifyH("LEFT");
        status:SetJustifyV("TOP");
        status:SetText(GetVulkanShaderStatus and GetVulkanShaderStatus() or "");
    end

    panel:SetScript("OnUpdate", function(self, elapsed)
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
        WowDecomp_InstallAdvanced();
        WowDecomp_OnPlaytestCVarChanged();
    end);
end

WowDecomp_InstallAdvanced();
