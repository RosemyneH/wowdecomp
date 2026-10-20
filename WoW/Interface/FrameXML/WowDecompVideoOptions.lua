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
        subText:SetText("Playtest settings. Scroll for atmosphere, distance-field fonts, image effects, and Vulkan shader experiments.");
    end

    local scroll = CreateFrame("ScrollFrame", "VideoOptionsAdvancedPanelScroll", panel);
    scroll:SetPoint("TOPLEFT", panel, "TOPLEFT", 0, -64);
    scroll:SetPoint("BOTTOMRIGHT", panel, "BOTTOMRIGHT", -28, 8);
    scroll:EnableMouseWheel(true);

    local child = CreateFrame("Frame", "VideoOptionsAdvancedPanelScrollChild", scroll);
    child:SetWidth(400);
    child:SetHeight(3200);
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

    local function AtmosSlider(parent, name, label, cvar, tooltip, minValue, maxValue, step, low, high, y)
        local slider = CreateFrame("Slider", name, parent, "OptionsSliderTemplate");
        slider:SetWidth(200);
        slider:SetPoint("TOPLEFT", parent, "TOPLEFT", 16, y);
        slider.type = CONTROLTYPE_SLIDER;
        slider.cvar = cvar;
        slider.tooltipText = tooltip;
        _G[name .. "Low"]:SetText(low);
        _G[name .. "High"]:SetText(high);
        local entry = { text = label, tooltip = tooltip, minValue = minValue, maxValue = maxValue, valueStep = step };
        panel.options[cvar] = entry;

        local function Show(value)
            local snapped = step >= 1 and math.floor(value + 0.5) or math.floor(value / step + 0.5) * step;
            local text = step >= 1 and tostring(math.floor(snapped + 0.5)) or string.format("%.2f", snapped);
            local title = _G[name .. "Text"];
            if title then
                title:SetText(label .. ": " .. text);
            end
            return snapped, text;
        end

        slider:SetScript("OnValueChanged", function(self, value)
            if not self.wowDecompReady then
                return;
            end
            local snapped, text = Show(value);
            BlizzardOptionsPanel_Slider_OnValueChanged(self, snapped);
            SetCVar(cvar, text);
            if VideoOptionsFrameApply then
                VideoOptionsFrameApply:Enable();
            end
        end);
        BlizzardOptionsPanel_RegisterControl(slider, panel);
        WowDecomp_SetupRegisteredControl(slider, panel, entry);
        slider.wowDecompReady = true;
        Show(slider:GetValue());
        return y - 52;
    end

    local function AtmosCheck(parent, name, label, cvar, tooltip, y)
        local button = WowDecomp_RegisterCheckbox(panel, parent, name, label, cvar, tooltip, parent, 12, y, nil);
        button:SetHitRectInsets(0, -200, 0, 0);
        return y - 28;
    end

    local function AtmosBox(anchor, name, title, height)
        local box = CreateFrame("Frame", name, child, "OptionsBoxTemplate");
        box:SetWidth(380);
        box:SetHeight(height);
        box:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", 0, -28);
        _G[name .. "Title"]:SetText(title);
        return box;
    end

    local atmos = AtmosBox(fonts, "VideoOptionsAdvancedPanelAtmosphere", "Atmosphere", 48);
    AtmosCheck(atmos, "VideoOptionsAdvancedPanelAtmos", "Atmosphere", "atmos", "Volumetric light, fog, sun shadows, sun rays, lamps, clouds, water, and grass. Each control below is one feature.", -12);

    local light = AtmosBox(atmos, "VideoOptionsAdvancedPanelAtmosLight", "Light", 320);
    local y = AtmosCheck(light, "VideoOptionsAdvancedPanelAtmosVolume", "Volumetric light", "atmosVolume", "Sunlight gathered in the air. It stays with the world as the camera moves.", -12);
    y = AtmosSlider(light, "VideoOptionsAdvancedPanelAtmosVolumeStrength", "Strength", "atmosVolumeStrength", "How bright the lit air is.", 0, 100, 1, "Off", "Full", y);
    y = AtmosSlider(light, "VideoOptionsAdvancedPanelAtmosVolumeDensity", "Density", "atmosVolumeDensity", "How thick the air is, in thousandths per yard.", 0, 40, 1, "Thin", "Thick", y);
    y = AtmosSlider(light, "VideoOptionsAdvancedPanelAtmosVolumeDistance", "Distance", "atmosVolumeDistance", "How far the light is gathered, in yards.", 20, 300, 10, "Near", "Far", y);
    y = AtmosSlider(light, "VideoOptionsAdvancedPanelAtmosVolumeToward", "Toward sun", "atmosVolumeToward", "How much the air glows when you look toward the sun.", 0, 80, 1, "Even", "Sun", y);
    AtmosSlider(light, "VideoOptionsAdvancedPanelAtmosVolumeQuality", "Quality", "atmosVolumeQuality", "Low, medium, or high. Higher takes more samples.", 1, 3, 1, "Low", "High", y);

    local fog = AtmosBox(light, "VideoOptionsAdvancedPanelAtmosFog", "Fog", 320);
    y = AtmosCheck(fog, "VideoOptionsAdvancedPanelAtmosFogOn", "Fog", "atmosFog", "Fog that sits on the ground and thins as it rises. It uses the zone fog color.", -12);
    y = AtmosSlider(fog, "VideoOptionsAdvancedPanelAtmosFogDensity", "Density", "atmosFogDensity", "How thick the fog is at the ground.", 0, 80, 1, "Clear", "Thick", y);
    y = AtmosSlider(fog, "VideoOptionsAdvancedPanelAtmosFogHeight", "Height", "atmosFogHeight", "Yards the fog takes to thin out upward.", 4, 80, 1, "Low", "High", y);
    y = AtmosSlider(fog, "VideoOptionsAdvancedPanelAtmosFogBright", "Brightness", "atmosFogBrightness", "How much of the sky color the fog carries.", 0, 100, 1, "Dim", "Bright", y);
    y = AtmosSlider(fog, "VideoOptionsAdvancedPanelAtmosFogPatches", "Patches", "atmosFogPatches", "Breaks the fog into drifting patches.", 0, 100, 1, "Even", "Patchy", y);
    AtmosSlider(fog, "VideoOptionsAdvancedPanelAtmosFogWater", "Low ground", "atmosFogWater", "Extra fog in hollows and over water.", 0, 400, 10, "Even", "Thick", y);

    local shadows = AtmosBox(fog, "VideoOptionsAdvancedPanelAtmosShadows", "Shadows", 300);
    y = AtmosCheck(shadows, "VideoOptionsAdvancedPanelAtmosShadowsOn", "Sun shadows", "atmosShadows", "Shadows from the depth of the frame. Trees, walls, and characters in view block the sun.", -12);
    y = AtmosCheck(shadows, "VideoOptionsAdvancedPanelAtmosShadowWorld", "World", "atmosShadowWorld", "Terrain, buildings, and trees cast shadows.", y);
    y = AtmosCheck(shadows, "VideoOptionsAdvancedPanelAtmosShadowUnits", "Characters", "atmosShadowUnits", "Players and creatures cast a tighter shadow.", y);
    y = AtmosSlider(shadows, "VideoOptionsAdvancedPanelAtmosShadowStrength", "Strength", "atmosShadowStrength", "How much light a shaded surface loses.", 0, 100, 1, "Soft", "Deep", y);
    y = AtmosSlider(shadows, "VideoOptionsAdvancedPanelAtmosShadowSoft", "Softness", "atmosShadowSoft", "Wider samples along the shadow edge.", 0, 2, 1, "Hard", "Soft", y);
    AtmosSlider(shadows, "VideoOptionsAdvancedPanelAtmosSunlight", "Sunlight", "atmosSunlight", "Brightens ground the sun reaches.", 0, 50, 1, "Off", "Bright", y);

    local sky = AtmosBox(shadows, "VideoOptionsAdvancedPanelAtmosSky", "Sky", 300);
    y = AtmosCheck(sky, "VideoOptionsAdvancedPanelAtmosRays", "Sun rays", "atmosRays", "Shafts of the bright sky toward the sun. They fade when the sun is behind terrain.", -12);
    y = AtmosSlider(sky, "VideoOptionsAdvancedPanelAtmosRaysStrength", "Ray strength", "atmosRaysStrength", "How strong the shafts are.", 0, 100, 1, "Off", "Full", y);
    y = AtmosCheck(sky, "VideoOptionsAdvancedPanelAtmosNight", "Night", "atmosNight", "Darkens the world at night and leaves lamp light on top.", y);
    y = AtmosSlider(sky, "VideoOptionsAdvancedPanelAtmosNightDark", "Darkness", "atmosNightDarkness", "How dark full night is.", 0, 90, 1, "Dim", "Dark", y);
    y = AtmosSlider(sky, "VideoOptionsAdvancedPanelAtmosNightStrength", "Night light", "atmosNightStrength", "Volumetric light and sun rays at night, as a percent of day.", 0, 100, 1, "Off", "Day", y);
    AtmosCheck(sky, "VideoOptionsAdvancedPanelAtmosNightIndoors", "Indoors", "atmosNightIndoors", "Also darken the night inside buildings.", y);

    local clouds = AtmosBox(sky, "VideoOptionsAdvancedPanelAtmosClouds", "Clouds", 196);
    y = AtmosCheck(clouds, "VideoOptionsAdvancedPanelAtmosCloudsOn", "Clouds", "atmosClouds", "A drifting cloud layer on the sky, in place of empty sky pixels.", -12);
    y = AtmosSlider(clouds, "VideoOptionsAdvancedPanelAtmosCloudCover", "Cover", "atmosCloudCover", "How much of the sky the layer covers.", 0, 100, 1, "Clear", "Full", y);
    y = AtmosSlider(clouds, "VideoOptionsAdvancedPanelAtmosCloudSpeed", "Speed", "atmosCloudSpeed", "How fast the layer drifts.", 0, 40, 1, "Still", "Fast", y);
    AtmosCheck(clouds, "VideoOptionsAdvancedPanelAtmosCloudShapes", "Shapes", "atmosCloudShapes", "A few clouds keep a distinct outline.", y);

    local colorBox = AtmosBox(clouds, "VideoOptionsAdvancedPanelAtmosColor", "Color", 230);
    y = AtmosCheck(colorBox, "VideoOptionsAdvancedPanelAtmosColorOn", "Color", "atmosColor", "Saturation for day and for night.", -12);
    y = AtmosSlider(colorBox, "VideoOptionsAdvancedPanelAtmosDaySat", "Day", "atmosDaySat", "100 is the game's color. 0 is grey.", 0, 200, 5, "Grey", "Vivid", y);
    y = AtmosSlider(colorBox, "VideoOptionsAdvancedPanelAtmosNightSat", "Night", "atmosNightSat", "Saturation at full night.", 0, 200, 5, "Grey", "Vivid", y);
    AtmosSlider(colorBox, "VideoOptionsAdvancedPanelAtmosGlow", "Game glow", "atmosGlow", "The game's full-screen glow, as a percent of its own. 100 leaves it alone.", 0, 200, 5, "Off", "Strong", y);

    local lamps = AtmosBox(colorBox, "VideoOptionsAdvancedPanelAtmosLamps", "Lamps", 320);
    y = AtmosCheck(lamps, "VideoOptionsAdvancedPanelAtmosLampsOn", "Lamps", "atmosLamps", "Torches, lanterns, candles, and other point lights glow in the air and on nearby surfaces.", -12);
    y = AtmosSlider(lamps, "VideoOptionsAdvancedPanelAtmosLampGlow", "Glow", "atmosLampGlow", "How bright a lamp is.", 0, 50, 1, "Dim", "Bright", y);
    y = AtmosSlider(lamps, "VideoOptionsAdvancedPanelAtmosLampCount", "Count", "atmosLampCount", "How many lamps are considered. The nearest 64 are drawn.", 16, 256, 16, "16", "256", y);
    y = AtmosSlider(lamps, "VideoOptionsAdvancedPanelAtmosLampsDay", "By day", "atmosLampsDay", "Lamp strength in daylight, as a percent of night.", 0, 100, 1, "Off", "Night", y);
    y = AtmosCheck(lamps, "VideoOptionsAdvancedPanelAtmosBeacon", "Lighthouses", "atmosBeacon", "A turning beam from tall, far-reaching lights.", y);
    AtmosSlider(lamps, "VideoOptionsAdvancedPanelAtmosBeaconBeam", "Beam", "atmosBeaconBeam", "How bright the lighthouse beam is.", 0, 100, 1, "Off", "Bright", y);

    local grass = AtmosBox(lamps, "VideoOptionsAdvancedPanelAtmosGrass", "Grass", 150);
    y = AtmosCheck(grass, "VideoOptionsAdvancedPanelAtmosGrassOn", "Grass", "atmosGrass", "Ground cover sways in the wind and parts around you.", -12);
    y = AtmosSlider(grass, "VideoOptionsAdvancedPanelAtmosGrassWind", "Wind", "atmosGrassWind", "How hard the grass sways.", 0, 5, 1, "Still", "Hard", y);
    AtmosCheck(grass, "VideoOptionsAdvancedPanelAtmosGrassPart", "Parting", "atmosGrassPart", "Blades lean away from you.", y);

    local water = AtmosBox(grass, "VideoOptionsAdvancedPanelAtmosWater", "Water", 280);
    y = AtmosCheck(water, "VideoOptionsAdvancedPanelAtmosWaterOn", "Water", "atmosWater", "Waves, foam, sun glint, and clearer color on the water surface. Rain adds rings.", -12);
    y = AtmosSlider(water, "VideoOptionsAdvancedPanelAtmosWaterWaves", "Waves", "atmosWaterWaves", "How tall the chop is.", 0, 300, 5, "Flat", "Choppy", y);
    y = AtmosSlider(water, "VideoOptionsAdvancedPanelAtmosWaterFoam", "Foam", "atmosWaterFoam", "White foam on the wave crests.", 0, 100, 1, "Off", "Full", y);
    y = AtmosSlider(water, "VideoOptionsAdvancedPanelAtmosWaterGlint", "Glint", "atmosWaterGlint", "The sun's glint on the waves.", 0, 300, 5, "Off", "Bright", y);
    AtmosSlider(water, "VideoOptionsAdvancedPanelAtmosWaterClarity", "Clarity", "atmosWaterClarity", "How much of the water's own color shows through.", 0, 100, 1, "Deep", "Clear", y);

    local timeBox = AtmosBox(water, "VideoOptionsAdvancedPanelAtmosTime", "Time", 210);
    AtmosCheck(timeBox, "VideoOptionsAdvancedPanelAtmosTimeOn", "On this screen", "atmosTime", "Changes the time of day you see. The server and other players keep the server time.", -12);
    panel.atmosTimeChecks = {};
    local timeChoices = {
        { "VideoOptionsAdvancedPanelAtmosTimeServer", "Server time", "0", -40 },
        { "VideoOptionsAdvancedPanelAtmosTimeComputer", "Computer time", "1", -68 },
        { "VideoOptionsAdvancedPanelAtmosTimeFixed", "Fixed hour", "2", -96 },
    };
    for _, choice in ipairs(timeChoices) do
        local check = CreateFrame("CheckButton", choice[1], timeBox, "VideoOptionsSmallCheckButtonTemplate");
        check.atmosTimeValue = choice[3];
        check:SetPoint("TOPLEFT", timeBox, "TOPLEFT", 16, choice[4]);
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
            SetCVar("atmosTimeMode", self.atmosTimeValue);
            if VideoOptionsFrameApply then
                VideoOptionsFrameApply:Enable();
            end
            if WowDecomp_RefreshAtmosphereTime then
                WowDecomp_RefreshAtmosphereTime();
            end
        end);
        panel.atmosTimeChecks[#panel.atmosTimeChecks + 1] = check;
    end
    AtmosSlider(timeBox, "VideoOptionsAdvancedPanelAtmosTimeHour", "Hour", "atmosTimeHour", "The hour used when Fixed hour is chosen.", 0, 24, 0.25, "0", "24", -124);

    function WowDecomp_RefreshAtmosphereTime()
        if not panel.atmosTimeChecks then
            return;
        end
        local mode = GetCVar("atmosTimeMode") or "0";
        for _, check in ipairs(panel.atmosTimeChecks) do
            check:SetChecked(check.atmosTimeValue == mode);
        end
    end

    local previousRefresh = panel.refresh;
    panel.refresh = function(self)
        if previousRefresh then
            previousRefresh(self);
        end
        WowDecomp_RefreshMsdfStatus();
        if WowDecomp_RefreshAtmosphereTime then
            WowDecomp_RefreshAtmosphereTime();
        end
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
        if WowDecomp_RefreshAtmosphereTime then
            WowDecomp_RefreshAtmosphereTime();
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
