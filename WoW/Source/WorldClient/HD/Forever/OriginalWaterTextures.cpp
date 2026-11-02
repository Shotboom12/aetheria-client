#include "WoW/Source/WorldClient/HD/Forever/OriginalWaterTextures.h"
#include "WoW/Source/WorldClient/HD/Forever/OriginalWaterBlp.h"
#include "WoW/Source/WorldClient/HD/Forever/Profiles.generated.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/GxSeam.h"
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

namespace HD::ForeverWater {
namespace {
struct Pixels { std::vector<uint8_t> bytes; OriginalBlp image; };
struct Entry { CTexture* texture = nullptr; };
std::map<uint32_t, Entry> entries;
size_t residentBytes = 0;
constexpr size_t kBudget = 128u * 1024u * 1024u;

void Fill(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t face,
          uint32_t level, void* user, uint32_t& stride, const void*& texels) {
    if (command != GxTex_Latch) return;
    texels = nullptr;
    stride = 0;
    const auto* pixels = static_cast<const Pixels*>(user);
    if (!pixels || level >= pixels->image.levelCount) return;
    const auto& mip = pixels->image.levels[level];
    if (width != mip.width || height != mip.height || face != 0) return;
    stride = mip.stride;
    texels = mip.bytes;
}
}

CGxTex* OriginalTexture(uint32_t fileDataId) {
    if (!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi() != GxApi_Vulkan) return nullptr;
    bool allowed = false;
    for (uint32_t id : kTextureIds) if (id == fileDataId) { allowed = true; break; }
    if (!allowed) return nullptr;
    const auto found = entries.find(fileDataId);
    if (found != entries.end()) return found->second.texture ? TextureGetGxTex(found->second.texture, 1, nullptr) : nullptr;
    // Cache a failed load for this session; don't repeatedly read disk on every water draw.
    auto& entry = entries[fileDataId];
    char path[96];
    std::snprintf(path, sizeof(path), "ForeverWater\\Textures\\%u.blp", fileDataId);
    FILE* file = std::fopen(path, "rb");
    if (!file) return nullptr;
    if (std::fseek(file, 0, SEEK_END)) { std::fclose(file); return nullptr; }
    const long size = std::ftell(file);
    if (size < 148 || size > 64 * 1024 * 1024 || size_t(size) > kBudget - residentBytes || std::fseek(file, 0, SEEK_SET)) {
        std::fclose(file); return nullptr;
    }
    auto pixels = std::make_shared<Pixels>();
    pixels->bytes.resize(static_cast<size_t>(size));
    const bool read = std::fread(pixels->bytes.data(), 1, pixels->bytes.size(), file) == pixels->bytes.size();
    std::fclose(file);
    if (!read || !ReadOriginalBlp(pixels->bytes.data(), pixels->bytes.size(), pixels->image)) return nullptr;
    const auto format = pixels->image.format == OriginalFormat::Dxt5 ? GxTex_Dxt5 : GxTex_Argb8888;
    const auto filter = pixels->image.levelCount > 1 ? GxTex_LinearMipLinear : GxTex_Linear;
    CGxTexFlags flags(filter, 1, 1, 0, 0, 0, 1, 0, 0, 0);
    char name[64];
    std::snprintf(name, sizeof(name), "hdForeverOriginal:%u", fileDataId);
    entry.texture = TextureCreate(pixels->image.width, pixels->image.height, format, format,
                                  flags, pixels.get(), Fill, name, 0);
    if (!entry.texture) return nullptr;
    // CTexture retains callback data even if another owner outlives this cache.
    entry.texture->m_esteriaPixels = pixels;
    residentBytes += pixels->bytes.size();
    return TextureGetGxTex(entry.texture, 1, nullptr);
}

void ReleaseOriginalTextures() {
    // Uses the engine's normal reference/deferred GPU texture destruction path.
    for (auto& pair : entries) if (pair.second.texture) HandleClose(reinterpret_cast<HOBJECT>(pair.second.texture));
    entries.clear();
    residentBytes = 0;
}
} // namespace HD::ForeverWater
