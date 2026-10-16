local panel = CreateFrame("Frame", "ExtendedViewOptionsPanel");
panel.name = "Extended View (Playtest)";

local function SetCVar01(name, checked)
    SetCVar(name, checked and "1" or "0");
end

local function AddCheck(parent, label, cvar, y)
    local box = CreateFrame("CheckButton", nil, parent, "UICheckButtonTemplate");
    box:SetPoint("TOPLEFT", 16, y);
    box:SetScript("OnShow", function(self)
        self:SetChecked(GetCVar(cvar) == "1");
    end);
    box:SetScript("OnClick", function(self)
        SetCVar01(cvar, self:GetChecked());
    end);
    local text = parent:CreateFontString(nil, "ARTWORK", "GameFontHighlight");
    text:SetPoint("LEFT", box, "RIGHT", 4, 0);
    text:SetText(label);
    return box;
end

panel:SetScript("OnShow", function(self)
    if not self.initialized then
        self.initialized = true;
        local title = self:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge");
        title:SetPoint("TOPLEFT", 16, -16);
        title:SetText("Extended view (off by default)");

        local blurb = self:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall");
        blurb:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8);
        blurb:SetWidth(500);
        blurb:SetJustifyH("LEFT");
        blurb:SetText(
            "Enable Extended View, then raise View Distance on the Graphics tab (up to 10000). "
                .. "Extended horizon Z allows horizonFarclipScale up to 48 (/console horizonFarclipScale). "
                .. "Retail limits apply when these are off."
        );

        AddCheck(self, "Extended view distance (unlock 10k farclip)", "extendedView", -72);
        AddCheck(self, "Extended horizon Z clip (map-wide projection)", "extendedHorizonZClip", -100);
        AddCheck(self, "Distance fog", "distanceFog", -128);
    end
end);

if InterfaceOptions_AddCategory then
    InterfaceOptions_AddCategory(panel);
end
