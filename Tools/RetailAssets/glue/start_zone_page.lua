-- Faction-restricted start-zone page, after Body sliders.
(function()
local SZ = { open = false, rows = {}, entries = {}, zone = 0, built = false, broken = nil };
local INK = { 0.03, 0.04, 0.055 };
local GOLD = { 0.91, 0.77, 0.42 };
local GOLD_DIM = { 0.54, 0.43, 0.18 };
local TEXT = { 0.88, 0.86, 0.80 };
local MUTED = { 0.60, 0.59, 0.55 };
local ZONES = {
    { id = 1, faction = "ALLIANCE", name = "Northshire Valley", region = "Elwynn Forest" },
    { id = 2, faction = "ALLIANCE", name = "Coldridge Valley", region = "Dun Morogh" },
    { id = 3, faction = "ALLIANCE", name = "Shadowglen", region = "Teldrassil" },
    { id = 4, faction = "ALLIANCE", name = "Ammen Vale", region = "Azuremyst Isle" },
    { id = 5, faction = "HORDE", name = "Valley of Trials", region = "Durotar" },
    { id = 6, faction = "HORDE", name = "Camp Narache", region = "Mulgore" },
    { id = 7, faction = "HORDE", name = "Deathknell", region = "Tirisfal Glades" },
    { id = 8, faction = "HORDE", name = "Sunstrider Isle", region = "Eversong Woods" },
};
local HOME = { [1] = 1, [3] = 2, [7] = 2, [4] = 3, [11] = 4, [2] = 5, [8] = 5,
    [6] = 6, [5] = 7, [10] = 8 };

local function RaceFaction()
    if not GetSelectedRace or not GetFactionForRace then return nil; end
    local index = GetSelectedRace();
    local _, faction = GetFactionForRace(index);
    faction = string.upper(tostring(faction or ""));
    if string.find(faction, "HORDE", 1, true) then return "HORDE"; end
    if string.find(faction, "ALLIANCE", 1, true) then return "ALLIANCE"; end
    return nil;
end

local function IsDeathKnight()
    if not CharacterCreate.personalizationMode or not GetSelectedClass then return false; end
    local _, class = GetSelectedClass();
    return class == "DEATHKNIGHT";
end

local function Available()
    return not SZ.broken and SZ.built and CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown()
        and type(SetCreateStartZone) == "function" and RaceFaction() ~= nil and not IsDeathKnight();
end

function StartZonePage_Open()
    return SZ.open and Available();
end

function StartZonePage_Follows()
    return not SZ.open and Available();
end

local function AddText(parent, text, size, color)
    local label = parent:CreateFontString(nil, "OVERLAY", "GlueFontHighlightSmall");
    label:SetFont("Fonts\\FRIZQT__.TTF", size);
    label:SetTextColor(color[1], color[2], color[3]);
    label:SetText(text);
    return label;
end

local function SetSelected(id)
    if not SetCreateStartZone(id) then return; end
    SZ.zone = id;
    for _, row in ipairs(SZ.rows) do
        local selected = row.entry and row.entry.id == id;
        row:SetBackdropColor(INK[1], INK[2], INK[3], selected and 0.98 or 0.72);
        row.title:SetTextColor(selected and GOLD[1] or TEXT[1], selected and GOLD[2] or TEXT[2], selected and GOLD[3] or TEXT[3]);
        row.mark:SetText(selected and "CHOSEN" or "");
    end
    local entry = SZ.entries[1];
    for _, candidate in ipairs(SZ.entries) do
        if candidate.id == id then entry = candidate; break; end
    end
    SZ.detailTitle:SetText(string.upper(entry.name));
    SZ.detailRegion:SetText(entry.region);
    SZ.detailBody:SetText(entry.id == 0
        and "This character begins at the normal starting location assigned to its race."
        or "You begin here with the local opening quests and hearthstone. This option belongs to your faction.");
end

local function Build()
    local panel = CreateFrame("Frame", "StartZonePagePanel", CharacterCreateFrame);
    panel:SetAllPoints(CharacterCreateFrame);
    panel:SetFrameStrata("DIALOG");
    panel:SetFrameLevel(CharacterCreateFrame:GetFrameLevel() + 6);
    panel:EnableMouse(true);
    panel:Hide();
    panel:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8" });
    panel:SetBackdropColor(INK[1], INK[2], INK[3], 0.94);
    SZ.panel = panel;

    local title = AddText(panel, "CHOOSE YOUR BEGINNING", 18, GOLD);
    title:SetPoint("TOPLEFT", panel, "TOPLEFT", 55, -82);
    local subtitle = AddText(panel, "Only starting areas of your faction are available.", 11, MUTED);
    subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8);

    for index = 1, 5 do
        local row = CreateFrame("Button", "StartZonePageRow" .. index, panel);
        row:SetSize(340, 46);
        row:SetPoint("TOPLEFT", panel, "TOPLEFT", 55, -132 - (index - 1) * 54);
        row:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
            tile = true, tileSize = 8, edgeSize = 10, insets = { left = 2, right = 2, top = 2, bottom = 2 } });
        row:SetBackdropColor(INK[1], INK[2], INK[3], 0.72);
        row:SetBackdropBorderColor(GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.8);
        row.title = AddText(row, "", 13, TEXT);
        row.title:SetPoint("TOPLEFT", row, "TOPLEFT", 12, -8);
        row.region = AddText(row, "", 10, MUTED);
        row.region:SetPoint("TOPLEFT", row.title, "BOTTOMLEFT", 0, -3);
        row.mark = AddText(row, "", 10, GOLD);
        row.mark:SetPoint("RIGHT", row, "RIGHT", -12, 0);
        row:SetScript("OnClick", function(self) SetSelected(self.entry.id); end);
        SZ.rows[index] = row;
    end

    local detail = CreateFrame("Frame", "StartZonePageDetail", panel);
    detail:SetSize(340, 300);
    detail:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -55, -132);
    detail:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 8, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } });
    detail:SetBackdropColor(INK[1], INK[2], INK[3], 0.75);
    detail:SetBackdropBorderColor(GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.8);
    SZ.detailTitle = AddText(detail, "", 18, GOLD);
    SZ.detailTitle:SetPoint("TOPLEFT", detail, "TOPLEFT", 18, -20);
    SZ.detailRegion = AddText(detail, "", 12, GOLD_DIM);
    SZ.detailRegion:SetPoint("TOPLEFT", SZ.detailTitle, "BOTTOMLEFT", 0, -8);
    SZ.detailBody = AddText(detail, "", 12, TEXT);
    SZ.detailBody:SetWidth(300);
    SZ.detailBody:SetJustifyH("LEFT");
    SZ.detailBody:SetPoint("TOPLEFT", SZ.detailRegion, "BOTTOMLEFT", 0, -20);
end

local function SZ_Buttons()
    return { CharCreateOkayButton, CharCreateBackButton };
end

-- The panel covers the whole creation frame at DIALOG strata, which hid the Accept (and Back) buttons beneath it.
-- Lift them above the panel while it is open and put them back afterwards.
local function SZ_RaiseButtons()
    if not SZ.panel then return; end
    for _, button in ipairs(SZ_Buttons()) do
        if button then
            if not SZ.saved then SZ.saved = {}; end
            if not SZ.saved[button] then SZ.saved[button] = { button:GetFrameStrata(), button:GetFrameLevel() }; end
            button:SetFrameStrata("DIALOG");
            button:SetFrameLevel(SZ.panel:GetFrameLevel() + 10);
        end
    end
end

function SZ_LowerButtons()
    if not SZ.saved then return; end
    for button, state in pairs(SZ.saved) do
        button:SetFrameStrata(state[1]);
        button:SetFrameLevel(state[2]);
    end
    SZ.saved = nil;
end

local function SetOpen(open)
    if SZ.open == open then return; end
    SZ.open = open;
    if not open then
        SZ.panel:Hide();
        SZ_LowerButtons();
        return;
    end

    local faction = RaceFaction();
    SZ.entries = { { id = 0, name = "Race's usual starting area", region = "Your race's homeland", faction = faction } };
    for _, zone in ipairs(ZONES) do
        if zone.faction == faction then SZ.entries[#SZ.entries + 1] = zone; end
    end
    for index, row in ipairs(SZ.rows) do
        local entry = SZ.entries[index];
        row.entry = entry;
        if entry then
            row.title:SetText(entry.name);
            row.region:SetText(entry.region);
            row:Show();
        else
            row:Hide();
        end
    end

    local race = CharacterCreate.selectedRaceID or 0;
    local selected = GetCreateStartZone and GetCreateStartZone() or 0;
    if selected ~= 0 and (not SZ.entries[1] or not ZONES[selected] or ZONES[selected].faction ~= faction) then
        selected = 0;
    end
    if selected == 0 and HOME[race] and ZONES[HOME[race]].faction == faction then selected = HOME[race]; end
    SZ.panel:Show();
    SetSelected(selected);
    SZ_RaiseButtons();
    if CharCreateOkayButton:GetText() ~= "Accept" then CharCreateOkayButton:SetText("Accept"); end
end

function StartZonePage_Show()
    if not Available() then return false; end
    SetOpen(true);
    return true;
end

local function Hook()
    local okay = CharacterCreate_Okay;
    function CharacterCreate_Okay(...)
        if SZ.open then
            SetCreateStartZone(SZ.zone);
            return okay(...);
        end
        if BodyShapePage_Follows and BodyShapePage_Follows() then
            return okay(...);
        end
        if StartZonePage_Follows() then
            StartZonePage_Show();
            return;
        end
        return okay(...);
    end

    local back = CharacterCreate_Back;
    function CharacterCreate_Back(...)
        if SZ.open then SetOpen(false); return; end
        return back(...);
    end

    local toggle = CharacterCreate_TogglePersonalization;
    function CharacterCreate_TogglePersonalization(...)
        if SZ.open then SetOpen(false); return; end
        return toggle(...);
    end

    local onShow = CharacterCreate_OnShow;
    function CharacterCreate_OnShow(...)
        SZ.open = false;
        SZ.zone = 0;
        if SetCreateStartZone then SetCreateStartZone(0); end
        if SZ.panel then SZ.panel:Hide(); end
        return onShow(...);
    end
end

local function Install()
    if type(SetCreateStartZone) ~= "function" then return; end
    Build();
    Hook();
    SZ.built = true;
    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function()
        if not SZ.open then return; end
        local ok, err = pcall(function()
            if not Available() then SetOpen(false); return; end
            SZ.panel:Show();
            SZ_RaiseButtons();
        end);
        if not ok then
            SZ.broken = tostring(err);
            SZ.open = false;
            if SZ.panel then SZ.panel:Hide(); end
            if SetCreateStartZone then SetCreateStartZone(0); end
        end
    end);
end

local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton) then return; end
    self:SetScript("OnUpdate", nil);
    local ok, err = pcall(Install);
    if not ok then SZ.broken = tostring(err); end
end);
end)();
