-- Waystone HD: small gear button next to the micro-button bar (character, spells, ..., menu, help).
-- Left click = opens / closes the /hd panel (no need to type /hd any more).
-- It follows the right-most micro-button (Blizzard bar or DragonUI), even after it is moved.
local MICRO = { "CharacterMicroButton", "SpellbookMicroButton", "TalentMicroButton", "AchievementMicroButton",
    "QuestLogMicroButton", "SocialsMicroButton", "PVPMicroButton", "LFDMicroButton", "MainMenuMicroButton",
    "HelpMicroButton" }

local button = CreateFrame("Button", "WaystoneHDMicroButton", UIParent)
button:SetFrameStrata("MEDIUM")
button:Hide()
local border = button:CreateTexture(nil, "BACKGROUND")
border:SetTexture(0, 0, 0, 0.85)
border:SetAllPoints()
local icon = button:CreateTexture(nil, "ARTWORK")
icon:SetTexture("Interface\\Icons\\INV_Misc_Gear_01")
icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
icon:SetPoint("TOPLEFT", 1, -1); icon:SetPoint("BOTTOMRIGHT", -1, 1)
button:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
button:RegisterForClicks("LeftButtonUp")
button:SetScript("OnMouseDown", function() icon:SetPoint("TOPLEFT", 2, -2); icon:SetPoint("BOTTOMRIGHT", 0, 0) end)
button:SetScript("OnMouseUp", function() icon:SetPoint("TOPLEFT", 1, -1); icon:SetPoint("BOTTOMRIGHT", -1, 1) end)
button:SetScript("OnClick", function()
    local open = SlashCmdList and SlashCmdList["WAYSTONEHD"]
    if open then open("") end
end)
button:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOPLEFT")
    GameTooltip:SetText("Waystone HD")
    GameTooltip:AddLine("HD graphics settings (same as /hd)", 1, 1, 1)
    GameTooltip:Show()
end)
button:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- The right-most visible micro-button: we sit to its right, at its size and scale.
local current, lastKey
local function Place()
    local best, right
    for _, name in ipairs(MICRO) do
        local b = _G[name]
        if b and b:IsVisible() and b:GetRight() then
            local r = b:GetRight() * b:GetEffectiveScale()
            if not right or r > right then best, right = b, r end
        end
    end
    if not best then button:Hide(); current = nil; return end
    local w, h = best:GetWidth(), best:GetHeight()
    local dragon = _G.pUiMicroMenu ~= nil
    -- Blizzard bar: the art fills the bottom of the button (28x58). DragonUI: full button (32x40).
    local side = dragon and math.floor(w * 0.68 + 0.5) or math.floor(w * 0.8 + 0.5)
    local key = tostring(best) .. side .. best:GetEffectiveScale() .. (best:GetRight() or 0) .. (best:GetBottom() or 0)
    if key ~= lastKey then
        lastKey = key
        button:SetScale(best:GetEffectiveScale() / UIParent:GetEffectiveScale())
        button:SetWidth(side); button:SetHeight(side)
        button:ClearAllPoints()
        if dragon then button:SetPoint("LEFT", best, "RIGHT", 0, 0)
        else button:SetPoint("BOTTOMLEFT", best, "BOTTOMRIGHT", 1, 3) end
    end
    current = best
    if not button:IsShown() then button:Show() end
end

local wait = 0
local watcher = CreateFrame("Frame")
watcher:RegisterEvent("PLAYER_ENTERING_WORLD")
watcher:RegisterEvent("UNIT_ENTERED_VEHICLE")
watcher:RegisterEvent("UNIT_EXITED_VEHICLE")
watcher:SetScript("OnEvent", function() wait = 0 end)
watcher:SetScript("OnUpdate", function(self, elapsed)
    wait = wait - elapsed
    if wait > 0 then return end
    wait = 1
    if not InCombatLockdown() then Place() end
end)
