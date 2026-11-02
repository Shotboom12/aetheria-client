// Native Vulkan base material for the supplied Forever PBR forward adapter.
// Its Wrath normals/foam are stand-ins; this is not an exact Retail water port.
#include "WoW/Source/WorldClient/HD/HdForeverWater.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/HD/Forever/Profiles.generated.h"
#include "WoW/Source/WorldClient/HD/Forever/OriginalWaterTextures.h"
#include "WoW/Source/WorldClient/HD/HdNativeWaterCapture.h"
#include "WoW/Source/WorldClient/HD/HdNativeWaterService.h"
#include "WoW/Source/WorldClient/HD/Forever/HdForeverFieldOwner.h"
#include "WoW/Source/WorldClient/HD/HdWaterPlanarMath.h"
#include "WoW/Source/WorldClient/HD/HdGxShader.h"
#include "WoW/Source/WorldClient/HD/Cata/CataWater.h"
#include "WoW/Source/LiquidSystem/Liquid.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/World.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdPixelHigh.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Manifest.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMirrorPass.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Md5.hpp"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Texture.hpp"
#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Tempest/CAaSphere.h"
#include "Engine/Source/Os/TimeManager.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace HD::ForeverWater {
namespace {
CVar *enabled = nullptr, *info = nullptr, *debug = nullptr, *body = nullptr, *reflect = nullptr, *color = nullptr, *light = nullptr, *sunScale = nullptr;
CGxShader *vertex = nullptr, *pixel = nullptr, *pixelPlanar = nullptr;
CGxShader *pixelFields = nullptr, *pixelPlanarFields = nullptr;
bool attempted = false, planarAttempted = false;
bool fieldVariantsAttempted = false, fieldVariantsReady = false;
char previousInfo[256]{};
bool lakeBody = false; // Bind() is for a lake/river material: its slot-0 texture is not the depth ramp, so use the Forever scattering

void Info(const char* message) {
    if (!info) info = CVar::Register("hdForeverWaterInfo", "Forever native water status", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false); if (info) info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
    if (info && std::strcmp(previousInfo, message)) {
        std::snprintf(previousInfo, sizeof(previousInfo), "%s", message);
        info->Set(previousInfo, true, false, false, false);
    }
}

CVar* Control(const char* name, const char* help, uint32_t flags, const char* value) {
    CVar* cvar = CVar::LookupRegistered(name);
    return cvar ? cvar : CVar::Register(name, help, flags, value, nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

bool ReadCode(const char* name, std::vector<uint8_t>& code) {
    char path[128];
    std::snprintf(path, sizeof(path), "ForeverWater\\Bytecode\\%s.bin", name);
    FILE* file = std::fopen(path, "rb");
    if (!file) return false;
    if (std::fseek(file, 0, SEEK_END)) { std::fclose(file); return false; }
    const long size = std::ftell(file);
    if (size < 8 || size > 1024 * 1024 || std::fseek(file, 0, SEEK_SET)) { std::fclose(file); return false; }
    code.resize(static_cast<size_t>(size));
    const bool ok = std::fread(code.data(), 1, code.size(), file) == code.size();
    std::fclose(file);
    return ok;
}

CGxShader* Acquire(const char* name, EGxShTarget target) {
    std::vector<uint8_t> code;
    std::string error;
    const auto kind = target == GxSh_Vertex ? CataWater::ShaderKind::Vertex : CataWater::ShaderKind::Pixel;
    if (!ReadCode(name, code) || !CataWater::ValidateBytecode(code, kind, error) || !HD::ShaderKnown(code.data(), code.size())) return nullptr;
    if (target == GxSh_Pixel) {
        char digest[33];
        gxvk::md5::Hex(code.data(), code.size(), digest);
        // c13..c43 must be in this draw's private high-pixel alias, never the bone/VS shadow.
        if (!gxvk::vk::hdconstants::PixelHigh(gxvk::vk::manifest::ByMd5(digest))) return nullptr;
    }
    auto* device = g_theGxDevicePtr;
    auto* shader = device->m_shaderList[target].Ptr(name);
    if (shader) {
        if (shader->m_code.Count() != code.size() || std::memcmp(shader->m_code.m_data, code.data(), code.size()) || !shader->Valid()) return nullptr;
        ++shader->m_refCount;
        return shader;
    }
    shader = device->m_shaderList[target].New(name, 0, 0);
    if (!shader) return nullptr;
    shader->m_refCount = 1;
    shader->m_target = target;
    shader->unk_003C = shader->unk_0040 = shader->unk_0044 = shader->unk_0046 = 0;
    shader->m_code.SetCount(static_cast<uint32_t>(code.size()));
    std::memcpy(shader->m_code.m_data, code.data(), code.size());
    if (!shader->Valid()) { device->ShaderDestroy(shader); return nullptr; }
    return shader;
}

bool Shaders() {
    if (attempted) return vertex && pixel;
    attempted = true;
    vertex = Acquire("ForeverWaterVS", GxSh_Vertex);
    pixel = Acquire("ForeverWaterPS", GxSh_Pixel);
    if (vertex && pixel) return true;
    if (vertex) { g_theGxDevicePtr->ShaderDestroy(vertex); vertex = nullptr; }
    if (pixel) { g_theGxDevicePtr->ShaderDestroy(pixel); pixel = nullptr; }
    Info("stock water: matching Forever bytecode/SPIR-V/high-pixel registration unavailable");
    return false;
}

CGxShader* PlanarPixel() {
    if (!planarAttempted) {
        planarAttempted = true;
        pixelPlanar = Acquire("ForeverWaterPSPlanar", GxSh_Pixel);
    }
    return pixelPlanar;
}

bool FieldVariants() {
    if (!fieldVariantsAttempted) {
        // Optional sidecar contains exactly two shader registry names: field-capable
        // pixel shader, then its planar equivalent. Each binary still passes Acquire's
        // bytecode, translated-manifest and high-pixel-alias checks.
        FILE* file = std::fopen("ForeverWater\\Bytecode\\field-variants.txt", "rb");
        if (file) {
            char names[2][96]{};
            bool valid = true;
            for (auto& name : names) {
                if (!std::fgets(name, sizeof name, file)) { valid = false; break; }
                const size_t n = std::strcspn(name, "\r\n"); name[n] = 0;
                if (!n || n >= sizeof name - 1) { valid = false; break; }
                for (size_t i = 0; i < n; ++i)
                    if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
                          (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-')) valid = false;
            }
            char extra[2]; if (std::fgets(extra, sizeof extra, file)) valid = false;
            std::fclose(file);
            if (valid) {
                pixelFields = Acquire(names[0], GxSh_Pixel);
                pixelPlanarFields = Acquire(names[1], GxSh_Pixel);
                fieldVariantsReady = pixelFields && pixelPlanarFields;
                if (!fieldVariantsReady) {
                    if (pixelFields) g_theGxDevicePtr->ShaderDestroy(pixelFields);
                    if (pixelPlanarFields) g_theGxDevicePtr->ShaderDestroy(pixelPlanarFields);
                    pixelFields = pixelPlanarFields = nullptr;
                } else fieldVariantsAttempted = true;
            }
        }
    }
    HD::ForeverNative::FieldOwner::SetMatchingVariantReady(fieldVariantsReady);
    return fieldVariantsReady;
}

const Profile* Select(Liquid::CMaterialSettings* settings, bool& interior) {
    // Resolve by actual bank identity, not a guessed ramp filename or stale pointer cache.
    auto& bank = Liquid::CSettingsBank::s_settings;
    for (uint32_t id = 0; id < bank.Count(); ++id) if (bank.m_data[id] == settings) return Resolve(id, interior);
    return nullptr;
}

float Clamp(float value, float low, float high) { return value < low ? low : value > high ? high : value; }
float Linear(uint8_t value) { return std::pow(value / 255.0f, 2.2f); }
void Color(const CImVector& value, float* out) { out[0] = Linear(value.r); out[1] = Linear(value.g); out[2] = Linear(value.b); }

bool Constants(const Profile& profile, bool interior, const CataWater::SceneCapture::Snapshot& capture, float* c) {
    const auto& mv = Liquid::s_vsConstants.m_modelView;
    c[0] = mv.a0; c[1] = mv.a1; c[2] = mv.a2;
    c[4] = mv.b0; c[5] = mv.b1; c[6] = mv.b2;
    c[8] = mv.c0; c[9] = mv.c1; c[10] = mv.c2;
    const auto& sun = Liquid::s_vsConstants.m_sunDir;
    const float length = std::sqrt(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
    const float divisor = length > 1e-6f ? length : 1.0f;
    c[12] = -sun.x / divisor; c[13] = -sun.y / divisor; c[14] = -sun.z / divisor;
    c[16] = 1.0f / capture.extent.width; c[17] = 1.0f / capture.extent.height;
    c[18] = interior ? 1.0f : 0.0f; c[19] = 1.0f;
    const auto& fog = Liquid::s_vsConstants.m_fog;
    c[20] = fog.x; c[21] = fog.y; c[22] = fog.z;
    c[23] = float(OsGetAsyncTimeMs() % 2000000) * 0.001f;
    std::memcpy(c + 24, profile.rows, sizeof(profile.rows));
    c[51] *= sunScale ? Clamp(sunScale->m_floatValue, 0.0f, 4.0f) : 1.0f; // cType[6].w: sun-glint scale
    const auto& bands = DayNight::g_dnInfo.m_bands;
    Color(bands.m_skyColors[0], c + 52); Color(bands.m_skyColors[1], c + 56);
    Color(bands.m_skyColors[3], c + 60); Color(bands.m_skyColors[4], c + 64);
    for (uint32_t k = 0; k < 3; ++k) c[68 + k] = (c[52 + k] + 2.0f * c[56 + k] + c[60 + k] + c[64 + k]) * 0.2f;
    c[55] = debug ? Clamp(float(debug->m_intValue), 0.0f, 7.0f) : 0.0f;
    c[59] = body ? Clamp(body->m_floatValue, 0.0f, 5.0f) : 1.0f;
    c[63] = reflect ? Clamp(reflect->m_floatValue, 0.0f, 1.0f) : 1.0f;
    c[75] = lakeBody ? 0.0f : (color ? Clamp(color->m_floatValue, 0.0f, 1.0f) : 1.0f);
    const auto& amb = Liquid::s_psProcWaterConstants.m_sunAmbient;
    const auto& dif = Liquid::s_psProcWaterConstants.m_sunDiffuse;
    for (uint32_t k = 0; k < 3; ++k) {
        c[72 + k] = std::pow(Clamp((&amb.x)[k] + (&dif.x)[k], 0.0f, 4.0f), 2.2f);
        c[80 + k] = std::pow(Clamp((&amb.x)[k], 0.0f, 4.0f), 2.2f);
        c[84 + k] = std::pow(Clamp((&dif.x)[k], 0.0f, 4.0f), 2.2f);
    }
    const float lightScale = light ? Clamp(light->m_floatValue, 0.0f, 4.0f) : 0.35f;
    for (uint32_t k = 0; k < 3; ++k) { c[72 + k] *= lightScale; c[80 + k] *= lightScale; c[84 + k] *= lightScale; }
    const auto& projection = g_theGxDevicePtr->m_projNative;
    // The depth copy belongs to this projection/viewport, never another view.
    static_assert(sizeof(projection) == sizeof(capture.scene.projection), "native captured matrix layout");
    if (std::memcmp(capture.scene.projection, &projection, sizeof(projection))) return false;
    C44Matrix view;
    GxXformView(view);
    if (std::memcmp(capture.scene.view, &view, sizeof(view))) return false;
    const auto& viewport = capture.viewport;
    if (!(viewport.width > 0.0f) || !(viewport.height > 0.0f)) return false;
    c[76] = 0.5f * projection.a0 * viewport.width / capture.extent.width;
    c[77] = -0.5f * projection.b1 * viewport.height / capture.extent.height;
    c[78] = (viewport.x + 0.5f * viewport.width * (1.0f + projection.c0)) / capture.extent.width;
    c[79] = (viewport.y + 0.5f * viewport.height * (1.0f - projection.c1)) / capture.extent.height;
    c[124] = projection.c2; c[125] = projection.d2;
    c[126] = capture.viewport.minDepth; c[127] = capture.viewport.maxDepth;
    if (c[127] - c[126] < 1e-4f || std::fabs(c[125]) < 1e-7f) return false;
    for (uint32_t i = 0; i < 128; ++i) if (!std::isfinite(c[i])) return false;
    return true;
}
}

void Trace(const char* format, ...) {
    static int lines = 0;
    if (lines >= 400) return;
    FILE* log = std::fopen("Logs\\HdForeverWater.log", "a");
    if (!log) return;
    ++lines;
    va_list args;
    va_start(args, format);
    std::vfprintf(log, format, args);
    va_end(args);
    std::fputc('\n', log);
    std::fclose(log);
}

 void Status(const char* message) { Info(message); }

bool Requested() {
    if (Visuals::Current() != Visuals::AzerothRemastered) {
        static int notAr = 0;
        if (notAr++ < 3) Trace("Requested: renderer is %d, not Azeroth Remastered", static_cast<int>(Visuals::Current()));
        return false;
    }
    if (!enabled) {
        enabled = CVar::LookupRegistered("hdWater");
        if (!enabled) enabled = CVar::Register("hdWater", "Forever PBR water native adapter (0 stock, 1 enabled)", 1, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    }
    if (!debug) debug = Control("hdWaterDebug", "Water debug view (0 off, 1 depth, 2 scattering, 3 reflections, 4 foam, 5 roughness, 6 scene depth, 7 background)", 0, "0");
    if (!body) body = Control("hdWaterBody", "Water body-colour strength (1 matches Forever)", 1, "1");
    if (!reflect) reflect = Control("hdWaterReflect", "Screen-space water reflections", 1, "1");
    if (!light) light = Control("hdWaterLight", "Forever water light scale: exposure for the sun and ambient colours that feed its scattering (1 = raw game values, which are too bright here)", 1, "0.35");
    if (!sunScale) sunScale = Control("hdWaterSun", "Scale of the sun glint on the water (1 = Forever strength)", 1, "0.5");
    if (!CVar::LookupRegistered("hdWaterWaveAmp")) Control("hdWaterWaveAmp", "Wave amplitude of the Forever wave fields (1 = pinned Forever data, 0 = flat)", 1, "0.3");
    if (!CVar::LookupRegistered("hdWaterWaveSpeed")) Control("hdWaterWaveSpeed", "Wave animation speed of the Forever wave fields (1 = real time)", 1, "0.4");
    if (!color) color = Control("hdWaterColor", "Blend the game's water colour into the body (1 game colour, 0 Forever scattering)", 1, "1");
    const bool on = enabled && enabled->m_intValue == 1;
    static int asked = 0;
    if (asked++ < 4) Trace("Requested: hdWater=%d -> %d", enabled ? static_cast<int>(enabled->m_intValue) : -1, on ? 1 : 0);
    return on;
}

bool Bind(Liquid::CMaterialSettings* settings, const CAaSphere* bounds, bool lake) {
    lakeBody = lake;
    static int calls = 0;
    if (calls++ < 6) Trace("Bind called (ready=%d, settings=%d)", static_cast<int>(g_gxCataWater.m_ready), settings ? 1 : 0);
    if (!Requested()) { Info("stock water: hdWater is not 1, or the renderer is not Azeroth Remastered"); return false; }
    if (!HD::VulkanDevice()) { Info("stock water: no Vulkan device"); return false; }
    if (!settings) return false;
    if (!g_gxCataWater.m_ready) { Info("stock water: the scene colour/depth capture is not ready (capture services off or not sampled yet)"); return false; }
    if (World::SceneCamLiquidStatus(nullptr)) { Info("stock water: camera is under water"); return false; }
    if (gxvk::vk::frame::Target() || gxvk::vk::frame::QueryActive()) return false;
    const auto* scene = gxvk::vk::frame::Scene();
    if (!scene || !scene->extent.width || !scene->extent.height) { Info("stock water: no scene target in this pass"); return false; }
    const auto* capture = NativeWaterCapture::Get(gxvk::vk::frame::Serial(), scene->extent);
    if (!capture) { Info("stock water: no capture for this frame (NativeWaterCapture::Get returned nothing)"); return false; }
    bool interior = false;
    const auto* profile = Select(settings, interior);
    if (!profile) { Info("stock water: no explicit Forever mapping for this liquid"); return false; }
    const float selectedPlane = g_gxCataPlane.m_height;
    // Each water body is keyed by its own surface height (to 4 yd, so a sloped river shares a few fields), not only by the one mirror plane, so lakes and rivers
    // at other heights get Forever water too (planar mirror still only for the selected plane).
    const float waterPlane = (bounds && std::isfinite(bounds->c.z)) ? std::round(bounds->c.z * 0.25f) * 4.0f : selectedPlane;
    // An ocean far below the camera is only visible through gaps in the terrain, where the scene depth behind it is meaningless: keep stock water there.
    static CVar* oceanHeight = nullptr; if (!oceanHeight) oceanHeight = Control("hdWaterOceanHeight", "Forever water is not used on a water body (lake or ocean) when the camera is more than this many yards above its surface (0 = always)", 1, "80");
    if (oceanHeight && oceanHeight->m_floatValue > 0.0f && std::isfinite(waterPlane) && CWorldScene::s_activeWorldView.z - waterPlane > oceanHeight->m_floatValue) { Info("stock water: the camera is far above this water (hdWaterOceanHeight)"); return false; }
    // Role assignments are explicit FileDataID mappings: reference order varies between profiles.
    // Do not silently substitute Wrath's animated lake alpha or another profile's assets.
    uint32_t depthFoamId = 0, rimFoamId = 0;
    if (!FoamTexturesFor(profile->id, depthFoamId, rimFoamId)) {
        Info("stock water: no explicit Forever depth/rim foam mapping for this profile");
        return false;
    }
    CGxTex* depthFoam = depthFoamId ? OriginalTexture(depthFoamId) : nullptr;
    CGxTex* rimFoam = rimFoamId ? OriginalTexture(rimFoamId) : nullptr;
    if (!depthFoam || !rimFoam) { Info("stock water: the mapped Forever depth/rim foam assets are unavailable"); return false; }
    int32_t fieldPlaneQuarterYard = static_cast<int32_t>(std::lround(waterPlane * 4.0f));
    const bool fieldVariantReady = FieldVariants();
    if (fieldVariantReady && (!bounds || !std::isfinite(waterPlane) ||
        !HD::ForeverNative::FieldOwner::Queue(World::QueryMapId(), profile->id, waterPlane, gxvk::vk::frame::Serial(), &fieldPlaneQuarterYard))) {
        static int planeTrace = 0;
        if (planeTrace++ < 12) Trace("plane check failed: bounds=%d valid=%d plane=%.2f centreZ=%.2f queueFull-or-other=%d profile=%u", bounds ? 1 : 0, static_cast<int>(g_gxCataPlane.m_valid), selectedPlane, bounds ? bounds->c.z : 0.0f, (bounds && g_gxCataPlane.m_valid && std::isfinite(selectedPlane) && std::fabs(bounds->c.z - selectedPlane) < 0.25f) ? 1 : 0, static_cast<unsigned>(profile->id));
        Info("stock water: no water height for this body, or the four-key field queue is full");
        return false;
    }
    // Resolve proxies before binding any replacement states. Their image resolver is frame/serial checked.
    const bool existed = g_gxCataWater.m_proxy[0] && g_gxCataWater.m_proxy[2];
    if (!NativeWaterService::PrepareMaterialProxies() || !existed || !Shaders()) return false;
    float constants[128]{};
    if (!Constants(*profile, interior, *capture, constants)) { Info("stock water: capture projection/viewport mismatch"); return false; }
    {
        static int traceCount[2]{}; static uint64_t lastSerial[2]{}; const int which = lakeBody ? 1 : 0; const uint64_t cur = gxvk::vk::frame::Serial();
        if (cur != lastSerial[which] && traceCount[which] < 150) { lastSerial[which] = cur; ++traceCount[which];
            Trace("%s consts serial=%llu sun=%.2f,%.2f,%.2f mvDiag=%.2f,%.2f,%.2f light=%.3f,%.3f,%.3f amb=%.3f dif=%.3f fog=%.3f,%.3f,%.3f t=%.2f", lakeBody ? "lake" : "ocean", static_cast<unsigned long long>(cur), constants[12], constants[13], constants[14], constants[0], constants[5], constants[10], constants[72], constants[73], constants[74], constants[80], constants[84], constants[20], constants[21], constants[22], constants[23]);
        }
    }

    // Reuse the renderer's same-frame Vulkan mirror only for the flat water plane it was rendered for.
    // If any plane/image/extent check fails, the normal SSR shader below remains the safe fallback.
    gxvk::vk::hdmirror::Snapshot mirrorSnapshot{};
    const bool mirrorCurrent = !interior && bounds && g_gxCataPlane.m_valid && g_gxCataWater.m_mirror &&
        gxvk::vk::hdmirror::GetSnapshot(gxvk::vk::frame::Serial(), mirrorSnapshot) && mirrorSnapshot.image &&
        mirrorSnapshot.sourceExtent.width && mirrorSnapshot.sourceExtent.height;
    const auto* mirror = mirrorCurrent ? mirrorSnapshot.image : nullptr;
    const float waterHeight = bounds ? bounds->c.z : 0.0f;
    C44Matrix mainView;
    GxXformView(mainView);
    const auto& mainProjection = g_theGxDevicePtr->m_projNative;
    const bool sameProducerView = mirrorCurrent &&
        std::memcmp(mirrorSnapshot.view, &mainView, sizeof(mainView)) == 0 &&
        std::memcmp(mirrorSnapshot.projection, &mainProjection, sizeof(mainProjection)) == 0;
    const bool sameProducerPlane = mirrorCurrent && std::isfinite(mirrorSnapshot.height) &&
        std::fabs(mirrorSnapshot.height - g_gxCataPlane.m_height) < 0.25f;
    const bool usePlanar = mirror && sameProducerView && sameProducerPlane &&
        mirror->extent.width && mirror->extent.height &&
        std::isfinite(waterHeight) && std::isfinite(g_gxCataPlane.m_height) &&
        std::fabs(waterHeight - g_gxCataPlane.m_height) < 0.25f && PlanarPixel();
    if (usePlanar) {
        C44Matrix mainToMirror = HD::PlanarMath::MainToMirror(
            mainView, mainProjection, CWorldScene::s_activeWorldView, mirrorSnapshot.height);
        static_assert(sizeof(mainToMirror) == 16 * sizeof(float), "Forever planar matrix ABI");
        constants[104] = 1.0f / mirror->extent.width;
        constants[105] = 1.0f / mirror->extent.height;
        std::memcpy(constants + 108, &mainToMirror, sizeof(mainToMirror)); // c39..c42
    }
    CGxShader* selectedPixel = usePlanar ? pixelPlanar : pixel;
    CGxTex* fieldTextures[5]{};
    HD::ForeverNative::FieldOwner::Metadata fieldMetadata{};
    const auto fieldKey = HD::ForeverNative::FieldOwner::Key{
        World::QueryMapId(), static_cast<int32_t>(profile->id),
        fieldPlaneQuarterYard};
    const bool useFields = fieldVariantReady && HD::ForeverNative::FieldOwner::Binding(
        fieldKey, gxvk::vk::frame::Serial(), fieldTextures, fieldMetadata);
    {
        static int bindTrace = 0;
        static uint64_t lastBindSerial = 0; const bool newBindSerial = gxvk::vk::frame::Serial() != lastBindSerial; if (newBindSerial) lastBindSerial = gxvk::vk::frame::Serial();
        if (bindTrace < 160 && newBindSerial && (fieldVariantReady || bindTrace < 10)) { ++bindTrace; Trace("bind serial=%llu plane=%.1f profile=%u useFields=%d planar=%d", static_cast<unsigned long long>(gxvk::vk::frame::Serial()), waterPlane, static_cast<unsigned>(profile->id), useFields ? 1 : 0, usePlanar ? 1 : 0); }
    }
    {
        static int sampleCount = 0; const uint64_t sampleSerial = gxvk::vk::frame::Serial();
        if ((sampleSerial % 60) == 0 && sampleCount < 300) { ++sampleCount; const auto& eye = CWorldScene::s_activeWorldView;
            Trace("sample serial=%llu lake=%d profile=%u plane=%.1f r=%.0f c=%.0f,%.0f,%.0f cam=%.0f,%.0f,%.0f fields=%d", static_cast<unsigned long long>(sampleSerial), lakeBody ? 1 : 0, static_cast<unsigned>(profile->id), waterPlane, bounds ? bounds->r : -1.0f, bounds ? bounds->c.x : 0.0f, bounds ? bounds->c.y : 0.0f, bounds ? bounds->c.z : 0.0f, eye.x, eye.y, eye.z, useFields ? 1 : 0); }
    }
    if (fieldVariantReady && !useFields) {
        Info("stock water: matching field shader is ready but the atomic current-frame field is unavailable");
        return false;
    }
    if (useFields) {
        // PixelConstantsSet starts at c12, so these offsets address c34-c37.
        std::copy(fieldMetadata.inverseTile.begin(), fieldMetadata.inverseTile.end(), constants + 4 * (34 - 12));
        // No verified profile whitecap controls exist yet; leave c35 at zero.
        std::copy(fieldMetadata.rippleUv.begin(), fieldMetadata.rippleUv.end(), constants + 4 * (36 - 12));
        constants[4 * (37 - 12)] = fieldMetadata.rippleStrength;
    }
    HD::PixelConstantsSet(12, constants, 32);
    g_theGxDevicePtr->RsSet(GxRs_Texture6, static_cast<CGxTex*>(g_gxCataWater.m_proxy[2]));
    g_theGxDevicePtr->RsSet(GxRs_Texture7, static_cast<CGxTex*>(g_gxCataWater.m_proxy[0]));
    g_theGxDevicePtr->RsSet(GxRs_Texture14, depthFoam);
    g_theGxDevicePtr->RsSet(GxRs_Texture15, rimFoam);
    if (usePlanar) g_theGxDevicePtr->RsSet(GxRs_Texture13, static_cast<CGxTex*>(g_gxCataWater.m_proxy[1]));
    if (useFields) {
        g_theGxDevicePtr->RsSet(GxRs_Texture8, fieldTextures[0]);
        g_theGxDevicePtr->RsSet(GxRs_Texture9, fieldTextures[1]);
        g_theGxDevicePtr->RsSet(GxRs_Texture10, fieldTextures[2]);
        g_theGxDevicePtr->RsSet(GxRs_Texture11, fieldTextures[3]);
        g_theGxDevicePtr->RsSet(GxRs_Texture12, fieldTextures[4]);
        selectedPixel = usePlanar ? pixelPlanarFields : pixelFields;
    } else {
        g_theGxDevicePtr->RsSet(GxRs_Texture8, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture9, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture10, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture11, nullptr);
        g_theGxDevicePtr->RsSet(GxRs_Texture12, nullptr);
    }
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, vertex);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, selectedPixel);
    g_theGxDevicePtr->RsSet(GxRs_ColorWrite, 7);
    g_theGxDevicePtr->RsSet(GxRs_DepthWrite, 1);
    Info(usePlanar ? "Forever PBR with same-frame Vulkan planar mirror; FFT/ripples pending" :
                     "Forever PBR with screen-space reflection fallback; FFT/ripples/planar shader pending");
    return true;
}

void Shutdown() {
    HD::ForeverNative::FieldOwner::Shutdown(gxvk::vk::frame::Completed());
    if (g_theGxDevicePtr) {
        if (vertex) g_theGxDevicePtr->ShaderDestroy(vertex);
        if (pixel) g_theGxDevicePtr->ShaderDestroy(pixel);
        if (pixelPlanar) g_theGxDevicePtr->ShaderDestroy(pixelPlanar);
        if (pixelFields) g_theGxDevicePtr->ShaderDestroy(pixelFields);
        if (pixelPlanarFields) g_theGxDevicePtr->ShaderDestroy(pixelPlanarFields);
    }
    vertex = pixel = pixelPlanar = pixelFields = pixelPlanarFields = nullptr;
    attempted = planarAttempted = false;
    fieldVariantsAttempted = fieldVariantsReady = false;
    ReleaseOriginalTextures();
    Info("off");
}
} // namespace HD::ForeverWater
