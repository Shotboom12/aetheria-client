-- Waystone HD: light by zone and time of day (3 Nov 2026; English edition).
-- Set the light to your taste in a zone at a time of day, then type /hdtime to keep that point.
-- The game remembers the point (zone, hour, values). Then, in that zone, the values follow the time of day:
-- between two points they glide from one to the other; with a single point the value stays fixed.
-- In a zone without a point the earlier values come back. Everything is kept in WaystoneHDHeures (SavedVariables).

local SUIVIS = { "hdLvCfg_SunScatter", "hdLvCfg_ViewLight", "hdLvCfg_AntiGlare", "hdLvCfg_ShaftGlare" }
local NOMS = { hdLvCfg_SunScatter = "sun in the haze", hdLvCfg_ViewLight = "angle", hdLvCfg_AntiGlare = "anti-glare", hdLvCfg_ShaftGlare = "ray glare" }

local function Read(name) local ok, v = pcall(GetCVar, name); if ok then return v end end
local function Has(name) return Read(name) ~= nil end
local function Say(text) if DEFAULT_CHAT_FRAME then DEFAULT_CHAT_FRAME:AddMessage("|cffffd200Waystone HD|r : " .. text) end end

local function Zone()
    local z = GetRealZoneText and GetRealZoneText() or nil
    if not z or z == "" then z = GetZoneText and GetZoneText() or "?" end
    return z
end

-- game time in hours (19.5 = 19:30); if the time is forced by the hdTime slider, that one counts
local function Heure()
    local forced = tonumber(Read("hdTime") or "")
    if forced and forced >= 0 then return forced % 24 end
    local h, m = 12, 0
    if GetGameTime then h, m = GetGameTime() end
    return (h + m / 60) % 24
end

local function HeureTexte(h)
    local hh = math.floor(h); local mm = math.floor((h - hh) * 60 + 0.5)
    if mm == 60 then hh = (hh + 1) % 24; mm = 0 end
    return string.format("%d:%02d", hh, mm)
end

local function Base()
    if type(WaystoneHDHeures) ~= "table" then WaystoneHDHeures = {} end
    return WaystoneHDHeures
end

-- value at time t from the points (sorted by hour), wrapping around midnight
local function Valeur(points, name, t)
    local list = {}
    for _, p in ipairs(points) do if p.v and p.v[name] then list[#list + 1] = p end end
    if #list == 0 then return nil end
    if #list == 1 then return list[1].v[name] end
    table.sort(list, function(a, b) return a.h < b.h end)
    local prev, next = list[#list], list[1]
    for i = 1, #list do
        if list[i].h <= t then prev = list[i]; next = list[i + 1] or list[1] end
    end
    if t < list[1].h then prev = list[#list]; next = list[1] end
    local span = (next.h - prev.h) % 24
    if span == 0 then return prev.v[name] end
    local u = ((t - prev.h) % 24) / span
    u = u * u * (3 - 2 * u)   -- smooth start and end, no kink when passing a point
    return prev.v[name] + (next.v[name] - prev.v[name]) * u
end

local avant = {}   -- values from before the automatic mode, restored when leaving a tuned zone
local pose = {}    -- last value set by the automatic mode
local pause = {}
local function _() end   -- setting changed by hand (/console): the automatic mode leaves it alone until a point is kept
-- Never a jump: the value glides toward the target (half way in ~3 s), on entering or leaving
-- a zone, after keeping or erasing. Between two points the target itself follows the time of day smoothly.
local DEMI_VIE = 3.0
local function Glisser(name, cur, target, dt)
    local k = 1 - 0.5 ^ (dt / DEMI_VIE)
    local x = cur + (target - cur) * k
    if math.abs(target - x) < 0.002 then x = target end
    return x
end
local function Appliquer(dt)
    dt = dt or 0.5
    local points = Base()[Zone()]
    local t = Heure()
    for _, name in ipairs(SUIVIS) do
        if Has(name) then
            local cur = tonumber(Read(name) or "")
            if pose[name] and cur and math.abs(cur - pose[name]) > 0.0015 then pause[name] = true end
            local v = points and Valeur(points, name, t)
            local target
            if v and not pause[name] then
                if avant[name] == nil then avant[name] = cur end
                target = v
            elseif not v and avant[name] ~= nil and not pause[name] then
                target = avant[name]
            end
            if target and cur then
                local x = Glisser(name, cur, target, dt)
                if math.abs(x - cur) > 0.00001 then pcall(SetCVar, name, string.format("%.4f", x)) end
                pose[name] = tonumber(Read(name) or "") or x
                if not v and x == target then avant[name] = nil; pose[name] = nil end
            elseif not v then
                avant[name] = nil; pose[name] = nil; pause[name] = nil
            end
        end
    end
end

function WaystoneHD_HeuresGarder()
    local zone, h = Zone(), Heure()
    local pts = Base()[zone] or {}
    local v, n = {}, 0
    for _, name in ipairs(SUIVIS) do
        local x = tonumber(Read(name) or "")
        if x then v[name] = x; n = n + 1 end
    end
    if n == 0 then Say("no Lumenveil light setting found in this game."); return end
    -- a point within 15 minutes is replaced
    for i = #pts, 1, -1 do
        local d = math.abs(pts[i].h - h); d = math.min(d, 24 - d)
        if d < 0.25 then table.remove(pts, i) end
    end
    pts[#pts + 1] = { h = h, v = v }
    table.sort(pts, function(a, b) return a.h < b.h end)
    Base()[zone] = pts
    pause = {}
    for k, x in pairs(v) do pose[k] = x end
    Say(string.format("kept for %s at %s (sun in the haze %.2f). %d point(s) in this zone.", zone, HeureTexte(h), v.hdLvCfg_SunScatter or -1, #pts))
end

function WaystoneHD_HeuresEffacer()
    local zone = Zone()
    Base()[zone] = nil
    pause = {}   -- the earlier values glide back (Appliquer)
    Say("points erased for " .. zone .. ".")
end

function WaystoneHD_HeuresTexte()
    local zone, h = Zone(), Heure()
    local pts = Base()[zone]
    local s = zone .. ", " .. HeureTexte(h) .. " : "
    if not pts or #pts == 0 then return s .. "no point (normal settings)." end
    local parts = {}
    for _, p in ipairs(pts) do parts[#parts + 1] = HeureTexte(p.h) .. " = " .. string.format("%.2f", p.v.hdLvCfg_SunScatter or -1) end
    if pause.hdLvCfg_SunScatter then return s .. #pts .. " point(s) [" .. table.concat(parts, ", ") .. "], PAUSED: you changed it by hand, type /hdtime to keep your value" end
    local now = Valeur(pts, "hdLvCfg_SunScatter", h)
    return s .. #pts .. " point(s) [" .. table.concat(parts, ", ") .. "], sun in the haze now " .. (now and string.format("%.2f", now) or "-")
end

SLASH_WAYSTONEHDHEURE1 = "/hdtime"
SLASH_WAYSTONEHDHEURE2 = "/hdheure"
SlashCmdList.WAYSTONEHDHEURE = function(msg)
    msg = (msg or ""):lower()
    if msg == "erase" or msg == "efface" or msg == "effacer" then WaystoneHD_HeuresEffacer()
    elseif msg == "list" or msg == "liste" then Say(WaystoneHD_HeuresTexte())
    elseif msg == "" or msg == "keep" or msg == "garde" or msg == "garder" then WaystoneHD_HeuresGarder()
    else Say(WaystoneHD_HeuresTexte()) end
end

local f = CreateFrame("Frame")
local acc = 0
f:SetScript("OnUpdate", function(_, dt) acc = acc + (dt or 0); if acc >= 0.25 then Appliquer(acc); acc = 0 end end)
WaystoneHD_HeuresAppliquer = Appliquer
