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
        sub:SetText("Retail uses the pad as a controller. Steam Input leaves it alone so a Steam config can send keys and mouse.");
    end

    local status = smallFont and panel:CreateFontString("WowDecompControllerStatus", "ARTWORK", smallFont);
    if status and title then
        status:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -36);
        status:SetJustifyH("LEFT");
    end

    local general = CreateFrame("Frame", "WowDecompControllerGeneral", panel, "OptionsBoxTemplate");
    general:SetWidth(188);
    general:SetHeight(118);
    general:SetPoint("TOPLEFT", 8, -84);
    _G[general:GetName() .. "Title"]:SetText("General");

    WowDecomp_ControllerCheckbox(panel, general, "WowDecompControllerEnable", "Enable controller", "Joystick", "Turns the connected gamepad on.", general, 12, -8);
    WowDecomp_ControllerCheckbox(panel, general, "WowDecompControllerCinema", "Use during cinematics", "CinematicJoystick", "Leave the pad active while a cinematic is playing.", general, 12, -36);

    local layoutLabel = smallFont and general:CreateFontString(nil, "ARTWORK", smallFont);
    if layoutLabel then
        layoutLabel:SetPoint("TOPLEFT", general, "TOPLEFT", 16, -68);
        layoutLabel:SetText("Layout");
    end
    local layout = CreateFrame("Frame", "WowDecompControllerLayout", general, "UIDropDownMenuTemplate");
    layout:SetPoint("TOPLEFT", general, "TOPLEFT", 48, -58);
    UIDropDownMenu_SetWidth(110, layout);
    UIDropDownMenu_JustifyText("LEFT", layout);
    panel.layoutDrop = layout;

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
        return value == "1" and "Steam Input" or "Retail layout";
    end

    local function applyLayoutEnabled()
        local retail = panel.layoutValue ~= "1";
        for _, control in ipairs(panel.retail) do
            if control.type == CONTROLTYPE_SLIDER and BlizzardOptionsPanel_Slider_Disable then
                if retail then
                    BlizzardOptionsPanel_Slider_Enable(control);
                else
                    BlizzardOptionsPanel_Slider_Disable(control);
                end
            elseif retail then
                control:Enable();
            else
                control:Disable();
            end
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
                UIDropDownMenu_SetText(layoutText(button.value), layout);
                applyLayoutEnabled();
            end
            UIDropDownMenu_AddButton(info);
        end
        choice("Retail layout", "0");
        choice("Steam Input", "1");
    end);

    local function sync()
        if BlizzardOptionsPanel_Refresh then
            BlizzardOptionsPanel_Refresh(panel);
        end
        panel.layoutValue = GetCVar("padLayout") or "0";
        UIDropDownMenu_SetText(layoutText(panel.layoutValue), layout);
        if status and GetConnectedController then
            local name = GetConnectedController();
            status:SetText(name ~= "" and ("Connected: " .. name) or "Connected: none");
        end
        applyLayoutEnabled();
    end

    panel.okay = function(self)
        if BlizzardOptionsPanel_Okay then
            BlizzardOptionsPanel_Okay(self);
        end
        SetCVar("padLayout", self.layoutValue or "0");
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
        SetCVar("padLayout", "0");
        sync();
    end
    panel.refresh = sync;
    panel:SetScript("OnShow", sync);
    sync();
    InterfaceOptions_AddCategory(panel);
end

WowDecomp_InstallController();
