BINDING_HEADER_AETHERIA_ACTION_CAMERA = "Aetheria Action Camera"
BINDING_NAME_AETHERIA_WAIST_LANTERN = "Toggle waist lantern"
AetheriaActionCameraProfile = type(AetheriaActionCameraProfile) == "table" and AetheriaActionCameraProfile or {}

local driver = CreateFrame("Frame")
local reticle = CreateFrame("Frame", nil, UIParent)
reticle:SetPoint("CENTER", UIParent, "CENTER")
reticle:SetSize(12, 12)
reticle:EnableMouse(false)
reticle:Hide()

local function AddReticleStroke(width, height)
    local stroke = reticle:CreateTexture(nil, "OVERLAY")
    stroke:SetTexture(1, 1, 1, 0.85)
    stroke:SetPoint("CENTER")
    stroke:SetSize(width, height)
    return stroke
end

local horizontal = AddReticleStroke(12, 2)
local vertical = AddReticleStroke(2, 12)
local art = reticle:CreateTexture(nil, "OVERLAY")
art:SetPoint("CENTER")
art:SetSize(40, 40)
local artPath
local bloomPieces = {}
local bloomPiecePaths = {}
local cuts = { 0, 0.4, 0.6, 1 }
local function DrawBloom(gap)
    if gap <= 0 or not artPath then
        for _, piece in ipairs(bloomPieces) do piece:Hide() end
        art:SetAlpha(1)
        return
    end

    for row = 1, 3 do
        for column = 1, 3 do
            local index = (row - 1) * 3 + column
            local left, right = cuts[column], cuts[column + 1]
            local top, bottom = cuts[row], cuts[row + 1]
            local piece = bloomPieces[index]
            if not piece then
                piece = reticle:CreateTexture(nil, "OVERLAY")
                bloomPieces[index] = piece
                piece:SetSize(40 * (right - left), 40 * (bottom - top))
                piece:SetTexCoord(left, right, top, bottom)
            end
            if bloomPiecePaths[index] ~= artPath then
                if not piece:SetTexture(artPath) then
                    DrawBloom(0)
                    return
                end
                bloomPiecePaths[index] = artPath
            end
            piece:ClearAllPoints()
            piece:SetPoint("TOPLEFT", reticle, "CENTER",
                40 * (left - 0.5) + (column - 2) * gap,
                40 * (0.5 - top) - (row - 2) * gap)
            piece:Show()
        end
    end
    art:SetAlpha(0)
end
local bloomEnd
local bloomDuration = 0.18
local bloomSize = 8
local rangedItemID
local lastShot
local weaponPulse = {
    [2] = {14, 0.24}, -- Bow
    [3] = {7, 0.14},  -- Gun
    [18] = {22, 0.45}, -- Crossbow
}

local function RefreshRangedItem()
    local slot = GetInventorySlotInfo("RangedSlot")
    local link = slot and GetInventoryItemLink("player", slot)
    rangedItemID = link and tonumber(link:match("item:(%d+)"))
    lastShot = nil
end

local shapes = {
    arc = "Arc", brackets = "Brackets", compass = "Compass", diamond = "Diamond",
    dot = "Dot", leaf = "Leaf", nexus = "Nexus", ring = "Ring", runic = "Runic",
    spire = "Spire", split = "Split", star = "Star",
}
local themes = { neutral = "Neutral", bronze = "WarmBronze" }

local function SetReticle(shape, theme)
    local path = shape and ("Interface\\AddOns\\AetheriaActionCamera\\Reticles\\" ..
        shape .. "\\" .. theme .. ".tga")
    artPath = nil
    if path and art:SetTexture(path) then
        artPath = path
        art:Show()
        horizontal:Hide()
        vertical:Hide()
    else
        art:Hide()
        horizontal:Show()
        vertical:Show()
    end
end

local function SelectReticle(shape, theme)
    bloomEnd = nil
    DrawBloom(0)
    AetheriaActionCameraReticleShape = shape
    AetheriaActionCameraReticleTheme = theme
    SetReticle(shapes[shape], themes[theme])
end

SelectReticle("ring", "neutral")

SLASH_AETHERIARETICLE1 = "/aetheriareticle"
SlashCmdList.AETHERIARETICLE = function(message)
    local shape, theme = string.lower(message or ""):match("^%s*(%a+)%s*(%a*)%s*$")
    if shape == "simple" and (theme == "" or themes[theme]) then
        SelectReticle(shape, theme ~= "" and theme or "neutral")
    elseif shapes[shape] and (theme == "" or themes[theme]) then
        SelectReticle(shape, theme ~= "" and theme or "neutral")
    else
        print("Usage: /aetheriareticle <arc|brackets|compass|diamond|dot|leaf|nexus|ring|runic|spire|split|star> [neutral|bronze], or /aetheriareticle simple")
    end
end

local cursorFrames = {
    "GameMenuFrame", "InterfaceOptionsFrame", "VideoOptionsFrame", "WorldMapFrame",
    "GossipFrame", "QuestFrame", "MerchantFrame", "LootFrame", "TaxiFrame",
    "BankFrame", "AuctionFrame", "MailFrame", "TradeFrame", "SpellBookFrame",
    "CharacterFrame", "DGossipFrame", "DQuestFrame", "DBookFrame",
}

local inWorld = false
local ownsMouseLook = false
local activationDeadline
local resumeAfterInteraction = false
local cursorFreeSince
local interactionEvents = {
    GOSSIP_SHOW = true, LOOT_OPENED = true, MERCHANT_SHOW = true,
    QUEST_GREETING = true, QUEST_DETAIL = true, QUEST_PROGRESS = true, QUEST_COMPLETE = true,
}
local dialogueFrames = { "GossipFrame", "QuestFrame", "DGossipFrame", "DQuestFrame" }

local function CursorNeeded()
    if AetheriaActionCameraNativeMenuOpen or GetCurrentKeyBoardFocus() or SpellIsTargeting() or CursorHasItem() or
        CursorHasSpell() or CursorHasMacro() then
        return true
    end
    for _, name in ipairs(cursorFrames) do
        local frame = _G[name]
        if frame and frame:IsVisible() then
            return true
        end
    end
    return false
end

local function ReleaseMouseLook()
    bloomEnd = nil
    DrawBloom(0)
    activationDeadline = nil
    resumeAfterInteraction = false
    cursorFreeSince = nil
    AetheriaActionCameraCenterHover = nil
    if ownsMouseLook then
        ownsMouseLook = false
        if IsMouselooking() then
            MouselookStop()
        end
    end
    reticle:Hide()
end

local function PauseMouseLook()
    ReleaseMouseLook()
    resumeAfterInteraction = true
end

AetheriaActionCameraReleaseMouseLook = ReleaseMouseLook

driver:RegisterEvent("PLAYER_ENTERING_WORLD")
driver:RegisterEvent("PLAYER_LEAVING_WORLD")
driver:RegisterEvent("PLAYER_DEAD")
driver:RegisterEvent("PLAYER_CONTROL_LOST")
driver:RegisterEvent("CINEMATIC_START")
driver:RegisterEvent("COMBAT_LOG_EVENT_UNFILTERED")
driver:RegisterEvent("PLAYER_EQUIPMENT_CHANGED")
driver:RegisterEvent("PLAYER_LOGOUT")
for event in pairs(interactionEvents) do
    driver:RegisterEvent(event)
end
driver:SetScript("OnEvent", function(_, event, timestamp, combatEvent, sourceGUID)
    if event == "COMBAT_LOG_EVENT_UNFILTERED" then
        if not AetheriaReadRangedFeedback and ownsMouseLook and art:IsShown() and
            AetheriaActionCameraReticleShape ~= "dot" and
            (combatEvent == "RANGE_DAMAGE" or combatEvent == "RANGE_MISSED") and
            sourceGUID == UnitGUID("player") then
            bloomEnd = GetTime() + bloomDuration
        end
    elseif event == "PLAYER_EQUIPMENT_CHANGED" then
        RefreshRangedItem()
        bloomEnd = nil
        DrawBloom(0)
    elseif event == "PLAYER_ENTERING_WORLD" then
        inWorld = true
        if type(AetheriaActionCameraHunterProfile) ~= "table" then
            AetheriaActionCameraHunterProfile = {}
        end
        RefreshRangedItem()
    elseif event == "PLAYER_LEAVING_WORLD" then
        ReleaseMouseLook()
        inWorld = false
    elseif event == "PLAYER_LOGOUT" then
        if AetheriaSaveActionCameraProfile then
            AetheriaSaveActionCameraProfile()
        end
        ReleaseMouseLook()
    elseif interactionEvents[event] then
        if ownsMouseLook or resumeAfterInteraction then
            PauseMouseLook()
        end
    else
        ReleaseMouseLook()
    end
end)

driver:SetScript("OnUpdate", function()
    if ownsMouseLook and AetheriaReadRangedFeedback then
        local shot, subclass = AetheriaReadRangedFeedback(rangedItemID)
        local pulse = weaponPulse[subclass]
        if lastShot and shot ~= lastShot and pulse and art:IsShown() and
            AetheriaActionCameraReticleShape ~= "dot" then
            bloomSize, bloomDuration = pulse[1], pulse[2]
            bloomEnd = GetTime() + bloomDuration
        end
        lastShot = shot
    else
        lastShot = nil
    end
    local dialogueOpen = false
    if inWorld then
        for _, name in ipairs(dialogueFrames) do
            local frame = _G[name]
            if frame and frame:IsVisible() then
                dialogueOpen = true
                break
            end
        end
    end
    AetheriaActionCameraDialogueOpen = dialogueOpen

    if bloomEnd then
        local remaining = bloomEnd - GetTime()
        if remaining <= 0 then
            bloomEnd = nil
            DrawBloom(0)
        else
            DrawBloom(bloomSize * remaining / bloomDuration)
        end
    end
    if ownsMouseLook then
        if CursorNeeded() then
            PauseMouseLook()
        elseif not IsMouselooking() then
            ReleaseMouseLook()
        end
    end
    if resumeAfterInteraction then
        if CursorNeeded() or IsMouselooking() then
            cursorFreeSince = nil
        else
            local now = GetTime()
            cursorFreeSince = cursorFreeSince or now
            if now - cursorFreeSince >= 0.15 then
                resumeAfterInteraction = false
                cursorFreeSince = nil
                activationDeadline = now + 1
            end
        end
    end
    if activationDeadline then
        if not inWorld or UnitIsDeadOrGhost("player") or GetTime() >= activationDeadline then
            activationDeadline = nil
            print("Action Camera could not start. Close menus, finish targeting, and try again.")
        elseif not CursorNeeded() then
            activationDeadline = nil
            if IsMouselooking() then
                print("Action Camera could not start because mouse look is already active.")
                return
            end
            MouselookStart()
            ownsMouseLook = IsMouselooking() and true or false
            if ownsMouseLook then
                AetheriaActionCameraCenterHover = true
                reticle:Show()
            else
                print("Action Camera could not start native mouse look.")
            end
        end
    end
end)

SLASH_AETHERIAACTIONCAM1 = "/aetheriaactioncam"
SlashCmdList.AETHERIAACTIONCAM = function(message)
    if message and message:match("^%s*status%s*$") then
        print("Action Camera: " .. (ownsMouseLook and "active" or resumeAfterInteraction and "paused for cursor"
            or activationDeadline and "waiting for cursor" or "off"))
    elseif ownsMouseLook or activationDeadline or resumeAfterInteraction then
        ReleaseMouseLook()
    elseif inWorld and not UnitIsDeadOrGhost("player") then
        -- Slash handlers may run before the chat edit box has released keyboard focus.
        activationDeadline = GetTime() + 1
    else
        print("Action Camera is available while your character is alive in the world.")
    end
end
