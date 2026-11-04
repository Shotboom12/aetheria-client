-- Waystone HD: single settings menu (/hd). English edition for Aetheria, 4 Nov 2026 (from Mado's 3 Nov 2026 French panel).
-- Left column = topics; on the right a few simple lists, plus + sections for fine settings. For every effect that exists in several versions
-- (For Ever, Lumenveil, Seyris) a drop-down switches between them or mixes them.
-- Each control simply writes a console variable (like typing /console name value).
-- A variable that does not exist in this build greys its control out instead of raising an error.
-- The "Upscaling" page drives Aetheria's own variables: hdFsr, hdFsrQuality, hdFsrSharpness, hdFsrReactive, hdFsrUnits, hdFsrObjectMotion,
-- hdDlss, hdDlssSharpness and the read-only status variables hdFsrInfo, hdDlssInfo, hdFsrMotionInfo.

local function Read(name) local ok, v = pcall(GetCVar, name); if ok then return v end end
local function Num(name, fallback) return tonumber(Read(name) or "") or fallback end
local function Set(name, value) pcall(SetCVar, name, tostring(value)) end
local function Has(name) return Read(name) ~= nil end
local function On(name) return Num(name, 0) ~= 0 end
local function SetMany(t) for i = 1, #t, 2 do if Has(t[i]) then Set(t[i], t[i + 1]) end end end

local COLUMN, ROWSLIDER, ROWCHECK = 285, 46, 26
local panel, pages, current, counter, updating = nil, {}, nil, 0, false
local openList = nil

local function Gold(fs) fs:SetTextColor(1, 0.82, 0) end
local function Grey(fs) fs:SetTextColor(0.5, 0.5, 0.5) end


-- ===================================================================== page builder
-- A page = stacked sections. The first (untitled) one is always visible;
-- the others have a clickable title (+ / -) that opens or closes them.
local function NewPage(key, title, parent, build)
    local page = { key = key, title = title, items = {}, sections = {}, y = 0, col = 0, rowH = 0, built = false }
    page.build = build
    page.Layout = function(self)
        local off = 0
        for _, s in ipairs(self.sections) do
            if s.header then s.header:SetPoint("TOPLEFT", self.root, "TOPLEFT", 4, -off); off = off + 30 end
            if s.open then
                s.body:ClearAllPoints()
                s.body:SetPoint("TOPLEFT", self.root, "TOPLEFT", 0, -off)
                s.body:Show(); off = off + s.height + 6
            else s.body:Hide() end
        end
        self.root:SetHeight(off + 16)
    end
    page.Flush = function(self)
        if self.col ~= 0 then self.y = self.y - self.rowH; self.col = 0; self.rowH = 0 end
    end
    -- closes the current section and opens another. open = visible at the start
    page.Section = function(self, text, open)
        if self.cur then self:Flush(); self.cur.height = -self.y end
        counter = counter + 1
        local sec = { open = open and true or false, height = 0 }
        sec.body = CreateFrame("Frame", nil, self.root)
        sec.body:SetWidth(570); sec.body:SetHeight(10)
        if text then
            local b = CreateFrame("Button", "WaystoneHDSec" .. counter, self.root, "UIPanelButtonTemplate")
            b:SetWidth(556); b:SetHeight(24)
            local function Label() b:SetText((sec.open and "-  " or "+  ") .. text) end
            Label()
            b:SetScript("OnClick", function()
                sec.open = not sec.open; Label(); self:Layout()
                if self.scroll.UpdateScrollChildRect then self.scroll:UpdateScrollChildRect() end
            end)
            sec.header = b
        end
        table.insert(self.sections, sec)
        self.cur, self.content, self.y, self.col, self.rowH = sec, sec.body, 0, 0, 0
    end
    page.Build = function(self)
        counter = counter + 1
        local scroll = CreateFrame("ScrollFrame", "WaystoneHDScroll" .. counter, parent, "UIPanelScrollFrameTemplate")
        scroll:SetPoint("TOPLEFT", 172, -48)
        scroll:SetPoint("BOTTOMRIGHT", -34, 16)
        local root = CreateFrame("Frame", nil, scroll)
        root:SetWidth(570); root:SetHeight(10)
        scroll:SetScrollChild(root)
        self.scroll, self.root = scroll, root
        self.built = true
        self:Section(nil, true)
        self.build(self)
        self:Flush(); self.cur.height = -self.y
        self:Layout()
        scroll:Hide()
    end
    -- half = placed in one of two columns; otherwise full width
    page.Place = function(self, frame, h, half, dy)
        local x
        if half then
            x = self.col * COLUMN
            if h > self.rowH then self.rowH = h end
            frame:SetPoint("TOPLEFT", self.content, "TOPLEFT", x + 4, self.y - (dy or 0))
            self.col = self.col + 1
            if self.col == 2 then self.y = self.y - self.rowH; self.col = 0; self.rowH = 0 end
        else
            self:Flush()
            frame:SetPoint("TOPLEFT", self.content, "TOPLEFT", 4, self.y - (dy or 0))
            self.y = self.y - h
        end
    end
    page.Header = function(self, text)
        self:Flush(); self.y = self.y - 8
        local fs = self.content:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
        fs:SetText(text)
        fs:SetPoint("TOPLEFT", self.content, "TOPLEFT", 4, self.y)
        self.y = self.y - 26
    end
    page.Note = function(self, text)
        self:Flush()
        local fs = self.content:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
        fs:SetWidth(556); fs:SetJustifyH("LEFT"); fs:SetText(text)
        fs:SetPoint("TOPLEFT", self.content, "TOPLEFT", 6, self.y)
        local lines = math.max(1, math.ceil(string.len(text) / 105))
        self.y = self.y - (lines * 13 + 6)
    end
    page.Space = function(self, h) self:Flush(); self.y = self.y - (h or 10) end

    -- check box. def = { cvar, label, tip, get (opt), on (opt) }
    page.Check = function(self, def)
        counter = counter + 1
        local name = "WaystoneHDCheck" .. counter
        local c = CreateFrame("CheckButton", name, self.content, "UICheckButtonTemplate")
        c:SetWidth(24); c:SetHeight(24)
        local label = _G[name .. "Text"]
        label:SetText(def.label)
        c:SetScript("OnClick", function(btn)
            if updating then return end
            if def.on then def.on(btn:GetChecked() and true or false) else Set(def.cvar, btn:GetChecked() and 1 or 0) end
        end)
        if def.tip then
            c:SetScript("OnEnter", function(btn) GameTooltip:SetOwner(btn, "ANCHOR_RIGHT"); GameTooltip:SetText(def.tip, 1, 1, 1, 1, true); GameTooltip:Show() end)
            c:SetScript("OnLeave", function() GameTooltip:Hide() end)
        end
        self:Place(c, ROWCHECK, true)
        table.insert(self.items, { refresh = function()
            local available = def.get and true or Has(def.cvar)
            local checked = def.get and def.get() or (not def.get and On(def.cvar))
            c:SetChecked(checked and true or false)
            if available then c:Enable(); Gold(label) else c:Disable(); Grey(label) end
        end })
    end

    -- slider. def = { cvar, label, min, max, step, default, release, forever }
    page.Slider = function(self, def)
        counter = counter + 1
        local name = "WaystoneHDSlider" .. counter
        local s = CreateFrame("Slider", name, self.content, "OptionsSliderTemplate")
        s:SetWidth(210)
        s:SetMinMaxValues(def.min, def.max); s:SetValueStep(def.step)
        _G[name .. "Text"]:SetText(def.label)
        _G[name .. "Low"]:SetText(def.min); _G[name .. "High"]:SetText(def.max)
        local value = self.content:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
        value:SetPoint("LEFT", s, "RIGHT", 8, 0)
        local function Show()
            local v = s:GetValue()
            if def.cvar == "hdTime" and v < 0 then value:SetText("real time")
            elseif def.cvar == "hdTime" then local h = math.floor(v); value:SetText(string.format("%d:%02d", h, math.floor((v - h) * 60 + 0.5)))
            elseif def.step >= 1 then value:SetText(string.format("%d", v))
            else value:SetText(string.format("%.2f", v)) end
        end
        local function Send()
            local v = math.floor(s:GetValue() / def.step + 0.5) * def.step
            if def.cvar == "hdTime" and v < 0 then v = -1 end
            Set(def.cvar, v)
        end
        s.dragging = false
        s:SetScript("OnValueChanged", function() Show(); if not updating and not def.release and Has(def.cvar) then Send() end end)
        s:SetScript("OnMouseDown", function() s.dragging = true end)
        s:SetScript("OnMouseUp", function() s.dragging = false; if not updating and def.release and Has(def.cvar) then Send() end end)
        self:Place(s, ROWSLIDER, true, 16)
        local item = { def = def, refresh = function()
            if s.dragging then return end
            local available = Has(def.cvar)
            s:SetValue(Num(def.cvar, def.default or def.min)); Show()
            if available then s:Enable(); Gold(_G[name .. "Text"]) else s:Disable(); Grey(_G[name .. "Text"]) end
        end }
        table.insert(self.items, item)
        if def.forever then pages.foreverSliders = pages.foreverSliders or {}; table.insert(pages.foreverSliders, def) end
    end

    -- numeric input box
    page.Input = function(self, def)
        counter = counter + 1
        local label = self.content:CreateFontString(nil, "ARTWORK", "GameFontNormalSmall")
        label:SetText(def.label)
        local box = CreateFrame("EditBox", "WaystoneHDInput" .. counter, self.content, "InputBoxTemplate")
        box:SetWidth(70); box:SetHeight(20); box:SetAutoFocus(false); box:SetNumeric(true)
        box:SetScript("OnEnterPressed", function(b) Set(def.cvar, tostring(tonumber(b:GetText()) or 0)); b:ClearFocus() end)
        box:SetScript("OnEscapePressed", function(b) b:ClearFocus() end)
        self:Flush()
        label:SetPoint("TOPLEFT", self.content, "TOPLEFT", 6, self.y)
        box:SetPoint("TOPLEFT", self.content, "TOPLEFT", 12, self.y - 16)
        self.y = self.y - 44
        table.insert(self.items, { refresh = function()
            if not box:HasFocus() then box:SetText(tostring(Num(def.cvar, 0))) end
        end })
    end

    page.Button = function(self, text, width, onClick)
        counter = counter + 1
        local b = CreateFrame("Button", "WaystoneHDButton" .. counter, self.content, "UIPanelButtonTemplate")
        b:SetWidth(width or 260); b:SetHeight(24); b:SetText(text)
        b:SetScript("OnClick", function() onClick(); if current then current:Refresh() end end)
        self:Place(b, 30, false)
    end

    -- live status text. fn() returns a string
    page.Info = function(self, fn, lines)
        local fs = self.content:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
        fs:SetWidth(556); fs:SetJustifyH("LEFT")
        self:Flush()
        fs:SetPoint("TOPLEFT", self.content, "TOPLEFT", 6, self.y)
        self.y = self.y - ((lines or 2) * 13 + 8)
        table.insert(self.items, { refresh = function() fs:SetText(fn()) end })
    end

    -- drop-down list. def = { label, options = { {text, apply, avail} }, current = function() -> index (0 = mixed) }
    page.Drop = function(self, def)
        counter = counter + 1
        local label = self.content:CreateFontString(nil, "ARTWORK", "GameFontNormal")
        label:SetText(def.label)
        self:Flush()
        label:SetPoint("TOPLEFT", self.content, "TOPLEFT", 6, self.y)
        local button = CreateFrame("Button", "WaystoneHDDrop" .. counter, self.content, "UIPanelButtonTemplate")
        button:SetWidth(400); button:SetHeight(24)
        button:SetPoint("TOPLEFT", self.content, "TOPLEFT", 12, self.y - 18)
        self.y = self.y - 52
        local item = {}
        item.refresh = function()
            local i = def.current()
            if i and i > 0 and def.options[i] then button:SetText(def.options[i].text .. "   v")
            else button:SetText((def.unknown or "Custom (mixed settings)") .. "   v") end
        end
        button:SetScript("OnClick", function()
            if openList and openList.owner == button then openList:Hide(); return end
            if openList then openList:Hide() end
            counter = counter + 1
            local list = CreateFrame("Frame", "WaystoneHDList" .. counter, UIParent)
            list.owner = button
            list:SetFrameStrata("TOOLTIP")
            list:SetWidth(404); list:SetHeight(#def.options * 24 + 10)
            list:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
                edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", tile = true, tileSize = 16, edgeSize = 14,
                insets = { left = 4, right = 4, top = 4, bottom = 4 } })
            list:SetPoint("TOPLEFT", button, "BOTTOMLEFT", 0, 0)
            for i, o in ipairs(def.options) do
                local b = CreateFrame("Button", nil, list, "UIPanelButtonTemplate")
                b:SetWidth(392); b:SetHeight(22); b:SetPoint("TOPLEFT", 6, -5 - (i - 1) * 24)
                local ok = (not o.avail) or o.avail()
                b:SetText(ok and o.text or (o.text .. "  (unavailable)"))
                if not ok then b:Disable() end
                b:SetScript("OnClick", function() o.apply(); list:Hide(); openList = nil; if current then current:Refresh() end end)
            end
            list:SetScript("OnHide", function(l) if openList == l then openList = nil end end)
            openList = list
            list:Show()
        end)
        table.insert(self.items, item)
    end

    -- button that resets the page: all its sliders to their start value + the listed settings (cvar, value, ...)
    page.Reset = function(self, extra)
        self:Button("Reset this page", 260, function()
            for _, it in ipairs(self.items) do
                if it.def and it.def.default ~= nil and Has(it.def.cvar) then Set(it.def.cvar, it.def.default) end
            end
            SetMany(extra)
        end)
    end

    page.Refresh = function(self)
        updating = true
        for _, it in ipairs(self.items) do it.refresh() end
        updating = false
    end
    return page
end

-- ===================================================================== the pages
local ADVANCED = {
 {"hdLvCfg_Banks","Banks",0,2,1,1},
 {"hdLvCfg_Density","Density",0,10,0.05,1},
 {"hdLvCfg_FogCeiling","FogCeiling",0,300,0.1,40},
 {"hdLvCfg_Haze","Haze",0,10,0.05,1},
 {"hdLvCfg_GroundFog","GroundFog",0,10,0.05,0.600000024},
 {"hdLvCfg_FarFog","FarFog",0,10,0.05,1},
 {"hdLvCfg_FarFogStretch","FarFogStretch",0,1,0.005,1},
 {"hdLvCfg_AerialPerspective","AerialPerspective",0,1,0.005,1},
 {"hdLvCfg_SunScatter","SunScatter",0,10,0.05,1},
 {"hdLvCfg_Ambient","Ambient",0,10,0.05,1},
 {"hdLvCfg_HazeFromSky","HazeFromSky",0,1,0.005,1},
 {"hdLvCfg_HazeSkyLevel","HazeSkyLevel",0.25,1.5,0.00625,0.850000024},
 {"hdLvCfg_Shafts","Shafts",0,4,0.02,1},
 {"hdLvCfg_LowSunShafts","LowSunShafts",0,3,0.015,1},
 {"hdLvCfg_ShaftGlare","ShaftGlare",0.0199999996,4,0.019900000002,0.150000006},
 {"hdLvCfg_ShaftFocus","ShaftFocus",0,0.949999988,0.00474999994,0.699999988},
 {"hdLvCfg_MoonShafts","MoonShafts",0,8,0.04,2},
 {"hdLvCfg_MoonShaftFocus","MoonShaftFocus",0,0.949999988,0.00474999994,0.5},
 {"hdLvCfg_ShaftHaze","ShaftHaze",0,10,0.05,1},
 {"hdLvCfg_ShaftRange","ShaftRange",30,600,0.1,200},
 {"hdLvCfg_ShaftLength","ShaftLength",10,300,0.1,250},
 {"hdLvCfg_ShaftDepth","ShaftDepth",0.100000001,4,0.019499999995,1.5},
 {"hdLvCfg_Drift","Drift",0,2,0.01,0.600000024},
 {"hdLvCfg_DriftSize","DriftSize",10,300,0.1,60},
 {"hdLvCfg_DriftDetail","DriftDetail",0,1,0.005,0.649999976},
 {"hdLvCfg_DriftSwirl","DriftSwirl",0,3,0.015,1},
 {"hdLvCfg_BankCover","BankCover",0,1,0.005,0.550000012},
 {"hdLvCfg_BankHeight","BankHeight",5,300,0.1,60},
 {"hdLvCfg_BankPool","BankPool",0,3,0.015,1},
 {"hdLvCfg_BankShadow","BankShadow",0,3,0.015,1},
 {"hdLvCfg_WindSpeed","WindSpeed",0,10,0.05,2},
 {"hdLvCfg_StormWind","StormWind",0,20,0.1,7},
 {"hdLvCfg_WindDirection","WindDirection",0,360,0.1,60},
 {"hdLvCfg_MorningMist","MorningMist",0,8,0.04,2},
 {"hdLvCfg_MistHeight","MistHeight",2,60,0.1,12},
 {"hdLvCfg_EveningHaze","EveningHaze",0,2,0.01,0.400000006},
 {"hdLvCfg_WeatherFog","WeatherFog",0,4,0.02,1},
 {"hdLvCfg_WeatherShafts","WeatherShafts",0,1,0.005,0.349999994},
 {"hdLvCfg_Twilight","Twilight",0,2,0.01,1},
 {"hdLvCfg_DustHaze","DustHaze",0,3,0.015,1},
 {"hdLvCfg_HumidHaze","HumidHaze",0,3,0.015,1},
 {"hdLvCfg_HorizonFog","HorizonFog",0,1,0.005,1},
 {"hdLvCfg_CloudShadows","CloudShadows",0,1,0.005,0.600000024},
 {"hdLvCfg_CloudCover","CloudCover",0,2,0.01,1},
 {"hdLvCfg_CloudSize","CloudSize",100,2000,0.1,400},
 {"hdLvCfg_FarLights","FarLights",0,4,0.02,1},
 {"hdLvCfg_LampFog","LampFog",0,4,0.02,1},
 {"hdLvCfg_LampFlicker","LampFlicker",0,2,0.01,1},
 {"hdLvCfg_FireStrength","FireStrength",0,2,0.01,1},
 {"hdLvCfg_LampIndoors","LampIndoors",0,2,0.01,1},
 {"hdLvCfg_LampLight","LampLight",0,4,0.02,2},
 {"hdLvCfg_LampDayLight","LampDayLight",0,1,0.005,0.349999994},
 {"hdLvCfg_LampShadows","LampShadows",0,1,0.005,1},
 {"hdLvCfg_WindowLight","WindowLight",0,4,0.02,2},
 {"hdLvCfg_EffectLight","EffectLight",0,4,0.02,1},
 {"hdLvCfg_LanternLight","LanternLight",0,4,0.02,1},
 {"hdLvCfg_LanternHalo","LanternHalo",0,2,0.01,0.300000012},
 {"hdLvCfg_LanternSwing","LanternSwing",0,2,0.01,1},
}


local function FogMode()
    local fe, lv = On("hdFog"), Has("hdLvFog") and On("hdLvFog")
    if not fe and not lv then return 1 elseif fe and not lv then return 2 elseif lv and not fe then return 3 end
    return 4
end

local function LampMode()
    local lights, classes = Num("hdLights", 1), Num("hdLvLampClasses", 4)
    if not (Has("hdLvLamps") and On("hdLvLamps")) then
        if lights == 0 then return 1 elseif lights == 1 then return 2 end
        return 0
    end
    local forever = Num("hdLvLampForever", 1) ~= 0
    if lights == 0 and classes == 30 then return 3 end
    if lights == 1 and classes == 4 and forever then return 4 end
    if lights == 1 and classes == 28 and not forever then return 5 end
    return 0
end

local function ClassBit(bit)
    return math.floor(Num("hdLvLampClasses", 4) / bit) % 2 == 1
end
local function SetClassBit(bit, on)
    local v = Num("hdLvLampClasses", 4)
    local has = math.floor(v / bit) % 2 == 1
    if on and not has then v = v + bit elseif not on and has then v = v - bit end
    Set("hdLvLampClasses", v)
end

-- Upscaling (Aetheria's own variables): 1 none, 2 FSR 3.1, 3 DLSS
local function UpscalerMode()
    if Has("hdDlss") and On("hdDlss") then return 3 end
    if Has("hdFsr") and On("hdFsr") then return 2 end
    return 1
end
local function QualityMode() return math.min(4, math.max(1, math.floor(Num("hdFsrQuality", 1)))) end

local function Line(label, cvar) return label .. ": " .. (Read(cvar) or "not available in this version") end

local function Gm(text) if SendChatMessage then pcall(SendChatMessage, text, "SAY") end end
local function LanternMode() return math.min(2, math.max(0, math.floor(Num("hdLvLantern", 0)))) + 1 end
local function FourWay(fe, lv)
    return function()
        local a, b = On(fe), Has(lv) and On(lv)
        if not a and not b then return 1 elseif a and not b then return 2 elseif b and not a then return 3 end
        return 4
    end
end


-- ===================================================================== "My settings"
-- A personal preset: the "My settings" button restores everything in the list below; the "Save" button re-reads the values from the
-- game and keeps them in WaystoneHDMine (WTF\...\SavedVariables\WaystoneHD.lua), so they survive a restart. Until something is saved,
-- it is MINE_DEFAULT (the recommended settings of 3 Nov). hdTime is not in it: it is the game's time of day, not an image setting.
local MINE_DEFAULT = {
    "farclip", "2500",
    "specular", "1",
    "groundEffectDensity", "128",
    "projectedTextures", "1",
    "textureFilteringMode", "5",
    "componentTextureLevel", "9",
    "shadowLevel", "0",
    "environmentDetail", "1.5",
    "extShadowQuality", "5",
    "weatherDensity", "3",
    "groundEffectDist", "280",
    "particleDensity", "1",
    "shadowMode", "3",
    "shadowTextureSize", "2048",
    "ambientOcclusion", "1",
    "postBloom", "1",
    "aerialFog", "1",
    "hdForever", "1",
    "hdFogGround", "2",
    "hdFogHorizon", "1",
    "hdFogTest", "7",
    "hdSunPath", "1",
    "hdGrade", "1",
    "hdLightHorizon", "0.75",
    "hdLightGround", "0.8",
    "hdWater", "1",
    "hdWaterWaves", "0",
    "hdGI", "1",
    "hdContactShadow", "1",
    "hdCataWater", "1",
    "hdAONormals", "1",
    "hdCataViewDistance", "1",
    "hdCataViewQuality", "2",
    "hdSunShadows", "1",
    "hdRays", "1",
    "hdCataHorizonVerify", "0",
    "hdCataRenderPerf", "0",
    "hdLvLampForever", "0",
    "hdAO", "1",
    "hdLvSpellGround", "1",
    "hdLvLampScan", "0",
    "hdLvSpellGain", "4",
    "hdLvLamps", "1",
    "hdLvFog", "1",
    "hdLvHalos", "1",
    "hdLvSpells", "1",
    "hdLvTerrain", "1",
    "hdLvLampClasses", "28",
    "hdLvCfg_FarLights", "0.3",
    "hdLvHaloGain", "0.6",
    "hdBloom", "1",
    "hdGrassBuffers", "4096",
    "hdLvCfg_ShaftGlare", "0.05",
    "hdLvTemporal", "0.85",
    "hdBloomStrength", "0",
    "hdBloomThreshold", "1",
    "hdLvShaftGain", "0.35",
    "hdFog", "0",
    -- 3 Nov 2026 20:18: recommended values
    "hdLvCfg_SunScatter", "1",
    "hdLvCfg_ViewLight", "1",
    "hdLvCfg_AntiGlare", "1",
    "hdLvFogLog", "0",
}

local function MineList()
    if type(WaystoneHDMine) == "table" and #WaystoneHDMine >= 2 then return WaystoneHDMine, true end
    return MINE_DEFAULT, false
end
local function MineApply()
    local list = MineList()
    SetMany(list)
end
-- settings added later: "Save" keeps them too, even if they are not in the starting list
local MINE_TRACKED = { "hdLvCfg_ViewLight", "hdLvCfg_SunScatter", "hdLvCfg_AntiGlare", "hdLvCfg_ScatterDawn",
    "hdLvCfg_ScatterDay", "hdLvCfg_ScatterDusk", "hdLvCfg_ScatterNight", "hdLvSpellSource", "hdLvMissileLight",
    "hdLvMissileReach", "hdLvMissileImpact", "hdLvShafts" }
local function MineSave()
    local list = MineList()
    local out, n, seen = {}, 0, {}
    local function keep(name)
        if seen[name] then return end
        seen[name] = true
        local v = Read(name)
        if v ~= nil then out[#out + 1] = name; out[#out + 1] = v; n = n + 1 end
    end
    for i = 1, #list, 2 do keep(list[i]) end
    for i = 1, #MINE_DEFAULT, 2 do keep(MINE_DEFAULT[i]) end
    for _, name in ipairs(MINE_TRACKED) do keep(name) end
    WaystoneHDMine = out
    return n
end
local function MineCount()
    local list, saved = MineList()
    return math.floor(#list / 2), saved
end

local PAGEDEFS = {
{ "home", "Home", function(p)
    p:Header("Waystone HD")
    p:Note("One topic per tab. The drop-down lists let you choose the For Ever, Lumenveil or Seyris version. Sections marked + open for fine settings.")
    p:Header("Two complete looks")
    p:Button("All For Ever (my graphics)", 340, function()
        SetMany({ "hdLvFog", 0, "hdLvShafts", 0, "hdLvMemory", 0, "hdLvHalos", 0, "hdLvTerrain", 0, "hdLvLamps", 0,
                  "hdLvSpells", 0, "hdLvLantern", 0, "hdLvAmbiance", 0, "hdSxWind", 0, "hdLvLampClasses", 4,
                  "hdFog", 1, "hdRays", 1, "hdBloom", 1, "hdShadowSun", 1, "hdLights", 1, "hdGrassWind", 1 })
    end)
    p:Button("All Lumenveil (For Ever off)", 340, function()
        SetMany({ "hdFog", 0, "hdRays", 0, "hdBloom", 0, "hdShadowSun", 0, "hdLights", 0,
                  "hdLvFog", 1, "hdLvShafts", 1, "hdLvMemory", 1, "hdLvHalos", 1, "hdLvTerrain", 1, "hdLvLamps", 1,
                  "hdLvLampClasses", 30, "hdLvSpells", 1, "hdLvLantern", 1, "hdSxWind", 1, "hdGrassWind", 1 })
    end)
    p:Note("Changes neither FSR/DLSS, nor the water, nor the view distance.")
    p:Header("Back to the original values")
    p:Button("For Ever values (original sliders and switches)", 340, function()
        for _, def in ipairs(pages.foreverSliders or {}) do if Has(def.cvar) then Set(def.cvar, def.default) end end
        SetMany({ "hdForeverForce", 0, "hdGradeDebug", 0, "hdBloomDebug", 0, "hdWmoLight", 1, "hdWater", 1, "hdGrassLight", 1, "hdWaterReflect", 1 })
    end)
    p:Header("My settings")
    p:Button("My settings (put everything back the way I like it)", 340, function() MineApply() end)
    p:Button("Save the current settings into this button", 340, function()
        local n = MineSave()
        if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage("|cffffd200Waystone HD|r: " .. n .. " settings kept in \"My settings\".") end
    end)
    p:Info(function()
        local n, saved = MineCount()
        return (saved and "Your saved settings: " or "Starting settings (3 Nov): ") .. n .. " values. The game's time of day (hdTime) is not part of them."
    end, 2)
    p:Section("Live status", false)
    p:Info(function()
        return Line("Fog", "hdFogInfo") .. "\n" .. Line("Ground", "hdTerrainInfo") .. "\n" .. Line("Water", "hdWaterInfo")
            .. "\n" .. Line("Lumenveil", "hdLvInfo") .. "\n" .. Line("Lumenveil lamps", "hdLvLampInfo")
            .. "\n" .. Line("Seyris grass", "hdSxInfo")
            .. "\n" .. Line("Upscaling", (Has("hdDlss") and On("hdDlss")) and "hdDlssInfo" or "hdFsrInfo")
            .. "\n" .. Line("Object motion", "hdFsrMotionInfo")
    end, 8)
end },

{ "ambiance", "Atmosphere", function(p)
    p:Header("Atmosphere")
    p:Slider({ cvar = "hdTime", label = "Time of day (-1 = real time)", min = -1, max = 24, step = 0.05, default = -1, forever = true })
    p:Drop({ label = "Fog", current = FogMode, options = {
        { text = "Original WotLK (no HD fog)", apply = function() SetMany({ "hdFog", 0, "hdLvFog", 0, "hdLvShafts", 0, "hdLvMemory", 0 }) end },
        { text = "For Ever only", apply = function() SetMany({ "hdFog", 1, "hdLvFog", 0, "hdLvShafts", 0, "hdLvMemory", 0 }) end },
        { text = "Lumenveil only", apply = function() SetMany({ "hdFog", 0, "hdLvFog", 1 }) end, avail = function() return Has("hdLvFog") end },
        { text = "For Ever + Lumenveil together", apply = function() SetMany({ "hdFog", 1, "hdLvFog", 1 }) end, avail = function() return Has("hdLvFog") end },
    } })
    p:Drop({ label = "Sun rays", current = FourWay("hdRays", "hdLvShafts"), options = {
        { text = "None", apply = function() SetMany({ "hdRays", 0, "hdLvShafts", 0 }) end },
        { text = "For Ever only", apply = function() SetMany({ "hdRays", 1, "hdLvShafts", 0 }) end },
        { text = "Lumenveil only (turns on its fog)", apply = function() SetMany({ "hdRays", 0, "hdLvShafts", 1, "hdLvFog", 1 }) end, avail = function() return Has("hdLvShafts") end },
        { text = "Both together", apply = function() SetMany({ "hdRays", 1, "hdLvShafts", 1, "hdLvFog", 1 }) end, avail = function() return Has("hdLvShafts") end },
    } })
    p:Drop({ label = "Glow (bloom)", current = FourWay("hdBloom", "hdLvHalos"), options = {
        { text = "None", apply = function() SetMany({ "hdBloom", 0, "hdLvHalos", 0 }) end },
        { text = "For Ever only", apply = function() SetMany({ "hdBloom", 1, "hdLvHalos", 0 }) end },
        { text = "Lumenveil only (turns on its fog)", apply = function() SetMany({ "hdBloom", 0, "hdLvHalos", 1, "hdLvFog", 1 }) end, avail = function() return Has("hdLvHalos") end },
        { text = "Both together", apply = function() SetMany({ "hdBloom", 1, "hdLvHalos", 1, "hdLvFog", 1 }) end, avail = function() return Has("hdLvHalos") end },
    } })
    p:Drop({ label = "Distant shadows", current = FourWay("hdShadowSun", "hdLvTerrain"), options = {
        { text = "None", apply = function() SetMany({ "hdShadowSun", 0, "hdLvTerrain", 0 }) end },
        { text = "For Ever only", apply = function() SetMany({ "hdShadowSun", 1, "hdLvTerrain", 0 }) end },
        { text = "Lumenveil only (turns on its fog)", apply = function() SetMany({ "hdShadowSun", 0, "hdLvTerrain", 1, "hdLvFog", 1 }) end, avail = function() return Has("hdLvTerrain") end },
        { text = "Both together", apply = function() SetMany({ "hdShadowSun", 1, "hdLvTerrain", 1, "hdLvFog", 1 }) end, avail = function() return Has("hdLvTerrain") end },
    } })
    p:Drop({ label = "Weather (GM command sent to the server)", unknown = "Choose a weather...", current = function() return 0 end, options = {
        { text = "Clear", apply = function() Gm(".wchange 1 0") end },
        { text = "Light rain", apply = function() Gm(".wchange 1 0.3") end },
        { text = "Rain", apply = function() Gm(".wchange 1 0.6") end },
        { text = "Heavy rain", apply = function() Gm(".wchange 1 1") end },
        { text = "Snow", apply = function() Gm(".wchange 2 0.6") end },
        { text = "Sandstorm", apply = function() Gm(".wchange 3 0.6") end },
    } })
    p:Reset({ "hdLvFog", 0, "hdLvShafts", 0, "hdLvMemory", 0, "hdLvHalos", 0, "hdLvTerrain", 0, "hdLvTerrainWmo", 1, "hdForeverForce", 0, "hdWmoLight", 1 })

    p:Section("Fog settings", false)
    p:Slider({ cvar = "hdFogDensity", label = "For Ever: density", min = 0, max = 3, step = 0.05, default = 1, forever = true })
    p:Slider({ cvar = "hdFogSun", label = "For Ever: sun colour", min = 0, max = 3, step = 0.05, default = 1, forever = true })
    p:Slider({ cvar = "hdFogAmbient", label = "For Ever: base colour", min = 0, max = 5, step = 0.05, default = 1, forever = true })
    p:Check({ cvar = "hdFogShadow", label = "For Ever: rays in the haze" })
    p:Slider({ cvar = "hdLvDensity", label = "Lumenveil: density", min = 0, max = 4, step = 0.05, default = 1 })
    p:Slider({ cvar = "hdLvQuality", label = "Lumenveil: quality (1 high, 3 fast)", min = 1, max = 3, step = 1, default = 2 })
    p:Slider({ cvar = "hdLvTemporal", label = "Lumenveil: stabilisation", min = 0, max = 0.95, step = 0.01, default = 0.85 })
    p:Check({ cvar = "hdLvMemory", label = "Lumenveil: obstacle memory", tip = "Needs the Lumenveil fog and rays." })

    p:Section("Ray, glow and shadow settings", false)
    p:Slider({ cvar = "hdRaysStrength", label = "For Ever rays: strength", min = 0, max = 3, step = 0.05, default = 1, forever = true })
    p:Slider({ cvar = "hdLvShaftGain", label = "Lumenveil rays: strength", min = 0, max = 4, step = 0.05, default = 1 })
    p:Slider({ cvar = "hdBloomStrength", label = "For Ever glow: strength", min = 0, max = 3, step = 0.05, default = 1, forever = true })
    p:Slider({ cvar = "hdBloomThreshold", label = "For Ever glow: threshold", min = 0, max = 1, step = 0.01, default = 0.7, forever = true })
    p:Slider({ cvar = "hdLvHaloGain", label = "Lumenveil glow: strength in the haze", min = 0, max = 4, step = 0.05, default = 1 })
    p:Slider({ cvar = "hdLvCfg_LampFog", label = "Lumenveil glow: lamp light in the haze", min = 0, max = 4, step = 0.02, default = 1 })
    p:Slider({ cvar = "hdLvCfg_FarLights", label = "Lumenveil glow: distant lamps", min = 0, max = 4, step = 0.02, default = 1 })
    p:Check({ cvar = "hdLvTerrainWmo", label = "Terrain shadows: buildings included", tip = "Unticked: the ground only." })

    p:Section("For Ever light and colours", false)
    p:Check({ cvar = "hdForever", label = "For Ever lights" })
    p:Check({ cvar = "hdGrade", label = "For Ever colour grading" })
    p:Check({ cvar = "hdSunPath", label = "Sun like For Ever" })
    p:Check({ cvar = "hdWmoLight", label = "For Ever buildings" })
    p:Slider({ cvar = "hdForeverLight", label = "Scenery (1 For Ever, 0 WotLK)", min = 0, max = 1, step = 0.05, default = 1, forever = true })
    p:Slider({ cvar = "hdTerrainLight", label = "Ground (1 For Ever, 0 WotLK, 2 test, 3 sun)", min = 0, max = 3, step = 1, default = 1, forever = true })
    p:Slider({ cvar = "hdLightAmbient", label = "Ground: ambient strength", min = 0, max = 1.5, step = 0.05, default = 1, forever = true })
    p:Slider({ cvar = "hdGradeStrength", label = "Grading: strength", min = 0, max = 2, step = 0.05, default = 1, forever = true })
    p:Input({ cvar = "hdForeverForce", label = "Force For Ever everywhere (0 = by place, e.g. 7624)" })

    p:Section("Test view and status", false)
    p:Slider({ cvar = "hdFogDebug", label = "Test view (0 normal, 3 haze, 4 shadows)", min = 0, max = 4, step = 1, default = 0, forever = true })
    p:Info(function() return Line("For Ever", "hdFogInfo") .. "\n" .. Line("Lumenveil", "hdLvInfo") end, 2)
end },

{ "lampes", "Lamps", function(p)
    p:Header("Lamps")
    p:Drop({ label = "What lights up around the lamps", current = LampMode, options = {
        { text = "Original WotLK", apply = function() SetMany({ "hdLights", 0, "hdLvLamps", 0 }) end },
        { text = "For Ever only", apply = function() SetMany({ "hdLights", 1, "hdLvLamps", 0 }) end },
        { text = "Lumenveil only", apply = function() SetMany({ "hdLights", 0, "hdLvLamps", 1, "hdLvLampClasses", 30 }) end, avail = function() return Has("hdLvLamps") end },
        { text = "For Ever + Lumenveil (building walls)", apply = function() SetMany({ "hdLights", 1, "hdLvLamps", 1, "hdLvLampClasses", 4, "hdLvLampForever", 1 }) end, avail = function() return Has("hdLvLamps") end },
        { text = "For Ever ground + real Lumenveil light", apply = function() SetMany({ "hdLights", 1, "hdLvLamps", 1, "hdLvLampClasses", 28, "hdLvLampForever", 0 }) end, avail = function() return Has("hdLvLampForever") end },
    } })
    p:Drop({ label = "Player lantern", current = LanternMode, options = {
        { text = "Off", apply = function() SetMany({ "hdLvLantern", 0 }) end },
        { text = "Automatic (at night)", apply = function() SetMany({ "hdLvLantern", 1 }) end, avail = function() return Has("hdLvLantern") end },
        { text = "Always on", apply = function() SetMany({ "hdLvLantern", 2 }) end, avail = function() return Has("hdLvLantern") end },
    } })
    p:Drop({ label = "Spell lights (spell colour)", current = function()
        if not On("hdLvSpells") then return 1 end
        return (Has("hdLvSpellGround") and On("hdLvSpellGround")) and 3 or 2
    end, options = {
        { text = "None", apply = function() SetMany({ "hdLvSpells", 0, "hdLvSpellGround", 0 }) end },
        { text = "Lumenveil (follows the lamp settings)", apply = function() SetMany({ "hdLvSpells", 1, "hdLvSpellGround", 0 }) end, avail = function() return Has("hdLvSpells") end },
        { text = "Lumenveil everywhere, ground included", apply = function() SetMany({ "hdLvSpells", 1, "hdLvSpellGround", 1 }) end, avail = function() return Has("hdLvSpellGround") end },
    } })

    p:Check({ cvar = "hdLvLampForever", label = "Lumenveil takes the For Ever colours", tip = "Unticked: Lumenveil lamps keep their own colours, even with the For Ever lamps on." })
    p:Reset({ "hdLvSpellGround", 0, "hdLvLampForever", 1, "hdLvLamps", 0, "hdLvLampClasses", 4, "hdLvSpells", 0, "hdLvLantern", 0, "hdLights", 1 })

    p:Section("Lamp settings", false)
    p:Slider({ cvar = "hdLights", label = "Ground lamps (1 For Ever, 0 WotLK)", min = 0, max = 3, step = 1, default = 1 })
    p:Slider({ cvar = "hdLvLampCount", label = "Lumenveil: number of lamps", min = 0, max = 128, step = 1, default = 32 })
    p:Slider({ cvar = "hdLvLampGain", label = "Lumenveil: intensity", min = 0, max = 4, step = 0.05, default = 1 })
    p:Slider({ cvar = "hdLvSpellGain", label = "Spells: intensity", min = 0, max = 4, step = 0.05, default = 1 })
    p:Header("What Lumenveil lights")
    p:Check({ label = "Building walls", get = function() return ClassBit(4) end, on = function(c) SetClassBit(4, c) end })
    p:Check({ label = "Ground (already lit)", get = function() return ClassBit(2) end, on = function(c) SetClassBit(2, c) end })
    p:Check({ label = "Interiors", get = function() return ClassBit(8) end, on = function(c) SetClassBit(8, c) end })
    p:Check({ label = "Models (already lit)", get = function() return ClassBit(16) end, on = function(c) SetClassBit(16, c) end })

    p:Section("Lantern colour", false)
    p:Slider({ cvar = "hdLvLanternR", label = "Red", min = 0, max = 4, step = 0.01, default = 1 })
    p:Slider({ cvar = "hdLvLanternG", label = "Green", min = 0, max = 4, step = 0.01, default = 0.55 })
    p:Slider({ cvar = "hdLvLanternB", label = "Blue", min = 0, max = 4, step = 0.01, default = 0.2 })

    p:Section("Status", false)
    p:Info(function() return Line("Lamps", "hdLvLampInfo") .. "\n" .. Line("Lumenveil", "hdLvInfo") .. "\n" .. Line("Ground lamps", "hdLightsInfo") .. "\n" .. Line("Spells", "hdLvSpellInfo") end, 5)
end },

{ "nature", "Water and grass", function(p)
    p:Header("Water and grass")
    p:Drop({ label = "Grass wind", current = function()
        local wind, sx = On("hdGrassWind"), Has("hdSxWind") and On("hdSxWind")
        if not wind then return 1 elseif not sx then return 2 else return 3 end
    end, options = {
        { text = "No wind", apply = function() SetMany({ "hdGrassWind", 0, "hdSxWind", 0 }) end },
        { text = "Waystone (your current wind)", apply = function() SetMany({ "hdGrassWind", 1, "hdSxWind", 0 }) end },
        { text = "Seyris (sets the Waystone wind strength by zone and weather)", apply = function() SetMany({ "hdGrassWind", 1, "hdSxWind", 1 }) end, avail = function() return Has("hdSxWind") end },
    } })
    p:Check({ cvar = "hdWater", label = "For Ever water" })
    p:Check({ cvar = "hdWaterReflect", label = "Water reflections" })
    p:Check({ cvar = "hdGrassLight", label = "For Ever grass (light)" })
    p:Check({ cvar = "hdGrassReact", label = "Grass parts around the player" })
    p:Slider({ cvar = "surfaceCoverGlints", label = "Snow glints (0 off, 1 full)", min = 0, max = 1, step = 0.01, default = 0.06 })
    p:Reset({ "hdSxWind", 0, "hdGrassWind", 1, "hdWater", 1, "hdWaterReflect", 1, "hdGrassLight", 1 })

    p:Section("Wind settings", false)
    p:Slider({ cvar = "hdGrassWindStrength", label = "Strength", min = 0, max = 5, step = 0.05, default = 1 })
    p:Slider({ cvar = "hdGrassWindSpeed", label = "Wave speed", min = 0, max = 10, step = 0.1, default = 3 })
    p:Slider({ cvar = "hdGrassWindDirection", label = "Direction (degrees)", min = 0, max = 360, step = 5, default = 45 })
    p:Slider({ cvar = "hdGrassReactRadius", label = "Parting: radius", min = 0, max = 10, step = 0.5, default = 2 })
    p:Info(function() return Line("Seyris", "hdSxInfo") end, 2)

    p:Section("Water settings", false)
    p:Slider({ cvar = "hdWaterBody", label = "Own colour (1 For Ever)", min = 0, max = 3, step = 0.05, default = 1, forever = true })
    p:Slider({ cvar = "hdWaterDebug", label = "Test view (0 normal, 1 depth, 3 reflections, 4 foam)", min = 0, max = 7, step = 1, default = 0, forever = true })
    p:Info(function() return Line("Status", "hdWaterInfo") end, 3)

    p:Section("Grass distance and density (applied on release)", false)
    p:Slider({ cvar = "farclip", label = "View distance", min = 200, max = 2500, step = 25, default = 1277, release = true })
    p:Slider({ cvar = "groundEffectDist", label = "Grass: distance (140 WotLK)", min = 0, max = 280, step = 10, default = 140, release = true })
    p:Slider({ cvar = "groundEffectDensity", label = "Grass: density (128 advised)", min = 16, max = 256, step = 8, default = 128, release = true })
    p:Slider({ cvar = "hdGrassBuffers", label = "Grass: buffers (0 = WotLK)", min = 0, max = 4096, step = 128, default = 1536, release = true })
    p:Slider({ cvar = "hdGrassPack", label = "Grass: right-sized buffers", min = 0, max = 1, step = 1, default = 1, release = true })
    p:Slider({ cvar = "hdGrassFillBudget", label = "Grass: fills per frame", min = 0, max = 200, step = 5, default = 60, release = true })
    p:Slider({ cvar = "hdGrassKeep", label = "Grass: keep off-screen", min = 0, max = 2000, step = 20, default = 600, release = true })
end },

{ "fluidite", "FSR / DLSS", function(p)
    p:Header("Upscaling")
    p:Note("Draws the 3D world at a lower resolution and rebuilds it sharp, for more frames per second. Only the world is upscaled: the interface stays sharp. FSR 3.1 works on any GPU; DLSS needs an NVIDIA RTX card and the DLSS folder beside the game.")
    p:Drop({ label = "Upscaler", current = UpscalerMode, options = {
        { text = "None (native resolution)", apply = function() SetMany({ "hdFsr", 0, "hdDlss", 0 }) end },
        { text = "FSR 3.1 (AMD)", apply = function() SetMany({ "hdFsr", 1, "hdDlss", 0 }) end, avail = function() return Has("hdFsr") end },
        { text = "DLSS (NVIDIA RTX, replaces FSR)", apply = function() SetMany({ "hdFsr", 0, "hdDlss", 1 }) end, avail = function() return Has("hdDlss") end },
    } })
    p:Drop({ label = "Quality", current = QualityMode, options = {
        { text = "Quality (about 67% resolution)", apply = function() SetMany({ "hdFsrQuality", 1 }) end },
        { text = "Balanced (about 59%)", apply = function() SetMany({ "hdFsrQuality", 2 }) end },
        { text = "Performance (50%)", apply = function() SetMany({ "hdFsrQuality", 3 }) end },
        { text = "Ultra performance (33%)", apply = function() SetMany({ "hdFsrQuality", 4 }) end },
    } })
    p:Reset({ "hdFsrReactive", 1, "hdFsrUnits", 1 })

    p:Section("Sharpness", false)
    p:Slider({ cvar = "hdFsrSharpness", label = "FSR (0 none, 1 max)", min = 0, max = 1, step = 0.05, default = 0.2, release = true })
    p:Slider({ cvar = "hdDlssSharpness", label = "DLSS (1 = FSR max, up to 2)", min = 0, max = 2, step = 0.05, default = 0.3, release = true })

    p:Section("Motion (against trails)", false)
    p:Check({ cvar = "hdFsrObjectMotion", label = "Full motion of players and creatures", tip = "Replays the animated models to give the upscaler their true motion: far fewer ghost trails behind moving characters." })
    p:Check({ cvar = "hdFsrReactive", label = "Transparency mask (water, effects, particles)" })
    p:Check({ cvar = "hdFsrUnits", label = "Trust history less around moving units" })

    p:Section("Render scale without an upscaler", false)
    p:Slider({ cvar = "hdRenderScale", label = "World resolution (1 = native)", min = 0.5, max = 1, step = 0.05, default = 1, release = true })

    p:Section("Status", false)
    p:Info(function()
        return Line("Upscaling", (Has("hdDlss") and On("hdDlss")) and "hdDlssInfo" or "hdFsrInfo") .. "\n" .. Line("Object motion", "hdFsrMotionInfo")
    end, 4)
end },

{ "avance", "Advanced", function(p)
    p:Header("Lumenveil: advanced settings")
    p:Note("For experimenting. No effect while the matching Lumenveil switch is off.")
    p:Reset({})
    p:Section("The sliders", false)
    for _, d in ipairs(ADVANCED) do
        p:Slider({ cvar = d[1], label = d[2], min = d[3], max = d[4], step = d[5], default = d[6] })
    end
end },
}

-- ===================================================================== window
local function Show(key)
    if openList then openList:Hide() end
    for _, p in ipairs(pages) do
        if p.key == key then
            if not p.built then p:Build() end
            p.scroll:Show(); p.button:LockHighlight(); current = p
        elseif p.built then p.scroll:Hide(); p.button:UnlockHighlight() end
    end
    if current then current:Refresh() end
end

local function BuildPanel()
    panel = CreateFrame("Frame", "WaystoneHDPanel", UIParent)
    panel:SetWidth(780); panel:SetHeight(600); panel:SetPoint("CENTER")
    panel:SetFrameStrata("DIALOG")
    panel:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
        edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 32,
        insets = { left = 11, right = 12, top = 12, bottom = 11 } })
    panel:EnableMouse(true); panel:SetMovable(true); panel:SetClampedToScreen(true)
    panel:RegisterForDrag("LeftButton")
    panel:SetScript("OnDragStart", panel.StartMoving)
    panel:SetScript("OnDragStop", panel.StopMovingOrSizing)
    panel:Hide()
    tinsert(UISpecialFrames, "WaystoneHDPanel")
    local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    title:SetPoint("TOP", 0, -16); title:SetText("Waystone HD")
    local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -6, -6)
    for i, d in ipairs(PAGEDEFS) do
        local page = NewPage(d[1], d[2], panel, d[3])
        local b = CreateFrame("Button", "WaystoneHDTab" .. i, panel, "UIPanelButtonTemplate")
        b:SetWidth(148); b:SetHeight(28)
        b:SetPoint("TOPLEFT", 16, -48 - (i - 1) * 32)
        b:SetText(d[2])
        b:SetScript("OnClick", function() Show(d[1]) end)
        page.button = b
        table.insert(pages, page)
    end
    local wait = 0
    panel:SetScript("OnUpdate", function(self, elapsed)
        wait = wait - elapsed
        if wait > 0 then return end
        wait = 0.5
        if current and not openList then current:Refresh() end
    end)
    panel:SetScript("OnHide", function() if openList then openList:Hide() end end)
end

local function Open(key)
    if not panel then BuildPanel() end
    if not panel:IsShown() then panel:Show() end
    Show(key or (current and current.key) or "home")
end

SLASH_WAYSTONEHD1 = "/hd"
SlashCmdList["WAYSTONEHD"] = function()
    if not panel then BuildPanel() end
    if panel:IsShown() then panel:Hide() else Open() end
end
SLASH_WAYSTONEHDLV1 = "/hdlumen"
SlashCmdList["WAYSTONEHDLV"] = function() Open("ambiance") end
SLASH_WAYSTONEHDLAMP1 = "/hdlamps"
SlashCmdList["WAYSTONEHDLAMP"] = function() Open("lampes") end
SLASH_WAYSTONEHDFSR1 = "/hdfsr"
SlashCmdList["WAYSTONEHDFSR"] = function() Open("fluidite") end
