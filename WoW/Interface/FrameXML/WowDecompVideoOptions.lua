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
    gxMsdfFonts = { text = "Distance-field fonts", tooltip = "Rebuilds UI fonts as multi-channel distance fields so edges stay smooth when the frame scales them." },
    gxMsdfRange = { text = "Range", minValue = 1, maxValue = 8, valueStep = 1, tooltip = "Texels of distance stored around each glyph. The atlases rebuild when you release the slider. 4 is the normal value." },
    gxMsdfSharp = { text = "Sharpness", minValue = 0.25, maxValue = 4, valueStep = 0.25, tooltip = "How hard the shader cuts the distance field. 1 matches the stored range." },
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
        subText:SetText("Playtest settings. Scroll for distance-field fonts, image effects, and Vulkan shader experiments.");
    end

    local scroll = CreateFrame("ScrollFrame", "VideoOptionsAdvancedPanelScroll", panel);
    scroll:SetPoint("TOPLEFT", panel, "TOPLEFT", 0, -64);
    scroll:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -28, 8);
    scroll:EnableMouseWheel(true);

    local child = CreateFrame("Frame", "VideoOptionsAdvancedPanelScrollChild", scroll);
    child:SetWidth(400);
    child:SetHeight(720);
    scroll:SetScrollChild(child);

    local function clampScroll()
        local maxScroll = child:GetHeight() - scroll:GetHeight();
        if maxScroll < 0 then
            maxScroll = 0;
        end
        local current = scroll:GetVerticalScroll();
        if current < 0 then
            current = 0;
        elseif current > maxScroll then
            current = maxScroll;
        end
        scroll:SetVerticalScroll(current);
    end

    scroll:SetScript("OnMouseWheel", function(self, delta)
        self:SetVerticalScroll(self:GetVerticalScroll() - delta * 48);
        clampScroll();
    end);
    scroll:SetScript("OnSizeChanged", clampScroll);
    panel:EnableMouseWheel(true);
    panel:SetScript("OnMouseWheel", function(_, delta)
        scroll:GetScript("OnMouseWheel")(scroll, delta);
    end);

    local view = CreateFrame("Frame", "VideoOptionsAdvancedPanelView", child, "OptionsBoxTemplate");
    view:SetWidth(380);
    view:SetHeight(44);
    view:SetPoint("TOPLEFT", child, "TOPLEFT", 16, -8);
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

    local image = CreateFrame("Frame", "VideoOptionsAdvancedPanelImage", child, "OptionsBoxTemplate");
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

    local shaders = CreateFrame("Frame", "VideoOptionsAdvancedPanelShaders", child, "OptionsBoxTemplate");
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

    -- ʕ •ᴥ•ʔ✿ Login XML has GluePanelButtonTemplate. UIPanelButtonTemplate arrives with the world UI ✿ ʕ •ᴥ•ʔ
    local buttonTemplate = GlueParent and "GluePanelButtonTemplate" or "UIPanelButtonTemplate";
    local reload = CreateFrame("Button", "VideoOptionsAdvancedPanelReloadShaders", shaders, buttonTemplate);
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

    local fonts = CreateFrame("Frame", "VideoOptionsAdvancedPanelFonts", child, "OptionsBoxTemplate");
    fonts:SetWidth(380);
    fonts:SetHeight(268);
    fonts:SetPoint("TOPLEFT", shaders, "BOTTOMLEFT", 0, -28);
    _G[fonts:GetName() .. "Title"]:SetText("Fonts");

    local msdf = WowDecomp_RegisterCheckbox(
        panel,
        fonts,
        "VideoOptionsAdvancedPanelMsdf",
        "Distance-field fonts",
        "gxMsdfFonts",
        WowDecompAdvancedOptions.gxMsdfFonts.tooltip,
        fonts,
        12,
        -8,
        WowDecomp_RefreshMsdfStatus
    );
    msdf:SetHitRectInsets(0, -180, 0, 0);

    if smallFont then
        local viewLabel = fonts:CreateFontString(nil, "ARTWORK", smallFont);
        viewLabel:SetPoint("TOPLEFT", fonts, "TOPLEFT", 16, -36);
        viewLabel:SetText("View");
    end

    -- ʕ •ᴥ•ʔ✿ Glue login has no UIDropDownMenuTemplate. The view is a radio list of the same checkboxes as the rest of this page ✿ ʕ •ᴥ•ʔ
    panel.msdfView = "0";
    panel.msdfChecks = {};
    local viewChoices = {
        { "VideoOptionsAdvancedPanelMsdfShaded", "Shaded", "0", -52 },
        { "VideoOptionsAdvancedPanelMsdfChannels", "Color channels", "1", -74 },
        { "VideoOptionsAdvancedPanelMsdfDistance", "Signed distance", "2", -96 },
    };
    for _, choice in ipairs(viewChoices) do
        local check = CreateFrame("CheckButton", choice[1], fonts, "VideoOptionsSmallCheckButtonTemplate");
        check.msdfValue = choice[3];
        check:SetPoint("TOPLEFT", fonts, "TOPLEFT", 16, choice[4]);
        check:SetHitRectInsets(0, -160, 0, 0);
        local text = _G[check:GetName() .. "Text"];
        if text then
            text:SetText(choice[2]);
        end
        check:SetScript("OnClick", function(self)
            if self:GetChecked() then
                PlaySound("igMainMenuOptionCheckBoxOn");
            else
                PlaySound("igMainMenuOptionCheckBoxOff");
            end
            self:SetChecked(true);
            panel.msdfView = self.msdfValue;
            SetCVar("gxMsdfView", self.msdfValue);
            if self.msdfValue ~= "0" and not msdf:GetChecked() then
                msdf:SetChecked(true);
                WowDecomp_ApplyPlaytestCVar(msdf);
            end
            WowDecomp_RefreshMsdfStatus();
            if VideoOptionsFrameApply then
                VideoOptionsFrameApply:Enable();
            end
        end);
        panel.msdfChecks[#panel.msdfChecks + 1] = check;
    end

    local function msdfSlider(name, cvar, entry, low, high, y, live)
        local slider = CreateFrame("Slider", name, fonts, "OptionsSliderTemplate");
        slider:SetWidth(200);
        slider:SetPoint("TOPLEFT", fonts, "TOPLEFT", 16, y);
        slider.type = CONTROLTYPE_SLIDER;
        slider.cvar = cvar;
        slider.tooltipText = entry.tooltip;
        _G[name .. "Low"]:SetText(low);
        _G[name .. "High"]:SetText(high);

        local function snappedText(value)
            local step = entry.valueStep or 1;
            local snapped = math.floor(value / step + 0.5) * step;
            local text = step >= 1 and tostring(math.floor(snapped + 0.5)) or string.format("%.2f", snapped);
            local title = _G[name .. "Text"];
            if title then
                title:SetText((entry.text or name) .. ": " .. text);
            end
            return snapped, text;
        end

        local function apply(value)
            if slider.wowDecompApplying then
                return;
            end
            slider.wowDecompApplying = true;
            local snapped, text = snappedText(value);
            BlizzardOptionsPanel_Slider_OnValueChanged(slider, snapped);
            SetCVar(cvar, text);
            if WowDecomp_RefreshMsdfStatus then
                WowDecomp_RefreshMsdfStatus();
            end
            if VideoOptionsFrameApply then
                VideoOptionsFrameApply:Enable();
            end
            slider.wowDecompApplying = false;
        end

        slider:SetScript("OnValueChanged", function(self, value)
            if not self.wowDecompReady then
                return;
            end
            if live then
                apply(value);
            else
                snappedText(value);
            end
        end);
        slider:SetScript("OnMouseUp", function(self)
            apply(self:GetValue());
        end);
        BlizzardOptionsPanel_RegisterControl(slider, panel);
        WowDecomp_SetupRegisteredControl(slider, panel, entry);
        slider.wowDecompReady = true;
        snappedText(slider:GetValue());
        return slider;
    end

    msdfSlider("VideoOptionsAdvancedPanelMsdfRange", "gxMsdfRange", WowDecompAdvancedOptions.gxMsdfRange, "1", "8", -132, false);
    msdfSlider("VideoOptionsAdvancedPanelMsdfSharp", "gxMsdfSharp", WowDecompAdvancedOptions.gxMsdfSharp, "Soft", "Hard", -172, true);

    if smallFont then
        local fontStatus = fonts:CreateFontString("VideoOptionsAdvancedPanelMsdfStatus", "ARTWORK", smallFont);
        fontStatus:SetPoint("TOPLEFT", fonts, "TOPLEFT", 16, -216);
        fontStatus:SetWidth(348);
        fontStatus:SetJustifyH("LEFT");
        fontStatus:SetJustifyV("TOP");
        fontStatus:SetHeight(40);
    end

    function WowDecomp_RefreshMsdfStatus()
        local fontStatus = _G["VideoOptionsAdvancedPanelMsdfStatus"];
        if fontStatus then
            local on = GetCVar("gxMsdfFonts") == "1";
            local view = GetCVar("gxMsdfView") or "0";
            if not on then
                fontStatus:SetText("Distance fields are off. UI fonts use the alpha atlas.");
            elseif view == "1" then
                fontStatus:SetText("Showing the red, green, and blue channels. Colored edges mean the field is present.");
            elseif view == "2" then
                fontStatus:SetText("Showing the median distance. Ink is white and the outside is black.");
            else
                fontStatus:SetText("Distance fields are on. Range " .. (GetCVar("gxMsdfRange") or "4") .. ", sharpness " .. (GetCVar("gxMsdfSharp") or "1") .. ".");
            end
        end
        if panel.msdfChecks then
            panel.msdfView = GetCVar("gxMsdfView") or panel.msdfView or "0";
            for _, check in ipairs(panel.msdfChecks) do
                check:SetChecked(check.msdfValue == panel.msdfView);
            end
        end
    end

    local previousRefresh = panel.refresh;
    panel.refresh = function(self)
        if previousRefresh then
            previousRefresh(self);
        end
        WowDecomp_RefreshMsdfStatus();
    end

    local previousDefault = panel.default;
    panel.default = function(self)
        if previousDefault then
            previousDefault(self);
        end
        SetCVar("gxMsdfView", "0");
        self.msdfView = "0";
        WowDecomp_RefreshMsdfStatus();
    end

    WowDecomp_RefreshMsdfStatus();

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
        if WowDecomp_RefreshMsdfStatus then
            WowDecomp_RefreshMsdfStatus();
        end
    end);
end

WowDecomp_InstallAdvanced();

local function WowDecomp_ControllerCheckbox(panel, parent, name, label, cvar, tooltip, anchor, x, y, hit)
    local button = CreateFrame("CheckButton", name, parent, "InterfaceOptionsCheckButtonTemplate");
    if not button then
        button = CreateFrame("CheckButton", name, parent, "VideoOptionsSmallCheckButtonTemplate");
    end
    button.type = CONTROLTYPE_CHECKBOX;
    button.cvar = cvar;
    button.tooltipText = tooltip;
    local entry = { text = label, tooltip = tooltip };
    panel.options[cvar] = entry;
    BlizzardOptionsPanel_RegisterControl(button, panel);
    WowDecomp_SetupRegisteredControl(button, panel, entry);
    button:SetPoint("TOPLEFT", anchor, "TOPLEFT", x, y);
    button:SetHitRectInsets(0, -(hit or 140), 0, 0);
    button:SetScript("OnClick", function(self)
        if self:GetChecked() then
            PlaySound("igMainMenuOptionCheckBoxOn");
        else
            PlaySound("igMainMenuOptionCheckBoxOff");
        end
        BlizzardOptionsPanel_CheckButton_OnClick(self);
    end);
    return button;
end

local function WowDecomp_ControllerSlider(panel, parent, name, label, cvar, tooltip, minValue, maxValue, step, low, high, anchor, x, y)
    local slider = CreateFrame("Slider", name, parent, "OptionsSliderTemplate");
    slider:SetWidth(160);
    slider:SetPoint("TOPLEFT", anchor, "TOPLEFT", x, y);
    slider.type = CONTROLTYPE_SLIDER;
    slider.cvar = cvar;
    slider.tooltipText = tooltip;
    _G[name .. "Text"]:SetText(label);
    _G[name .. "Low"]:SetText(low);
    _G[name .. "High"]:SetText(high);
    local entry = { text = label, tooltip = tooltip, minValue = minValue, maxValue = maxValue, valueStep = step };
    panel.options[cvar] = entry;
    BlizzardOptionsPanel_RegisterControl(slider, panel);
    WowDecomp_SetupRegisteredControl(slider, panel, entry);
    slider:SetScript("OnValueChanged", function(self, value)
        if not self.wowDecompReady then
            return;
        end
        local snapped = step >= 1 and math.floor(value + 0.5) or math.floor(value / step + 0.5) * step;
        BlizzardOptionsPanel_Slider_OnValueChanged(self, snapped);
    end);
    slider.wowDecompReady = true;
    return slider;
end

local function WowDecomp_InstallController()
    if WowDecompControllerPanel or not InterfaceOptions_AddCategory or not BlizzardOptionsPanel_RegisterControl then
        return;
    end

    local parent = InterfaceOptionsFramePanelContainer or UIParent;
    local panel = CreateFrame("Frame", "WowDecompControllerPanel", parent);
    panel.name = "Controller";
    panel.parent = CONTROLS_LABEL or "Controls";
    panel.options = {};
    panel.retail = {};

    local function addRetail(control)
        panel.retail[#panel.retail + 1] = control;
        return control;
    end

    if InterfaceOptionsPanel_OnLoad then
        InterfaceOptionsPanel_OnLoad(panel);
    end

    local titleFont = WowDecomp_Font(true);
    local smallFont = WowDecomp_Font(false);
    local title = titleFont and panel:CreateFontString(nil, "ARTWORK", titleFont);
    if title then
        title:SetPoint("TOPLEFT", 16, -16);
        title:SetText("Controller");
    end
    if smallFont and title then
        local sub = panel:CreateFontString(nil, "ARTWORK", smallFont);
        sub:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -6);
        sub:SetPoint("RIGHT", panel, "RIGHT", -32, 0);
        sub:SetJustifyH("LEFT");
        sub:SetHeight(28);
        sub:SetText("Built-in plays the pad directly. A GameCube controller uses A, B, X, Y, Z, L, and R. Key bindings assigns those buttons, and Steam Input leaves the pad alone.");
    end

    local status = smallFont and panel:CreateFontString("WowDecompControllerStatus", "ARTWORK", smallFont);
    if status and title then
        status:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -28);
        status:SetPoint("RIGHT", panel, "RIGHT", -24, 0);
        status:SetJustifyH("LEFT");
        status:SetHeight(32);
    end

    local general = CreateFrame("Frame", "WowDecompControllerGeneral", panel, "OptionsBoxTemplate");
    general:SetWidth(188);
    general:SetHeight(176);
    general:SetPoint("TOPLEFT", 8, -92);
    _G[general:GetName() .. "Title"]:SetText("General");

    WowDecomp_ControllerCheckbox(panel, general, "WowDecompControllerEnable", "Enable controller", "Joystick", "Turns the connected gamepad on.", general, 12, -8);
    WowDecomp_ControllerCheckbox(panel, general, "WowDecompControllerCinema", "Use during cinematics", "CinematicJoystick", "Leave the pad active while a cinematic is playing.", general, 12, -36);
    local showUi = WowDecomp_ControllerCheckbox(panel, general, "WowDecompControllerUI", "Show controller UI", "padUI", "Shows the controller buttons on screen. They light up while held.", general, 12, -64);
    showUi.setFunc = function(value)
        SetCVar("padUI", value);
    end

    local layoutLabel = smallFont and general:CreateFontString(nil, "ARTWORK", smallFont);
    if layoutLabel then
        layoutLabel:SetPoint("TOPLEFT", general, "TOPLEFT", 16, -96);
        layoutLabel:SetText("Layout");
    end
    local layout = CreateFrame("Frame", "WowDecompControllerLayout", general, "UIDropDownMenuTemplate");
    layout:SetPoint("TOPLEFT", general, "TOPLEFT", 48, -86);
    UIDropDownMenu_SetWidth(layout, 110);
    UIDropDownMenu_JustifyText(layout, "LEFT");
    panel.layoutDrop = layout;

    local styleLabel = smallFont and general:CreateFontString(nil, "ARTWORK", smallFont);
    if styleLabel then
        styleLabel:SetPoint("TOPLEFT", general, "TOPLEFT", 16, -128);
        styleLabel:SetText("Pad");
    end
    local style = CreateFrame("Frame", "WowDecompControllerStyle", general, "UIDropDownMenuTemplate");
    style:SetPoint("TOPLEFT", general, "TOPLEFT", 48, -118);
    UIDropDownMenu_SetWidth(style, 110);
    UIDropDownMenu_JustifyText(style, "LEFT");
    panel.styleDrop = style;

    local sticks = CreateFrame("Frame", "WowDecompControllerSticks", panel, "OptionsBoxTemplate");
    sticks:SetWidth(188);
    sticks:SetHeight(196);
    sticks:SetPoint("TOPLEFT", general, "BOTTOMLEFT", 0, -20);
    _G[sticks:GetName() .. "Title"]:SetText("Sticks");

    addRetail(WowDecomp_ControllerSlider(panel, sticks, "WowDecompControllerDeadzone", "Deadzone", "padDeadzone", "Ignores a little stick movement around the center.", 0, 0.5, 0.01, "0", "0.5", sticks, 16, -22));
    addRetail(WowDecomp_ControllerSlider(panel, sticks, "WowDecompControllerLook", "Look", "padLookSens", "How fast the right stick turns the camera.", 0.25, 3, 0.05, "Slow", "Fast", sticks, 16, -58));
    addRetail(WowDecomp_ControllerSlider(panel, sticks, "WowDecompControllerCursor", "Cursor", "padCursorSpeed", "How fast the right stick moves the cursor.", 4, 48, 1, "Slow", "Fast", sticks, 16, -94));
    addRetail(WowDecomp_ControllerCheckbox(panel, sticks, "WowDecompControllerInvert", "Invert vertical look", "padInvertY", "Looking up on the stick looks down in the world.", sticks, 12, -128));
    addRetail(WowDecomp_ControllerCheckbox(panel, sticks, "WowDecompControllerStrafe", "Left stick strafes", "padStrafe", "On strafes with Q and E. Off turns with A and D.", sticks, 12, -156));

    local buttons = CreateFrame("Frame", "WowDecompControllerButtons", panel, "OptionsBoxTemplate");
    buttons:SetWidth(210);
    buttons:SetHeight(188);
    buttons:SetPoint("TOPLEFT", general, "TOPRIGHT", 8, 0);
    _G[buttons:GetName() .. "Title"]:SetText("Buttons");

    local face = {
        { "WowDecompControllerSouth", "A jumps", "padSouth", "A jumps. With the cursor out, A clicks." },
        { "WowDecompControllerEast", "B cancels", "padEast", "B closes what is open, or clears the target." },
        { "WowDecompControllerWest", "X interacts", "padWest", "X right-clicks, which talks, loots, and uses objects." },
        { "WowDecompControllerNorth", "Y sheathes", "padNorth", "Y sheathes and unsheathes your weapons." },
        { "WowDecompControllerShoulders", "Shoulders", "padShoulders", "Right shoulder targets the next enemy. Left shoulder targets the previous one." },
        { "WowDecompControllerTriggers", "Triggers", "padTriggers", "Right trigger presses 1. Left trigger presses 2." },
        { "WowDecompControllerBack", "Select map", "padBack", "The Select button opens and closes the map." },
        { "WowDecompControllerStart", "Start menu", "padStart", "The Start button opens the game menu." },
        { "WowDecompControllerCursorToggle", "Stick cursor", "padCursorToggle", "Clicking the right stick switches between looking and the cursor." },
        { "WowDecompControllerAutorun", "Stick autorun", "padAutorun", "Clicking the left stick toggles autorun." },
        { "WowDecompControllerDpad", "D-pad panels", "padDpad", "Up character, down bags, left spellbook, right quest log." },
    };
    for i, entry in ipairs(face) do
        local col = i > 6 and 1 or 0;
        local row = col == 1 and (i - 7) or (i - 1);
        addRetail(WowDecomp_ControllerCheckbox(panel, buttons, entry[1], entry[2], entry[3], entry[4], buttons, 8 + col * 104, -8 - row * 26, 90));
    end

    local function layoutText(value)
        if value == "1" then
            return "Steam Input";
        end
        if value == "2" then
            return "Key bindings";
        end
        return "Built-in";
    end

    local function styleText(value)
        if value == "1" then
            return "Standard";
        end
        if value == "2" then
            return "GameCube";
        end
        return "Auto";
    end

    local function shownKind()
        if panel.styleValue == "2" then
            return "gamecube";
        end
        if panel.styleValue == "1" then
            return "standard";
        end
        local kind = GetControllerKind and GetControllerKind() or "none";
        if kind == "none" then
            return "standard";
        end
        return kind;
    end

    local function setCheckText(name, text, tooltip)
        local button = _G[name];
        local label = _G[name .. "Text"];
        if label then
            label:SetText(text);
        end
        if button then
            button.tooltipText = tooltip;
        end
    end

    local function applyFaceLabels()
        local gc = shownKind() == "gamecube";
        setCheckText("WowDecompControllerSouth", "A jumps", gc and "A jumps. With the cursor out, A clicks." or "A jumps. With the cursor out, A clicks.");
        setCheckText("WowDecompControllerEast", "B cancels", "B closes what is open, or clears the target.");
        setCheckText("WowDecompControllerWest", "X interacts", "X right-clicks, which talks, loots, and uses objects.");
        setCheckText("WowDecompControllerNorth", "Y sheathes", "Y sheathes and unsheathes your weapons.");
        setCheckText("WowDecompControllerShoulders", gc and "Z targets" or "Shoulders", gc and "Z targets the next enemy." or "Right shoulder targets the next enemy. Left shoulder targets the previous one.");
        setCheckText("WowDecompControllerTriggers", gc and "L and R" or "Triggers", gc and "R presses 1. L presses 2." or "Right trigger presses 1. Left trigger presses 2.");
        setCheckText("WowDecompControllerCursorToggle", gc and "C-stick cursor" or "Stick cursor", "Clicking the right stick switches between looking and the cursor.");
    end

    local function controlEnabled(control)
        local steam = panel.layoutValue == "1";
        local builtin = panel.layoutValue == "0";
        if steam then
            return false;
        end
        if control.type == CONTROLTYPE_SLIDER or control.cvar == "padStrafe" or control.cvar == "padInvertY" then
            local axis = panel.axisBind and panel.axisBind:GetChecked() and panel.layoutValue == "2";
            if axis and (control.cvar == "padLookSens" or control.cvar == "padCursorSpeed" or control.cvar == "padInvertY") then
                return false;
            end
            return true;
        end
        return builtin;
    end

    local function setControlEnabled(control, enabled)
        if control.type == CONTROLTYPE_SLIDER and BlizzardOptionsPanel_Slider_Disable then
            if enabled then
                BlizzardOptionsPanel_Slider_Enable(control);
            else
                BlizzardOptionsPanel_Slider_Disable(control);
            end
        elseif enabled then
            control:Enable();
        else
            control:Disable();
        end
    end

    local function applyLayoutEnabled()
        for _, control in ipairs(panel.retail) do
            setControlEnabled(control, controlEnabled(control));
        end
        if panel.axisBind then
            setControlEnabled(panel.axisBind, panel.layoutValue == "2");
        end
    end

    UIDropDownMenu_Initialize(layout, function()
        local function choice(text, value)
            local info = UIDropDownMenu_CreateInfo();
            info.text = text;
            info.value = value;
            info.checked = panel.layoutValue == value;
            info.func = function(button)
                panel.layoutValue = button.value;
                UIDropDownMenu_SetText(layout, layoutText(button.value));
                applyLayoutEnabled();
            end
            UIDropDownMenu_AddButton(info);
        end
        choice("Built-in", "0");
        choice("Key bindings", "2");
        choice("Steam Input", "1");
    end);

    UIDropDownMenu_Initialize(style, function()
        local function choice(text, value)
            local info = UIDropDownMenu_CreateInfo();
            info.text = text;
            info.value = value;
            info.checked = panel.styleValue == value;
            info.func = function(button)
                panel.styleValue = button.value;
                UIDropDownMenu_SetText(style, styleText(button.value));
                applyFaceLabels();
            end
            UIDropDownMenu_AddButton(info);
        end
        choice("Auto", "0");
        choice("Standard", "1");
        choice("GameCube", "2");
    end);

    local binds = CreateFrame("Frame", "WowDecompControllerBinds", panel, "OptionsBoxTemplate");
    binds:SetWidth(210);
    binds:SetHeight(148);
    binds:SetPoint("TOPLEFT", buttons, "BOTTOMLEFT", 0, -16);
    _G[binds:GetName() .. "Title"]:SetText("Key bindings");
    if smallFont then
        local hint = binds:CreateFontString(nil, "ARTWORK", smallFont);
        hint:SetPoint("TOPLEFT", binds, "TOPLEFT", 12, -8);
        hint:SetPoint("RIGHT", binds, "RIGHT", -12, 0);
        hint:SetHeight(36);
        hint:SetJustifyH("LEFT");
        hint:SetText("Choose a command in Key Bindings, then press a button. Load defaults fills A, B, X, Y, Z, L, and R.");
    end
    panel.axisBind = WowDecomp_ControllerCheckbox(panel, binds, "WowDecompControllerAxisBind", "Bind stick directions", "padAxisBindings", "The stick sends key bindings instead of moving and looking. Load defaults assigns move, strafe, turn, and pitch.", binds, 8, -44, 150);
    panel.axisBind:SetScript("OnClick", function(self)
        if self:GetChecked() then
            PlaySound("igMainMenuOptionCheckBoxOn");
        else
            PlaySound("igMainMenuOptionCheckBoxOff");
        end
        BlizzardOptionsPanel_CheckButton_OnClick(self);
        applyLayoutEnabled();
    end);

    local openBindings = CreateFrame("Button", "WowDecompControllerOpenBindings", binds, "UIPanelButtonTemplate");
    openBindings:SetWidth(90);
    openBindings:SetHeight(22);
    openBindings:SetPoint("BOTTOMLEFT", binds, "BOTTOMLEFT", 10, 10);
    openBindings:SetText("Bindings");
    openBindings:SetScript("OnClick", function()
        PlaySound("igMainMenuOption");
        if InterfaceOptionsFrame and HideUIPanel then
            HideUIPanel(InterfaceOptionsFrame);
        end
        KeyBindingFrame_LoadUI();
        KeyBindingFrame.mode = 1;
        ShowUIPanel(KeyBindingFrame);
    end);

    local loadDefaults = CreateFrame("Button", "WowDecompControllerLoadDefaults", binds, "UIPanelButtonTemplate");
    loadDefaults:SetWidth(90);
    loadDefaults:SetHeight(22);
    loadDefaults:SetPoint("LEFT", openBindings, "RIGHT", 6, 0);
    loadDefaults:SetText("Defaults");
    loadDefaults:SetScript("OnClick", function()
        PlaySound("igMainMenuOptionCheckBoxOn");
        local gc = shownKind() == "gamecube";
        local rows = {
            { "JOYBUTTON1", "JUMP" },
            { "JOYBUTTON2", "TOGGLEGAMEMENU" },
            { "JOYBUTTON3", "INTERACTMOUSEOVER" },
            { "JOYBUTTON4", "TOGGLESHEATH" },
            { "JOYBUTTON5", not gc and "TARGETPREVIOUSENEMY" or nil },
            { "JOYBUTTON6", "TARGETNEARESTENEMY" },
            { "JOYBUTTON7", not gc and "TOGGLEWORLDMAP" or nil },
            { "JOYBUTTON8", "TOGGLEGAMEMENU" },
            { "JOYBUTTON9", "TOGGLEAUTORUN" },
            { "JOYBUTTON11", "TOGGLECHARACTER0" },
            { "JOYBUTTON12", "OPENALLBAGS" },
            { "JOYBUTTON13", "TOGGLESPELLBOOK" },
            { "JOYBUTTON14", "TOGGLEQUESTLOG" },
            { "JOYBUTTON15", "ACTIONBUTTON2" },
            { "JOYBUTTON16", "ACTIONBUTTON1" },
            { "JOYAXIS1NEG", "STRAFELEFT" },
            { "JOYAXIS1POS", "STRAFERIGHT" },
            { "JOYAXIS2NEG", "MOVEFORWARD" },
            { "JOYAXIS2POS", "MOVEBACKWARD" },
            { "JOYAXIS3NEG", "TURNLEFT" },
            { "JOYAXIS3POS", "TURNRIGHT" },
            { "JOYAXIS4NEG", "PITCHUP" },
            { "JOYAXIS4POS", "PITCHDOWN" },
        };
        for _, row in ipairs(rows) do
            if row[2] then
                SetBinding(row[1], row[2]);
            end
        end
        if SaveBindings and GetCurrentBindingSet then
            SaveBindings(GetCurrentBindingSet());
        end
        if panel.layoutValue ~= "2" then
            panel.layoutValue = "2";
            UIDropDownMenu_SetText(layout, layoutText("2"));
            SetCVar("padLayout", "2");
            applyLayoutEnabled();
        end
    end);

    local function sync()
        if BlizzardOptionsPanel_Refresh then
            BlizzardOptionsPanel_Refresh(panel);
        end
        panel.layoutValue = GetCVar("padLayout") or "0";
        panel.styleValue = GetCVar("padStyle") or "0";
        UIDropDownMenu_SetText(layout, layoutText(panel.layoutValue));
        UIDropDownMenu_SetText(style, styleText(panel.styleValue));
        applyFaceLabels();
        if status and GetConnectedController then
            local name = GetConnectedController();
            local kind = GetControllerKind and GetControllerKind() or "none";
            local mapped = GetControllerMapped and GetControllerMapped();
            local kindText = kind == "gamecube" and "GameCube" or (kind == "standard" and "Standard" or "");
            if name == "" then
                status:SetText("Connected: none");
            elseif mapped then
                status:SetText("Connected: " .. name .. (kindText ~= "" and (" (" .. kindText .. ")") or ""));
            elseif kind == "gamecube" then
                status:SetText("Connected: " .. name .. " (GameCube, waiting for an SDL map. Buttons are Button 1 and up.)");
            else
                status:SetText("Connected: " .. name .. " (unmapped. Buttons are Button 1 and up.)");
            end
        end
        applyLayoutEnabled();
    end

    panel.okay = function(self)
        if BlizzardOptionsPanel_Okay then
            BlizzardOptionsPanel_Okay(self);
        end
        SetCVar("padLayout", self.layoutValue or "0");
        SetCVar("padStyle", self.styleValue or "0");
    end
    panel.cancel = function(self)
        if BlizzardOptionsPanel_Cancel then
            BlizzardOptionsPanel_Cancel(self);
        end
        sync();
    end
    panel.default = function(self)
        if BlizzardOptionsPanel_Default then
            BlizzardOptionsPanel_Default(self);
        end
        self.layoutValue = "0";
        self.styleValue = "0";
        SetCVar("padLayout", "0");
        SetCVar("padStyle", "0");
        sync();
    end
    panel.refresh = sync;
    panel:SetScript("OnShow", sync);
    sync();
    InterfaceOptions_AddCategory(panel);
end

WowDecomp_InstallController();

local function WowDecomp_ControllerAction(key)
    local preset = {
        JOYBUTTON1 = "Jump",
        JOYBUTTON2 = "Cancel",
        JOYBUTTON3 = "Interact",
        JOYBUTTON4 = "Sheathe",
        JOYBUTTON5 = "Prev",
        JOYBUTTON6 = "Target",
        JOYBUTTON7 = "Map",
        JOYBUTTON8 = "Menu",
        JOYBUTTON9 = "Autorun",
        JOYBUTTON10 = "Cursor",
        JOYBUTTON11 = "Char",
        JOYBUTTON12 = "Bags",
        JOYBUTTON13 = "Spells",
        JOYBUTTON14 = "Quests",
        JOYBUTTON15 = "Slot 2",
        JOYBUTTON16 = "Slot 1",
    };
    local commands = {
        JUMP = "Jump",
        TOGGLEGAMEMENU = "Menu",
        INTERACTMOUSEOVER = "Interact",
        TOGGLESHEATH = "Sheathe",
        TARGETPREVIOUSENEMY = "Prev",
        TARGETNEARESTENEMY = "Target",
        TOGGLEWORLDMAP = "Map",
        TOGGLEAUTORUN = "Autorun",
        TOGGLECHARACTER0 = "Char",
        OPENALLBAGS = "Bags",
        TOGGLESPELLBOOK = "Spells",
        TOGGLEQUESTLOG = "Quests",
        ACTIONBUTTON2 = "Slot 2",
        ACTIONBUTTON1 = "Slot 1",
        STRAFELEFT = "Strafe L",
        STRAFERIGHT = "Strafe R",
        MOVEFORWARD = "Forward",
        MOVEBACKWARD = "Back",
        TURNLEFT = "Turn L",
        TURNRIGHT = "Turn R",
        PITCHUP = "Look up",
        PITCHDOWN = "Look down",
    };
    if GetCVar("padLayout") == "2" and GetBindingAction then
        local action = GetBindingAction(key);
        if not action or action == "" then
            return "Free";
        end
        return commands[action] or (GetBindingText and GetBindingText(action, "BINDING_NAME_")) or action;
    end
    return preset[key] or "";
end

local function WowDecomp_InstallControllerUI()
    if WowDecompControllerWatch or not UIParent or not CreateFrame then
        return;
    end

    local watch = CreateFrame("Frame", "WowDecompControllerWatch", UIParent);
    local bar = CreateFrame("Frame", "WowDecompControllerUI", UIParent);
    bar:SetSize(528, 78);
    bar:SetPoint("BOTTOM", UIParent, "BOTTOM", 0, 168);
    bar:SetFrameStrata("HIGH");
    bar:EnableMouse(true);
    bar:SetMovable(true);
    bar:RegisterForDrag("LeftButton");
    bar:SetScript("OnDragStart", function(self)
        self:StartMoving();
    end);
    bar:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing();
    end);
    local bg = bar:CreateTexture(nil, "BACKGROUND");
    bg:SetAllPoints();
    bg:SetTexture("Interface\\Tooltips\\UI-Tooltip-Background");
    bg:SetVertexColor(0, 0, 0, 0.72);
    local font = WowDecomp_Font(false) or "GameFontHighlightSmall";
    local slots = {};
    for i = 1, 16 do
        local col = (i - 1) % 8;
        local row = math.floor((i - 1) / 8);
        local text = bar:CreateFontString(nil, "OVERLAY", font);
        text:SetPoint("TOPLEFT", 10 + col * 64, -8 - row * 22);
        text:SetWidth(62);
        text:SetJustifyH("LEFT");
        slots[i] = text;
    end
    local hint = bar:CreateFontString(nil, "OVERLAY", font);
    hint:SetPoint("BOTTOMLEFT", bar, "BOTTOMLEFT", 10, 6);
    hint:SetPoint("RIGHT", bar, "RIGHT", -10, 0);
    hint:SetJustifyH("LEFT");

    local function publishNames()
        if not GetControllerControlCount or not GetControllerControl then
            return;
        end
        local count = GetControllerControlCount();
        for i = 1, count do
            local key, label = GetControllerControl(i);
            if key and label then
                _G["KEY_" .. key] = label;
            end
        end
        local gc = GetControllerKind and GetControllerKind() == "gamecube";
        _G.KEY_JOYAXIS1NEG = gc and "Stick left" or "Stick left";
        _G.KEY_JOYAXIS1POS = gc and "Stick right" or "Stick right";
        _G.KEY_JOYAXIS2NEG = gc and "Stick up" or "Stick up";
        _G.KEY_JOYAXIS2POS = gc and "Stick down" or "Stick down";
        _G.KEY_JOYAXIS3NEG = gc and "C-stick left" or "Look left";
        _G.KEY_JOYAXIS3POS = gc and "C-stick right" or "Look right";
        _G.KEY_JOYAXIS4NEG = gc and "C-stick up" or "Look up";
        _G.KEY_JOYAXIS4POS = gc and "C-stick down" or "Look down";
    end

    local function hookBindings()
        if not KeyBindingFrame or KeyBindingFrame.wowDecompHooked or not KeyBindingFrame.HookScript then
            return;
        end
        KeyBindingFrame.wowDecompHooked = true;
        KeyBindingFrame:HookScript("OnShow", function()
            if SetControllerCapture then
                SetControllerCapture(true);
            end
        end);
        KeyBindingFrame:HookScript("OnHide", function()
            if SetControllerCapture then
                SetControllerCapture(false);
            end
        end);
    end

    watch:SetScript("OnUpdate", function()
        hookBindings();
        local capturing = KeyBindingFrame and KeyBindingFrame:IsShown();
        if SetControllerCapture then
            SetControllerCapture(capturing and true or false);
        end
        if GetControllerCapture then
            local key = GetControllerCapture();
            while key and key ~= "" do
                if capturing and KeyBindingFrame.selected and KeyBindingFrame_OnKeyDown then
                    KeyBindingFrame_OnKeyDown(KeyBindingFrame, key);
                end
                key = GetControllerCapture();
            end
        end
        publishNames();
        local show = GetCVar("padUI") == "1";
        if show then
            bar:Show();
        else
            bar:Hide();
        end
        if not show or not GetControllerControlCount then
            return;
        end
        local count = GetControllerControlCount();
        for i = 1, 16 do
            local text = slots[i];
            if i <= count then
                local key, label, down = GetControllerControl(i);
                local action = WowDecomp_ControllerAction(key);
                text:SetText((label or "") .. " " .. action);
                if down then
                    text:SetTextColor(0.45, 1, 0.55);
                else
                    text:SetTextColor(1, 0.86, 0.55);
                end
                text:Show();
            else
                text:Hide();
            end
        end
        local gc = GetControllerKind and GetControllerKind() == "gamecube";
        if GetCVar("padLayout") == "2" and GetCVar("padAxisBindings") == "1" then
            hint:SetText(gc and "Stick and C-stick use key bindings." or "Sticks use key bindings.");
        elseif gc then
            hint:SetText("Stick moves. C-stick looks. Z is the shoulder button.");
        else
            hint:SetText("Left stick moves. Right stick looks.");
        end
    end);
    bar:Hide();
end

WowDecomp_InstallControllerUI();
