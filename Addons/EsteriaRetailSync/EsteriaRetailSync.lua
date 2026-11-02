local prefix = "ERCP1"
local alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
local decodeMap = {}
local pendingChunks = {}
local pendingAppearances = {}

for index = 1, #alphabet do
    decodeMap[string.byte(alphabet, index)] = index - 1
end

local function DecodeBase64(value)
    local output = {}
    for index = 1, #value, 4 do
        local a = decodeMap[string.byte(value, index)]
        local b = decodeMap[string.byte(value, index + 1)]
        local c = decodeMap[string.byte(value, index + 2)]
        local d = decodeMap[string.byte(value, index + 3)]
        if a == nil or b == nil then
            return nil
        end

        local combined = a * 262144 + b * 4096 + (c or 0) * 64 + (d or 0)
        output[#output + 1] = string.char(math.floor(combined / 65536) % 256)
        if string.byte(value, index + 2) ~= 61 then
            output[#output + 1] = string.char(math.floor(combined / 256) % 256)
        end
        if string.byte(value, index + 3) ~= 61 then
            output[#output + 1] = string.char(combined % 256)
        end
    end
    return table.concat(output)
end

local frame = CreateFrame("Frame")
local retryElapsed = 0
frame:SetScript("OnUpdate", function(_, elapsed)
    retryElapsed = retryElapsed + elapsed
    if retryElapsed < 0.25 then return end
    retryElapsed = 0
    if not EsteriaRetailAppearanceReceive then return end
    for guid, appearance in pairs(pendingAppearances) do
        EsteriaRetailAppearanceReceive(guid, appearance)
        pendingAppearances[guid] = nil
    end
end)
frame:RegisterEvent("PLAYER_LOGIN")
frame:RegisterEvent("CHAT_MSG_ADDON")
frame:SetScript("OnEvent", function(_, event, messagePrefix, message)
    if event == "PLAYER_LOGIN" then
        if RegisterAddonMessagePrefix then
            RegisterAddonMessagePrefix(prefix)
        end
        return
    end

    if messagePrefix ~= prefix or type(message) ~= "string" then
        return
    end

    local guid, chunkIndex, chunkCount, encoded = string.match(message, "^(%d+)|(%d+)|(%d+)|([%w+/=]+)$")
    if not guid then
        return
    end

    chunkIndex = tonumber(chunkIndex)
    chunkCount = tonumber(chunkCount)
    if not chunkIndex or not chunkCount or chunkCount < 1 or chunkCount > 8
        or chunkIndex < 1 or chunkIndex > chunkCount or #encoded > 200 or #encoded % 4 ~= 0 then
        return
    end

    local chunks = pendingChunks[guid]
    if not chunks or chunks.count ~= chunkCount then
        chunks = { count = chunkCount, parts = {}, received = 0 }
        pendingChunks[guid] = chunks
    end
    if not chunks.parts[chunkIndex] then
        chunks.parts[chunkIndex] = encoded
        chunks.received = chunks.received + 1
    end

    if chunks.received == chunks.count then
        local appearance = DecodeBase64(table.concat(chunks.parts))
        pendingChunks[guid] = nil
        if appearance and #appearance <= 1024 then
            if EsteriaRetailAppearanceReceive then
                EsteriaRetailAppearanceReceive(guid, appearance)
            else
                pendingAppearances[guid] = appearance
            end
        end
    end
end)
