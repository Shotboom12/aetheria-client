#include "Cursor.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Lumenveil/Lumenveil.h"
#include "Engine/Source/Lumenveil/OptionsPages.h"
#include "WoW/Source/WorldClient/HD/HdAO.h"
#include "WoW/Source/WorldClient/HD/HdBloom.h"
#include "WoW/Source/WorldClient/HD/HdContactShadow.h"
#include "WoW/Source/WorldClient/HD/HdFog.h"
#include "WoW/Source/WorldClient/HD/HdGI.h"
#include "WoW/Source/WorldClient/HD/HdGrade.h"
#include "WoW/Source/WorldClient/HD/HdWater.h"
#include "WoW/Source/WorldClient/HD/HdTime.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"

namespace {
void RegisterGraphicsOptionCVar(const char* name, const char* defaultValue) {
    if (!CVar::LookupRegistered(name)) {
        OptionCVars::Register(name, nullptr, defaultValue);
    }
}

void RegisterAetheriaRendererOptions() {
    // Register before the video-options Lua pages are built and before the first world frame.
    // Renderer passes also look these up lazily; registering here makes saved values and menu
    // controls available from the start of the session.
    Visuals::Register();
    // Only CVars whose modules register them lazily (on first use) are listed: this is their first registration, so
    // these values are their defaults and early LookupRegistered calls find them. The AO, bloom, fog, contact-shadow,
    // GI, grading and water CVars are registered by the HD::*RegisterCVars calls above with their donor defaults; the
    // "0" entries they had here never applied (the first registration wins) and were removed (2026-10-30, P6).
    const char* options[][2] = {
        {"hdForever", "0"}, {"hdForeverForce", "0"}, {"hdForeverLight", "1"},
        {"hdSky", "1"}, {"hdSkyGlow", "0.35"}, {"hdSkyGlowHorizon", "0.8"},
        {"hdSkyGlowSize", "50"}, {"hdSkyCloudSun", "1"}, {"hdSkyDither", "1"},
        {"hdTerrainLight", "1"}, {"hdWmoLight", "1"}, {"hdGrassLight", "0"},
        {"hdLights", "1"},
        {"hdSunShadows", "0"}, {"hdSunQuality", "1"}, {"hdSunStrength", "1"}, {"hdSunContactLength", "1.5"},
        {"hdAONormals", "0"},
    };
    for (const auto& option : options) {
        RegisterGraphicsOptionCVar(option[0], option[1]);
    }
}
} // namespace

// Graphics options ported from Kalibros/STRHercules wowdecomp ff7b6ecf3a61.
// Retains the upstream outline, loot-beam and overlay pages without unrelated distance settings.
void AetheriaInitializeGraphicsOptions() {
    Visuals::Register();
    HD::AO::RegisterCVars();
    HD::Bloom::RegisterCVars();
    HD::RegisterContactShadowCVars();
    HD::Fog::RegisterCVars();
    HD::RegisterGICVars();
    HD::Grade::RegisterCVars();
    HD::RegisterWaterCVars();
    HD::RegisterTimeCVars();
    RegisterAetheriaRendererOptions();
    // Load Lumenveil's settings and register its live setting CVars before the generated pages are built.
    // The call is idempotent; RenderWorld retains a fallback for startup paths that skip this UI initializer.
    Lumenveil::Startup();
    // A second chunk: one literal cannot hold both.
    static const char pagesScript[] = R"lua(
if not VideoOptionsFrame or WowDecompOutlinePanel then return end

-- Esteria option pages: the unit outline, the loot beam and the performance overlay. Every control is a CVar, so Okay,
-- Cancel and Defaults treat the pages like any other video panel. Colours are RRGGBB boxes that apply at once.
function WowDecompBuildPage(name, title, subtitle)
    local page = CreateFrame("Frame", name, VideoOptionsFramePanelContainer)
    page:Hide()
    page.name = title
    page.options = {}
    page.checks = {}
    page.boxes = {}
    page.y = -4

    local heading = page:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    heading:SetPoint("TOPLEFT", 16, -16)
    heading:SetText(title)

    local sub = page:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    sub:SetPoint("TOPLEFT", heading, "BOTTOMLEFT", 0, -8)
    sub:SetPoint("RIGHT", -32, 0)
    sub:SetJustifyH("LEFT")
    sub:SetText(subtitle)

    local scroll = CreateFrame("ScrollFrame", name .. "Scroll", page, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", 8, -60)
    scroll:SetPoint("BOTTOMRIGHT", -30, 8)
    local child = CreateFrame("Frame", name .. "Child", scroll)
    child:SetSize(500, 10)
    scroll:SetScrollChild(child)
    page.child = child

    page:SetScript("OnShow", function(self)
        for _, check in ipairs(self.checks) do
            check:SetChecked(GetCVar(check.cvar) == "1")
        end
        for _, box in ipairs(self.boxes) do
            box:SetText(GetCVar(box.cvar) or "")
            box:UpdateSwatch()
        end
    end)

    function page:Header(text)
        self.y = self.y - 10
        local fs = self.child:CreateFontString(nil, "ARTWORK", "GameFontNormal")
        fs:SetPoint("TOPLEFT", 8, self.y)
        fs:SetText(text)
        self.y = self.y - 24
    end

    function page:Check(cvar, text, tooltip)
        local check = CreateFrame("CheckButton", name .. "Check" .. cvar, self.child, "OptionsCheckButtonTemplate")
        check:SetPoint("TOPLEFT", 8, self.y)
        _G[check:GetName() .. "Text"]:SetText(text)
        check.type = CONTROLTYPE_CHECKBOX
        check.cvar = cvar
        check.tooltipText = tooltip
        check:SetScript("OnClick", function(self) BlizzardOptionsPanel_CheckButton_OnClick(self) end)
        self.options[cvar] = { text = text, tooltip = tooltip }
        BlizzardOptionsPanel_RegisterControl(check, self)
        self.checks[#self.checks + 1] = check
        self.y = self.y - 28
        return check
    end

    function page:Dropdown(cvar, text, tooltip, choices)
        local dropdown = CreateFrame("Frame", name .. "Dropdown" .. cvar, self.child, "UIDropDownMenuTemplate")
        dropdown:SetPoint("TOPLEFT", 0, self.y + 2)
        UIDropDownMenu_SetWidth(dropdown, 250)
        local label = self.child:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
        label:SetPoint("BOTTOMLEFT", dropdown, "TOPLEFT", 18, 2)
        label:SetText(text)
        dropdown.type = CONTROLTYPE_DROPDOWN
        dropdown.cvar = cvar
        dropdown.defaultValue = GetCVarDefault(cvar)
        dropdown.GetValue = function(control) return GetCVar(control.cvar) end
        dropdown.SetValue = function(control, value)
            SetCVar(control.cvar, value)
            control.value = GetCVar(control.cvar)
            UIDropDownMenu_SetSelectedValue(control, control.value)
        end
        dropdown.RefreshValue = function(control)
            control.value = control:GetValue()
            control.newValue = nil
            UIDropDownMenu_SetSelectedValue(control, control.value)
            for _, choice in ipairs(choices) do
                if tostring(choice[1]) == tostring(control.value) then
                    UIDropDownMenu_SetText(control, choice[2])
                    break
                end
            end
        end
        UIDropDownMenu_Initialize(dropdown, function(control, level)
            for _, choice in ipairs(choices) do
                local info = UIDropDownMenu_CreateInfo()
                info.text = choice[2]
                info.value = tostring(choice[1])
                info.checked = tostring(GetCVar(cvar)) == info.value
                info.func = function(button)
                    SetCVar(cvar, button.value)
                    dropdown.newValue = button.value ~= dropdown.value and button.value or nil
                    dropdown.value = GetCVar(cvar)
                    dropdown:RefreshValue()
                end
                UIDropDownMenu_AddButton(info, level)
            end
        end)
        dropdown:RefreshValue()
        self.options[cvar] = { text = text, tooltip = tooltip }
        BlizzardOptionsPanel_RegisterControl(dropdown, self)
        self.y = self.y - 58
        return dropdown
    end

    function page:Slider(cvar, text, tooltip, low, high, step, format, wheel)
        local sliderName = name .. "Slider" .. cvar
        local slider = CreateFrame("Slider", sliderName, self.child, "OptionsSliderTemplate")
        slider:SetSize(280, 17)
        slider:SetPoint("TOPLEFT", 12, self.y - 14)
        slider.type = CONTROLTYPE_SLIDER
        slider.cvar = cvar
        slider.SetDisplayValue = slider.SetValue
        slider.SetValue = function(self, value)
            self:SetDisplayValue(value)
            BlizzardOptionsPanel_SetCVarSafe(self.cvar, value)
        end
        _G[sliderName .. "Text"]:SetText(text)
        _G[sliderName .. "Low"]:SetText("")
        _G[sliderName .. "High"]:SetText("")

        local readout = CreateFrame("EditBox", sliderName .. "Edit", slider, "InputBoxTemplate")
        readout:SetSize(70, 20)
        readout:SetPoint("LEFT", slider, "RIGHT", 16, 0)
        readout:SetAutoFocus(false)
        readout:SetJustifyH("CENTER")
        readout:SetScript("OnEnterPressed", function(box)
            local value = tonumber(box:GetText())
            if value then slider:SetDisplayValue(value) end
            box:ClearFocus()
            box:SetText(format(slider:GetValue()))
        end)
        readout:SetScript("OnEscapePressed", function(box)
            box:ClearFocus()
            box:SetText(format(slider:GetValue()))
        end)

        slider:EnableMouseWheel(true)
        slider:SetScript("OnMouseWheel", function(self, delta)
            self:SetDisplayValue(self:GetValue() + delta * (wheel or step))
        end)
        slider:SetScript("OnValueChanged", function(self, value)
            self.newValue = value
            if not readout:HasFocus() then readout:SetText(format(value)) end
        end)
        slider:SetScript("OnShow", function(self)
            self:SetDisplayValue(tonumber(GetCVar(self.cvar)) or low)
        end)
        slider:SetMinMaxValues(low, high)
        slider:SetValueStep(step)
        self.options[cvar] = { text = text, minValue = low, maxValue = high, valueStep = step, tooltip = tooltip }
        BlizzardOptionsPanel_RegisterControl(slider, self)
        self.y = self.y - 46
        return slider
    end

    function page:Color(cvar, text, tooltip)
        local label = self.child:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
        label:SetPoint("TOPLEFT", 12, self.y - 4)
        label:SetText(text)

        local box = CreateFrame("EditBox", name .. "Color" .. cvar, self.child, "InputBoxTemplate")
        box:SetSize(70, 20)
        box:SetPoint("TOPLEFT", 230, self.y)
        box:SetAutoFocus(false)
        box:SetMaxLetters(6)
        box.cvar = cvar

        local swatch = self.child:CreateTexture(nil, "ARTWORK")
        swatch:SetSize(18, 18)
        swatch:SetPoint("LEFT", box, "RIGHT", 10, 0)
        swatch:SetTexture(1, 1, 1)

        function box:UpdateSwatch()
            local hex = self:GetText()
            if string.match(hex, "^%x%x%x%x%x%x$") then
                swatch:SetTexture(tonumber(string.sub(hex, 1, 2), 16) / 255, tonumber(string.sub(hex, 3, 4), 16) / 255, tonumber(string.sub(hex, 5, 6), 16) / 255)
            end
        end
        box:SetScript("OnTextChanged", function(self) self:UpdateSwatch() end)
        box:SetScript("OnEnterPressed", function(self)
            local hex = string.upper(string.gsub(self:GetText(), "#", ""))
            if string.match(hex, "^%x%x%x%x%x%x$") then
                SetCVar(self.cvar, hex)
            else
                self:SetText(GetCVar(self.cvar) or "")
            end
            self:ClearFocus()
        end)
        box:SetScript("OnEscapePressed", function(self)
            self:SetText(GetCVar(self.cvar) or "")
            self:ClearFocus()
        end)
        self.boxes[#self.boxes + 1] = box
        self.y = self.y - 28
        return box
    end

    function page:Done()
        self.child:SetHeight(-self.y + 16)
        VideoOptionsPanel_OnLoad(self)
    end

    return page
end

)lua";

    static const char pagesScript2[] = R"lua(
if not VideoOptionsFrame or WowDecompOutlinePanel then return end
local BuildPage = WowDecompBuildPage

local function Pixels(v) return string.format("%d px", v + 0.5) end
local function Yards(v) return string.format("%.1f yd", v) end
local function Percent(v) return string.format("%d%%", v * 100 + 0.5) end
local function Seconds(v) return string.format("%.2f s", v) end
local function Plain(v) return string.format("%.2f", v) end
local function Count(v) return string.format("%d", v + 0.5) end

local TierNames = { [0] = "Money only", "Poor", "Common", "Uncommon", "Rare", "Epic", "Legendary", "Artifact", "Heirloom" }

-- Unit outline
do
    local p = BuildPage("WowDecompOutlinePanel", "Aetheria Outline", "Outlines your target and mouseover. Occluded outlines require Vulkan; Direct3D requires the through-terrain option.")
    p:Header("Unit outline")
    p:Check("unitOutline", "Enable unit outline", "Master switch for the unit outline.")
    p:Check("unitOutlineTarget", "Outline your target", "Outlines the unit you have targeted.")
    p:Check("unitOutlinePetTarget", "Outline your pet's target", "Outlines whatever your pet is attacking.")
    p:Check("unitOutlineMouseover", "Outline the unit under the mouse", "Outlines the unit the cursor is over.")
    p:Check("unitOutlineObjects", "Outline doors, chests and other objects", "Outlines interactive objects the cursor is over.")
    p:Check("unitOutlineQuestGivers", "Outline quest givers and flight masters", "Keeps quest givers with a quest for you, and flight masters, outlined without selecting them.")
    p:Header("Look")
    p:Slider("unitOutlineWidth", "Width", "Width of the line in pixels.", 1, 6, 1, Pixels, 1)
    p:Slider("unitOutlineOpacity", "Opacity", "Strength of the line.", 0.1, 1, 0.05, Percent, 0.05)
    p:Check("unitOutlineThroughWalls", "Show through terrain and buildings", "Off hides the line where something stands between you and the unit.")
    p:Check("unitOutlineSettleFade", "Dim the line after a moment", "A selected unit's line starts at full strength and settles to a dimmer one.")
    p:Slider("unitOutlineSettleOpacity", "Settled opacity", "How strong the line is once it has settled.", 0.1, 1, 0.05, Percent, 0.05)
    p:Header("Colours (RRGGBB, press Enter to apply)")
    p:Color("unitOutlineHostile", "Hostile")
    p:Color("unitOutlineNeutral", "Neutral")
    p:Color("unitOutlineFriendly", "Friendly")
    p:Color("unitOutlineFriendlyPlayer", "Friendly player")
    p:Color("unitOutlineQuestGiver", "Quest giver / flight master")
    p:Color("unitOutlineObject", "Object")
    p:Done()
end

-- Loot beam
do
    local p = BuildPage("WowDecompLootBeamPanel", "Aetheria Loot Beam", "A beam of light over lootable corpses, tinted by the best item in the loot. A marker only; it sends nothing to the server.")
    p:Header("Loot beam")
    p:Check("lootBeam", "Enable loot beam", "Master switch for the loot beam.")
    p:Check("lootBeamShowBeam", "Draw the light shaft", "Off leaves only the sparkles.")
    p:Check("lootBeamSparkles", "Draw sparkles", "Drifting motes of light around the beam.")
    p:Check("lootBeamThroughWalls", "Show through terrain and buildings", "Off hides the beam behind terrain and buildings.")
    p:Slider("lootBeamMinQuality", "Lowest loot tier with a beam", "Corpses whose best loot is below this tier get no beam. 0 shows money-only corpses too.", 0, 8, 1, function(v) return TierNames[math.floor(v + 0.5)] or "?" end, 1)
    p:Slider("lootBeamMaxDistance", "Maximum distance", "Corpses further away than this get no beam. 0 is unlimited.", 0, 300, 5, function(v) if v < 1 then return "Unlimited" end return string.format("%d yd", v + 0.5) end, 5)
    p:Header("Shaft")
    p:Slider("lootBeamHeight", "Height", "How high the beam rises.", 2, 60, 1, Yards, 1)
    p:Slider("lootBeamBaseOffset", "Base height", "How far above the corpse the beam starts.", 0, 5, 0.1, Yards, 0.1)
    p:Slider("lootBeamWidth", "Width", "Half-width of the beam at its base.", 0.1, 3, 0.05, Yards, 0.05)
    p:Slider("lootBeamMinWidth", "Width kept at distance", "Minimum width per yard of camera distance, so far beams stay visible.", 0, 0.05, 0.002, Plain, 0.002)
    p:Slider("lootBeamAlpha", "Opacity", "Strength of the beam.", 0.05, 1, 0.05, Percent, 0.05)
    p:Slider("lootBeamPulse", "Pulse depth", "How much the beam pulses. 0 is steady.", 0, 1, 0.05, Percent, 0.05)
    p:Slider("lootBeamPulseSpeed", "Pulse speed", "How fast the beam pulses.", 0, 6, 0.1, Plain, 0.1)
    p:Slider("lootBeamFadeIn", "Fade-in time", "How long a new beam takes to appear.", 0, 3, 0.05, Seconds, 0.05)
    p:Slider("lootBeamFadeOut", "Fade-out time", "How long a beam takes to disappear once the corpse is looted.", 0, 5, 0.05, Seconds, 0.05)
    p:Header("Sparkles")
    p:Slider("lootBeamSparkleCount", "Count", "Sparkles per beam.", 0, 32, 1, Count, 1)
    p:Slider("lootBeamSparkleSize", "Size", "Size of each sparkle.", 0.01, 0.25, 0.01, Plain, 0.01)
    p:Slider("lootBeamSparkleAlpha", "Opacity", "Strength of the sparkles.", 0.05, 1, 0.05, Percent, 0.05)
    p:Slider("lootBeamSparkleRise", "Rise speed", "How fast sparkles climb.", 0, 5, 0.1, Plain, 0.1)
    p:Slider("lootBeamSparkleDrift", "Sideways drift", "How far sparkles wander sideways.", 0, 1, 0.01, Plain, 0.01)
    p:Slider("lootBeamSparkleLife", "Lifetime", "How long a sparkle lasts.", 0.3, 8, 0.1, Seconds, 0.1)
    p:Slider("lootBeamSparkleTwinkle", "Twinkle speed", "How fast sparkles flicker.", 0, 20, 0.5, Plain, 0.5)
    p:Header("Colours (RRGGBB, press Enter to apply)")
    p:Color("lootBeamColor", "Unknown loot")
    for tier = 0, 8 do
        p:Color("lootBeamTier" .. tier, TierNames[tier])
    end
    p:Done()
end

-- Performance overlay
do
    local p = BuildPage("WowDecompOverlayPanel", "Aetheria Overlay", "A small movable overlay with frame rate, frame time, draw calls, triangles and CPU use.")
    p:Header("Performance overlay")
    p:Check("showPerformance", "Show the overlay", "Drag the overlay with the left mouse button to move it.")
    p:Check("perfOverlayFps", "Frame rate", "Frames per second, averaged over 30 frames.")
    p:Check("perfOverlayFrameTime", "Frame time", "Milliseconds per frame, taken from the averaged frame rate.")
    p:Check("perfOverlayDrawCalls", "Draw calls", "Draw calls submitted last frame, including the outline's extra passes.")
    p:Check("perfOverlayTriangles", "Triangles", "Triangles submitted last frame.")
    p:Check("perfOverlayCpu", "CPU use", "This process's share of the whole machine's CPU.")
    p:Header("Look")
    p:Slider("perfOverlayScale", "Size", "Scale of the overlay.", 0.5, 2, 0.05, Percent, 0.05)
    p:Slider("perfOverlayAlpha", "Opacity", "Opacity of the overlay.", 0.2, 1, 0.05, Percent, 0.05)
    p:Slider("perfOverlayInterval", "Update interval", "How often the numbers refresh.", 0.1, 2, 0.05, Seconds, 0.05)
    p:Check("perfOverlayBackground", "Dark background", "Draws a dark panel behind the numbers.")
    p:Check("perfOverlayLocked", "Lock position", "Stops the overlay from being dragged.")
    p:Done()
end

-- Renderer selection. Each renderer owns its passes; the old Aetheria-native controls are not selectable here.
do
    local p = BuildPage("WowDecompRendererPanel", "Graphics Renderer", "Choose one world renderer. The change applies to the world rendering pipeline.")
    p:Header("World renderer")
    p:Dropdown("visuals", "Visual mode", "Select the renderer used for the world.", {
        { "0", "Stock Vulkan" },
        { "1", "Lumenveil" },
        { "2", "Azeroth Remastered" },
    })
    p:Done()
end

-- Azeroth Remastered options, bound to the donor's Vulkan passes and CVars.
do
    local p = BuildPage("WowDecompARPanel", "Azeroth Remastered", "Vulkan effects from Kalibros' Azeroth Remastered renderer. Requires Visual mode: Azeroth Remastered.")
    p:Header("Lighting and atmosphere")
    p:Check("hdFog", "Volumetric fog", "Uses Forever fog data when available; without it, only diagnostic views are available.")
    p:Slider("hdFogDensity", "Fog density", "Fog density multiplier.", 0, 5, 0.1, Plain, 0.1)
    p:Slider("hdFogSun", "Sun scattering", "Scattering from the world light.", 0, 5, 0.1, Plain, 0.1)
    p:Check("hdBloom", "Bloom", "Adds a halo around bright world pixels.")
    p:Slider("hdBloomStrength", "Bloom strength", "Halo intensity.", 0, 5, 0.1, Plain, 0.1)
    p:Check("hdRays", "Sun rays", "Screen-space rays are most visible with the sun in view.")
    p:Slider("hdRaysStrength", "Sun-ray strength", "Ray intensity.", 0, 5, 0.1, Plain, 0.1)
    p:Check("hdGrade", "Colour grading", "Uses Forever grading data when available.")
    p:Slider("hdGradeStrength", "Grading strength", "Grading intensity.", 0, 2, 0.05, Plain, 0.05)
    p:Header("Shadows and ambient light")
    p:Check("hdAO", "Ambient occlusion", "Screen-space ambient occlusion; higher quality can use more GPU time.")
    p:Slider("hdAORadius", "AO radius", "Ambient-occlusion radius in yards.", 0.25, 6, 0.25, Yards, 0.25)
    p:Slider("hdAOStrength", "AO strength", "Ambient-occlusion intensity.", 0, 3, 0.05, Plain, 0.05)
    p:Slider("hdAOQuality", "AO quality", "0 = low, 1 = medium, 2 = high, 3 = ultra.", 0, 3, 1, Count, 1)
    p:Check("hdContactShadow", "Contact shadows", "Short screen-space shadows for nearby geometry and characters.")
    p:Slider("hdContactShadowLength", "Contact shadow length", "Ray length in yards.", 0.25, 6, 0.25, Yards, 0.25)
    p:Slider("hdContactShadowStrength", "Contact shadow strength", "Shadow contribution.", 0, 1, 0.05, Percent, 0.05)
    p:Check("hdGI", "One-bounce global illumination", "Adds a screen-space colour bounce over opaque geometry.")
    p:Slider("hdGIRadius", "GI reach", "Bounce reach in yards.", 0.5, 12, 0.5, Yards, 0.5)
    p:Slider("hdGIStrength", "GI strength", "Bounce intensity.", 0, 4, 0.05, Plain, 0.05)
    p:Header("Day, sun and shadows")
    p:Slider("hdTime", "Local time", "-1 follows the server clock; 0 to 24 sets a local preview time.", -1, 24, 0.25, Plain, 0.25)
    p:Check("hdSunPath", "Sun follows world light", "Positions the sun using the active world light direction.")
    p:Check("hdShadowSun", "Real sun shadow length", "Uses the light's actual elevation for longer dawn and dusk shadows.")
    p:Slider("hdShadowBillboard", "Tree shadow style", "0 = original, 1 = face leaf cards toward the sun, 2 = no leaf-card shadows.", 0, 2, 1, Count, 1)
    p:Header("Forever water")
    p:Slider("hdWater", "Water mode", "0 = Blizzard water, 1 = Forever PBR, 2 = 3.3.5 style test shaders.", 0, 2, 1, Count, 1)
    p:Slider("hdWaterBody", "Water body colour", "Strength of the water's own colour.", 0, 5, 0.05, Plain, 0.05)
    p:Check("hdWaterReflect", "Water reflections", "Adds screen-space reflections of the world.")
    p:Check("hdWaterColor", "Use game water colour", "Keeps the game's water colours across the day/night cycle.")
    p:Check("hdWaterRipples", "Water ripples", "Use nearby character interactions to disturb the water surface.")
    p:Done()
end

)lua";

    // Tree and bush wind (Engine/Source/Model2/M2TreeWind.cpp).
    static const char treeWindScript[] = R"lua(
if not VideoOptionsFrame or not WowDecompBuildPage or WowDecompTreeWindPanel then return end

local function Degrees(v) return string.format("%.2f deg", v) end
local function Heading(v) return string.format("%d deg", v + 0.5) end
local function Speed(v) return string.format("%.1f yd/s", v) end
local function Yards(v) return string.format("%.1f yd", v) end
local function Plain(v) return string.format("%.2f", v) end
local function Limit(v) if v < 1 then return "Unlimited" end return string.format("%d yd", v + 0.5) end

local function WindRows(p, prefix, what, one)
    p:Slider(prefix .. "Direction", "Wind direction", "Heading the wind blows toward, in the world.", 0, 360, 1, Heading, 5)
    p:Slider(prefix .. "Speed", "Wave speed", "How fast the gusts travel across the " .. what .. ".", 0, 12, 0.1, Speed, 0.1)
    p:Slider(prefix .. "Amplitude", "Sway", "How far the top sways. Keep it under about 2 degrees for a breeze.", 0, 6, 0.05, Degrees, 0.05)
    p:Slider(prefix .. "Wavelength", "Wave length", "Longer waves make broader, slower gusts.", 2, 80, 0.5, Yards, 1)
    p:Slider(prefix .. "Lean", "Downwind lean", "A constant lean with the wind on top of the sway.", 0, 4, 0.05, Degrees, 0.05)
    p:Slider(prefix .. "CrossAmplitude", "Cross-swell", "A second, smaller wave across the wind, so the motion is not a single rhythm.", 0, 3, 0.01, Degrees, 0.05)
    p:Slider(prefix .. "CrossWavelength", "Cross-swell length", "Length of the second wave.", 2, 40, 0.5, Yards, 1)
    p:Slider(prefix .. "CrossAngle", "Cross-swell angle", "Heading of the second wave from the wind.", 0, 180, 1, Heading, 5)
    p:Slider(prefix .. "Variance", "Variety", "How much the sway differs from one " .. one .. " to the next. 0 makes them all alike.", 0, 1, 0.05, Plain, 0.05)
    p:Slider(prefix .. "Gust", "Gusts", "Depth of the slow rise and fall of the wind. 0 is steady.", 0, 1, 0.05, Plain, 0.05)
    p:Slider(prefix .. "DistanceFade", "Fade with distance", "Sway lost per yard from the camera. 0 sways far ones as much as near ones.", 0, 0.05, 0.001, function(v) return string.format("%.3f", v) end, 0.001)
end

local p = WowDecompBuildPage("WowDecompTreeWindPanel", "Esteria Tree Wind", "Placed trees and bushes sway gently in the wind. Purely visual: collision and selection do not move.")
p:Header("Trees")
p:Check("treeWind", "Trees sway", "Master switch for the trees.")
p:Check("treeWindFollowWorld", "Follow the world wind", "Where WindProfile.cdbc gives a place wind, its heading, strength and passing gusts drive the trees and bushes; calm places stand still. Elsewhere the sliders below apply as they are.")
p:Slider("treeWindWorldScale", "World wind response", "Sway per unit of world wind. At 2, a half-strength wind gives the sway set below.", 0, 10, 0.1, Plain, 0.1)
WindRows(p, "treeWind", "trees", "tree")
p:Header("What counts as a tree")
p:Check("treeWindDoodadsOnly", "Placed objects only", "Only trees placed in the map sway, never units, weapons or spell effects.")
p:Check("treeWindMatchNames", "Match tree names", "The model's name must name a tree (tree, pine, oak, willow and so on).")
p:Check("treeWindMatchTallThin", "Match tall models", "The model must also be tall and narrow. Off by default: names alone decide.")
p:Check("treeWindExcludeDead", "Skip dead and bare trees", "Dead, burnt, bare and felled trees, stumps and logs stay still.")
p:Slider("treeWindMinHeight", "Least height", "Tall-model test: the model must be at least this tall.", 0, 40, 0.5, Yards, 1)
p:Slider("treeWindMinAspect", "Least height to width", "Tall-model test: height divided by the widest side.", 0.5, 6, 0.05, Plain, 0.1)
p:Slider("treeWindMaxHeight", "Greatest height", "Trees taller than this keep still, so giant world trees do not lean across the zone. 0 is unlimited.", 0, 300, 5, Limit, 5)
p:Slider("treeWindMaxDistance", "Greatest distance", "Trees further away than this keep still. 0 is unlimited.", 0, 500, 5, Limit, 5)
p:Header("Bushes")
p:Check("bushWind", "Bushes sway", "Bushes, shrubs, ferns and hedges, matched by name only: the tree size tests never apply.")
WindRows(p, "bushWind", "bushes", "bush")
p:Check("bushWindDoodadsOnly", "Placed objects only", "Only bushes placed in the map sway.")
p:Check("bushWindExcludeDead", "Skip dead and bare bushes", "Dead, burnt and bare bushes stay still.")
p:Slider("bushWindMaxDistance", "Greatest distance", "Bushes further away than this keep still. 0 is unlimited.", 0, 500, 5, Limit, 5)
p:Done()
)lua";

    // Snow, sand and ash lying on the ground (WorldClient/SurfaceCover.cpp).
    static const char surfaceCoverScript[] = R"lua(
if not VideoOptionsFrame or not WowDecompBuildPage or WowDecompSurfaceCoverPanel then return end

local function Times(v) return string.format("%.2fx", v) end
local function Plain(v) return string.format("%.2f", v) end
local function Yards(v) return string.format("%.2f yd", v) end

local p = WowDecompBuildPage("WowDecompSurfaceCoverPanel", "Esteria Snow Cover", "Painted snow lies on the ground as a layer of its own, from SurfaceCover.cdbc: a few inches deep on flat ground, thinning on slopes, burying the grass, lit and shadowed like the ground. Near you it is lumpy and takes trenches.")
p:Header("Cover")
p:Check("surfaceCover", "Snow lies on the ground", "Master switch. Needs its shaders translated once on Vulkan (surfaceCoverInfo says so).")
p:Check("surfaceCoverOthers", "Sand and ash too", "The table's other covers: a thin layer of sand in deserts, ash in burnt lands. Off: only snow.")
p:Slider("surfaceCoverDepth", "Depth", "Multiplier on how deep the table says the cover lies. 1 is as the table says.", 0, 3, 0.05, Times, 0.05)
p:Slider("surfaceCoverBreakup", "Ragged edges", "Multiplier on how patchy the cover's edges are. 0 gives smooth, rounded edges.", 0, 3, 0.05, Times, 0.05)
p:Slider("surfaceCoverBrightness", "Brightness", "How bright the cover is under the same light as the ground.", 0.2, 1.5, 0.01, Plain, 0.02)
p:Slider("surfaceCoverDistanceLift", "Lift far away", "Far away the cover is lifted this much so it never flickers with the ground under it. Keep it small: more makes distant snow float.", 0, 3, 0.05, Yards, 0.05)
p:Header("Near you")
p:Check("surfaceCoverNear", "Fine snow around you", "A detailed, lumpy patch of cover 48 yards across follows you, lit by its own bumps. Off: the coarser cover everywhere, and no trenches.")
p:Check("surfaceCoverTrenches", "Trenches", "You and the creatures and players near you press trenches into the cover, with pushed-up rims. They fill back in over time.")
p:Slider("surfaceCoverTrenchScale", "Trench width", "Width of a trench as a multiple of each one's size. Mounts make wider ones.", 0.2, 4, 0.05, Times, 0.05)
p:Slider("surfaceCoverNearRange", "Fine snow range", "How far across the fine patch reaches. 48 yards is the finest; wider reaches further but is coarser (trenches get blurrier) and costs more.", 24, 192, 4, Yards, 4)
p:Header("Fallen snow")
p:Check("surfaceCoverFallen", "Snow falls and lies", "In snowstorms on ground below freezing, snow builds up on bare ground in the fine patch, and melts when it is warm. Kept per zone.")
p:Check("surfaceCoverMelt", "Painted snow melts", "Snow painted on the ground thins in warm weather, its edges retreating, and grows back when it is cold or snowing.")
p:Slider("surfaceCoverFallenRate", "Snowfall and melting speed", "How many times faster snow builds up and melts. 1 is about 18 cm an hour in a full storm.", 0, 200, 1, Times, 1)
p:Done()
)lua";

    // The world wind and the grass (WorldClient/LivingWind.cpp, WorldClient/HD/HdGrassWind.cpp).
    static const char worldWindScript[] = R"lua(
if not VideoOptionsFrame or not WowDecompBuildPage or WowDecompWorldWindPanel then return end

local function Plain(v) return string.format("%.2f", v) end
local function Times(v) return string.format("%.2fx", v) end
local function Heading(v) return string.format("%d deg", v + 0.5) end
local function Speed(v) return string.format("%.1f yd/s", v) end
local function Yards(v) return string.format("%.1f yd", v) end
local function Distance(v) return string.format("%d yd", v + 0.5) end

local p = WowDecompBuildPage("WowDecompWorldWindPanel", "Esteria World Wind", "Wind per zone from WindProfile.cdbc: stronger in storms, with gusts that travel across the land, calm under roofs and behind walls. It moves the grass and the trees.")
p:Header("World wind")
p:Check("worldWind", "Enable the world wind", "Off: the trees and the grass use their own fixed wind (the sliders on their pages).")
p:Slider("worldWindStrength", "Strength", "Your multiplier on every place's wind. 1 is as the table says.", 0, 3, 0.05, Times, 0.05)
p:Check("worldWindShelter", "Shelter under roofs", "Roofs and overhangs keep the ground below calm.")
p:Check("worldWindLee", "Wind shadow behind walls", "Walls, cliffs and steep rock calm the air on their downwind side.")
p:Header("Grass")
p:Check("hdGrassWind", "Grass sways", "Grass moves in the wind, in every visuals mode. Outside Azeroth Remastered the grass keeps the game's own lighting.")
p:Check("hdGrassWindFollowWorld", "Follow the world wind", "Where the world wind blows, it sets the grass's heading and strength, and gusts pass over it.")
p:Slider("hdGrassWindStrength", "Sway", "Multiplier on the grass sway.", 0, 5, 0.05, Times, 0.05)
p:Slider("hdGrassWindSpeed", "Wave speed", "How fast the waves cross the grass.", 0, 12, 0.1, Speed, 0.1)
p:Slider("hdGrassWindDirection", "Fixed wind direction", "Heading of the grass wind where the world wind does not blow.", 0, 360, 1, Heading, 5)
p:Check("hdGrassReact", "Grass parts around you", "Grass bends away from your character.")
p:Slider("hdGrassReactRadius", "Parting radius", "How far around you the grass bends away.", 0.1, 20, 0.1, Yards, 0.5)
p:Check("hdGrassReactUnits", "Grass parts around others", "Creatures and other players near you part the grass too, each by its own size.")
p:Slider("hdGrassReactUnitScale", "Their parting size", "Parting radius around a creature or player, as a multiple of its size.", 0.5, 5, 0.1, Times, 0.1)
p:Slider("hdGrassReactMountedScale", "Wider when mounted", "How much wider the parting is around anyone mounted, you included.", 1, 5, 0.1, Times, 0.1)
p:Slider("groundEffectDist", "Grass distance", "How far grass is drawn. The game's own limit of 140 yards is lifted. Grass in range is baked once and kept on the GPU, up to the memory below.", 0, 1024, 5, Distance, 10)
p:Slider("grassBufferMemory", "Grass memory", "Most memory the kept grass may use. Grass past it is baked again every frame, which costs CPU time. Takes effect when the grass distance changes or the world loads.", 16, 4096, 16, function(v) return string.format("%d MB", v + 0.5) end, 64)
p:Header("Grass density")
p:Check("grassDensity", "Denser grass near you", "Extra plants on each plant's own patch of ground, as GroundEffectDoodadDensity.cdbc asks for each place and plant. They share the grass memory, so the grass reaches less far.")
p:Slider("grassDensityMaxMultiplier", "Most plants per plant", "Your limit on how dense grass gets. 1 is never denser.", 1, 8, 0.5, Times, 0.5)
p:Slider("grassDensityMaxRadius", "Dense radius", "Your limit on how far around you grass is made denser.", 0, 500, 5, Distance, 10)
p:Slider("grassDensityMaxSpread", "Spread", "Your limit on how far an extra plant may sit from its plant.", 0, 10, 0.1, Yards, 0.5)
p:Header("Far grass")
p:Check("grassThinning", "Thin far grass", "Far away, grass draws an even sample of its plants: much less GPU time at long grass distances, hardly visible.")
p:Slider("grassThinStart", "Full grass within", "Every plant is drawn this close to you.", 0, 1000, 5, Distance, 10)
p:Slider("grassThinEnd", "Thinnest from", "Where the thinning reaches its lowest share.", 0, 2000, 10, Distance, 25)
p:Slider("grassThinMinShare", "Thinnest share", "Share of plants drawn at that distance and beyond.", 0.05, 1, 0.05, function(v) return string.format("%d%%", v * 100 + 0.5) end, 0.05)
p:Done()
)lua";

    static const char overlayScript[] = R"lua(
-- The overlay itself. Created hidden and shown by the showPerformance CVar; a hidden frame runs no OnUpdate, so it
-- costs nothing while it is off. The position is saved in perfOverlayPos when it is dropped.
if not WowDecompPerfOverlay then
    local f = CreateFrame("Frame", "WowDecompPerfOverlay", UIParent)
    f:SetFrameStrata("LOW")
    f:SetSize(190, 78)
    f:SetMovable(true)
    f:SetClampedToScreen(true)
    f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    local text = f:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    text:SetPoint("TOPLEFT", 8, -8)
    text:SetJustifyH("LEFT")

    f:SetScript("OnDragStart", function(self)
        if GetCVar("perfOverlayLocked") ~= "1" then self:StartMoving() end
    end)
    f:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        local x, y = self:GetCenter()
        local scale = self:GetEffectiveScale() / UIParent:GetEffectiveScale()
        SetCVar("perfOverlayPos", string.format("%.0f,%.0f", x * scale, y * scale))
    end)

    local function Number(v)
        if v >= 1000000 then return string.format("%.2fM", v / 1000000) end
        if v >= 1000 then return string.format("%.1fk", v / 1000) end
        return string.format("%d", v)
    end

    local function Look()
        local scale = tonumber(GetCVar("perfOverlayScale")) or 1
        f:SetScale(scale)
        f:SetAlpha(tonumber(GetCVar("perfOverlayAlpha")) or 0.8)
        local shown = GetCVar("perfOverlayBackground") ~= "0"
        f:SetBackdropColor(0, 0, 0, shown and 0.6 or 0)
        f:SetBackdropBorderColor(1, 1, 1, shown and 0.4 or 0)
        f:ClearAllPoints()
        local x, y = string.match(GetCVar("perfOverlayPos") or "", "^(-?%d+),(-?%d+)$")
        if x then
            f:SetPoint("CENTER", UIParent, "BOTTOMLEFT", tonumber(x) / scale, tonumber(y) / scale)
        else
            f:SetPoint("TOPRIGHT", UIParent, "TOPRIGHT", -8, -8)
        end
    end

    local elapsed = 1
    f:SetScript("OnUpdate", function(self, dt)
        elapsed = elapsed + dt
        if elapsed < (tonumber(GetCVar("perfOverlayInterval")) or 0.25) then return end
        elapsed = 0
        local fps, ms, draws, tris, cpu = GetPerformanceStats()
        local lines = {}
        if GetCVar("perfOverlayFps") ~= "0" then lines[#lines + 1] = string.format("FPS %.1f", fps) end
        if GetCVar("perfOverlayFrameTime") ~= "0" then lines[#lines + 1] = string.format("Frame %.2f ms", ms) end
        if GetCVar("perfOverlayDrawCalls") ~= "0" then lines[#lines + 1] = string.format("Draw %d", draws) end
        if GetCVar("perfOverlayTriangles") ~= "0" then lines[#lines + 1] = "Tris " .. Number(tris) end
        if GetCVar("perfOverlayCpu") ~= "0" then lines[#lines + 1] = string.format("CPU %.1f%%", cpu) end
        if #lines == 0 then lines[1] = "(nothing selected)" end
        text:SetText(table.concat(lines, "\n"))
        f:SetHeight(#lines * 12 + 18)
    end)

    local function Apply()
        if GetCVar("showPerformance") == "1" then
            Look()
            elapsed = 1
            f:Show()
        else
            f:Hide()
        end
    end
    hooksecurefunc("SetCVar", function(name)
        if name and string.find(string.lower(name), "^perfoverlay") then Look() end
        if name and string.lower(name) == "showperformance" then Apply() end
    end)
    f:RegisterEvent("PLAYER_ENTERING_WORLD")
    f:SetScript("OnEvent", Apply)
    Apply()
end
)lua";
    FrameScript_Execute(pagesScript, "NativeEsteriaPageHelpers", nullptr);
    FrameScript_Execute(pagesScript2, "NativeEsteriaPages", nullptr);
    FrameScript_Execute(treeWindScript, "NativeTreeWindPage", nullptr);
    FrameScript_Execute(worldWindScript, "NativeWorldWindPage", nullptr);
    FrameScript_Execute(surfaceCoverScript, "NativeSurfaceCoverPage", nullptr);
    const std::string lumenveilOptions = LumenveilOptions::BuildScript();
    FrameScript_Execute(lumenveilOptions.c_str(), "NativeLumenveilOptions", nullptr);
    FrameScript_Execute(overlayScript, "NativeEsteriaOverlay", nullptr);
}
