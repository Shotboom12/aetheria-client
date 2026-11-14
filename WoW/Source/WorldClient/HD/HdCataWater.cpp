// HD: Cataclysm's water material under Vulkan (HdCataWater.h). The device side (the scene copy, the reflection and
// the bridge that hands them to the proxy textures) is Engine/Source/Gx/CGxDeviceVulkan/hd/CataWaterService.cpp.
#include "WoW/Source/WorldClient/HD/HdCataWater.h"
#include "WoW/Source/WorldClient/HD/HdNativeWaterService.h"
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/CGxBuf.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"
#include "Engine/Source/Tempest/C33Matrix.h"
#include "WoW/Source/LiquidSystem/Liquid.h"
#include "WoW/Source/WorldClient/HD/Cata/CataWater.h"
#include "WoW/Source/WorldClient/HD/Cata/CataWaterGx.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdCataMirror.h"
#include "WoW/Source/WorldClient/HD/HdCataRipples.h"
#include "WoW/Source/WorldClient/HD/HdGxShader.h"
#include "WoW/Source/WorldClient/CPortalView.h"
#include "WoW/Source/WorldClient/World.h"

namespace {
    struct MaterialConstantsScope {
        CGxDevice* device;
        float vertex[53*4],pixel[32*4];
        explicit MaterialConstantsScope(CGxDevice* d):device(d) {
            std::memcpy(vertex,d->ShaderConstantsLock(GxSh_Vertex),sizeof vertex);
            std::memcpy(pixel,d->ShaderConstantsLock(GxSh_Pixel),sizeof pixel);
        }
        ~MaterialConstantsScope(){
            device->ShaderConstantsSet(GxSh_Vertex,0,vertex,53);
            device->ShaderConstantsSet(GxSh_Pixel,0,pixel,32);
        }
    };

    namespace cw = HD::CataWater;

    // Cata CMaterialWater at Liquid::GetWaterDetail() 2, the camera above the water, no dynamic displacement map
    // yet (step D): depth mode 3 (DepthCache::DM_Texture, the copy is raw R32F depth), hardware depth.
    constexpr uint32_t kDetail = 2;
    constexpr uint32_t kDepthMode = 3;
    constexpr uint32_t kDisplacements = 0;
    constexpr uint32_t kRippleMaps = 2;       // HD: tranche 2, Cata's dynamic displacement maps (CataRipples.cpp)
    constexpr float kPlaneMatch = 0.25f;   // yards: an instance this close to the mirror's plane takes its reflection
    // HD L19 (2026-11-03): a chunk batch's sphere centre is the middle of ALL its water (a lake and the stream that
    // leaves it), not the lake's height, so the batch next to the mirrored one could fall back to the sky reflection:
    // a straight seam on the lake at the batch border. With hdCataWaterPlaneRange 1 a chunk batch whose water reaches
    // the plane (its lowest..highest within kPlaneMatch) takes the reflection too, like Cata where every reflecting
    // water uses the closest plane. Batches taller than kRangeMax (falls, rapids) keep the old test.
    constexpr float kRangeMax = 3.0f;
    CVar* s_planeRange = nullptr;
    CVar* s_waterLog = nullptr;
    // HD L19: read-only journal Logs\\hd-eau.txt (hdCataWaterLog 1): the water drawn near the camera and why it took
    // the reflection or not. It changes nothing on screen.
    struct LogEntry { float x, y, cz, low, high; int haveRange, mesh, oldMirror, mirrored, sky; uint32_t type; };
    std::vector<LogEntry> s_logEntries;
    float s_logCamera[3] = {};
    uint32_t s_logLast = 0;
    int s_logLines = 0;
    constexpr uint32_t kVertexRows = 53;
    constexpr uint32_t kPixelRows = 32;
    constexpr uint32_t kPixelLowRows = 13;    // the translated pixel modules read c0..c12 from the pixel slice

    enum class Load { NotTried, Ready, Failed };
    Load s_load = Load::NotTried;
    cw::LiquidTypes s_types;
    CGxShader* s_vertex = nullptr;
    CGxShader* s_pixelReal = nullptr;   // ReflectionKind::Real: s1 is the mirror
    CGxShader* s_pixelFake = nullptr;   // ReflectionKind::Fake: no mirror and no sky for this water (s3 only)
    CGxShader* s_pixelNone = nullptr;   // ReflectionKind::None: Cata's water without a mirror, s1 the sky's reflection
    // HD: tranche 2, the same with the two displacement maps (s4, s8): used while the device has them this frame.
    CGxShader* s_rippleVertex = nullptr;
    CGxShader* s_ripplePixelReal = nullptr;
    CGxShader* s_ripplePixelFake = nullptr;
    CGxShader* s_ripplePixelNone = nullptr;
    bool s_ripplesLoaded = false;
    // HD: tranche 4, Cata CMaterialWater::SetPixelShaderBelow: the water seen from under its surface (the camera in a
    // liquid). Without and with the displacement maps; Below's reflection permutations share one module.
    CGxShader* s_pixelBelow = nullptr;
    CGxShader* s_ripplePixelBelow = nullptr;
    CTexture* s_proxy[7] = {};
    bool s_submerged = false;
    CVar* s_info = nullptr;
    char s_lastInfo[256] = "";

    // Why an instance keeps the 3.3.5 material: counted in g_gxCataWater.m_kept, which the device logs (Vulkan.log).
    enum Kept { Kept_Submerged, Kept_Files, Kept_Proxies, Kept_NoCopy, Kept_Type, Kept_Wmo, Kept_Textures, Kept_Constants,
                Kept_Buffers, Kept_Count };
    static_assert(Kept_Count <= sizeof(g_gxCataWater.m_kept) / sizeof(g_gxCataWater.m_kept[0]), "GxCataWater::m_kept");
    bool KeepOld(Kept reason) {
        g_gxCataWater.m_kept[reason]++;
        return false;
    }

    // A BLP2 of the Cata runtime, kept in memory: its texture's fill callback hands out its levels.
    struct Blp {
        std::vector<uint8_t> bytes;
        uint32_t width = 0, height = 0, levels = 0, blockBytes = 0;   // blockBytes 0: 32-bit BGRA rows
        uint32_t offset[16] = {}, size[16] = {};
        CTexture* texture = nullptr;
    };
    std::map<std::string, std::unique_ptr<Blp>> s_blps;   // by lower-case name; a failed load stays as nullptr

    void SetInfo(const char* format, ...) {
        char text[256];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        snprintf(g_gxCataWater.m_info, sizeof(g_gxCataWater.m_info), "%s", text);
        if (!s_info || !strcmp(s_lastInfo, text)) return;
        snprintf(s_lastInfo, sizeof(s_lastInfo), "%s", text);
        s_info->Set(text, true, false, false, false);
    }

    FILE* OpenRuntime(const char* relative) {
        char path[512];
        snprintf(path, sizeof(path), "CataWater\\%s", relative);
        FILE* file = fopen(path, "rb");
        if (file) return file;
        snprintf(path, sizeof(path), "..\\..\\CataWater\\%s", relative);
        return fopen(path, "rb");
    }

    bool ReadRuntime(const char* relative, std::vector<uint8_t>& out) {
        FILE* file = OpenRuntime(relative);
        if (!file) return false;
        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        fseek(file, 0, SEEK_SET);
        bool ok = size > 0 && size < 64 * 1024 * 1024;
        if (ok) {
            out.resize(static_cast<size_t>(size));
            ok = fread(out.data(), 1, out.size(), file) == out.size();
        }
        fclose(file);
        return ok;
    }

    uint32_t Word(const uint8_t* p) {
        return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    }

    void BlpFill(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg,
                 uint32_t& texelStrideInBytes, const void*& texels) {
        if (command != GxTex_Latch) return;
        const Blp* blp = static_cast<const Blp*>(userArg);
        texels = nullptr;
        if (!blp || mipLevel >= blp->levels || !blp->size[mipLevel]) return;
        texels = blp->bytes.data() + blp->offset[mipLevel];
        texelStrideInBytes = blp->blockBytes ? ((width + 3) / 4 > 0 ? (width + 3) / 4 : 1) * blp->blockBytes : width * 4;
    }

    // XTextures\foam\waterchop7bw.blp: DXT (compression 2) or 32-bit BGRA (compression 3), with its own mip chain.
    CGxTex* BlpTexture(const std::string& name) {
        if (name.empty()) return nullptr;
        std::string key = name;
        for (char& c : key) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
        auto found = s_blps.find(key);
        if (found != s_blps.end()) return found->second ? TextureGetGxTex(found->second->texture, 1, nullptr) : nullptr;

        std::unique_ptr<Blp> blp(new Blp());
        std::string relative = "Textures\\" + name;
        std::vector<uint8_t>& d = blp->bytes;
        bool ok = ReadRuntime(relative.c_str(), d) && d.size() >= 148 && !memcmp(d.data(), "BLP2", 4) && Word(d.data() + 4) == 1;
        EGxTexFormat format = GxTex_Unknown;
        if (ok) {
            const uint8_t compression = d[8], alphaEncoding = d[10];
            blp->width = Word(d.data() + 12);
            blp->height = Word(d.data() + 16);
            if (compression == 3) {
                format = GxTex_Argb8888;
            } else if (compression == 2) {
                format = alphaEncoding == 7 ? GxTex_Dxt5 : alphaEncoding == 1 ? GxTex_Dxt3 : GxTex_Dxt1;
                blp->blockBytes = format == GxTex_Dxt1 ? 8 : 16;
            }
            ok = format != GxTex_Unknown && blp->width && blp->height && blp->width <= 4096 && blp->height <= 4096;
        }
        if (ok) {
            for (uint32_t i = 0; i < 16; i++) {
                blp->offset[i] = Word(d.data() + 20 + 4 * i);
                blp->size[i] = Word(d.data() + 84 + 4 * i);
                if (!blp->size[i]) break;
                if (uint64_t(blp->offset[i]) + blp->size[i] > d.size()) { ok = false; break; }
                blp->levels = i + 1;
            }
            ok = ok && blp->levels > 0;
        }
        if (!ok) {
            s_blps.emplace(key, nullptr);
            SetInfo("ERROR: CataWater\\%s missing or not a BLP2 this reads (DXT or BGRA)", relative.c_str());
            return nullptr;
        }
        CGxTexFlags flags(GxTex_LinearMipLinear, 1, 1, 0, 0, 0, 1, 0, 0, 0);
        char texName[160];
        snprintf(texName, sizeof(texName), "hdCata:%s", name.c_str());
        blp->texture = TextureCreate(blp->width, blp->height, format, format, flags, blp.get(), BlpFill, texName, 0);
        Blp* raw = blp.get();
        s_blps.emplace(key, std::move(blp));
        return raw->texture ? TextureGetGxTex(raw->texture, 1, nullptr) : nullptr;
    }

    void ProxyFill(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg,
                   uint32_t& texelStrideInBytes, const void*& texels) {
        static uint32_t s_white[8 * 8];
        if (command != GxTex_Latch) return;
        if (!s_white[0]) for (uint32_t& texel : s_white) texel = 0xFFFFFFFFu;
        texelStrideInBytes = 8 * 4;
        texels = s_white;
    }

    // The three textures the device's bridge redirects (GxSeam.h GxCataWater). True once all three exist. 8x8: GxTexCreate
    // refuses anything under 8 texels a side.
    bool Proxies() {
        static const char* const kNames[7] = {"hdCataSceneColor", "hdCataReflection", "hdCataSceneDepth", "hdCataSkyReflection",
                                              "hdCataBlurredScene",                     // HD: tranche 1, the blur
                                              "hdCataRipples0", "hdCataRipples1"};      // HD: tranche 2, the ripple maps
        bool all = true;
        for (uint32_t i = 0; i < 7; i++) {
            if (!s_proxy[i]) {
                CGxTexFlags flags(i == 2 ? GxTex_Nearest : GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
                s_proxy[i] = TextureCreate(8, 8, GxTex_Argb8888, GxTex_Argb8888, flags, nullptr, ProxyFill, kNames[i], 0);
            }
            CGxTex* tex = s_proxy[i] ? TextureGetGxTex(s_proxy[i], 1, nullptr) : nullptr;
            if (i < 3) g_gxCataWater.m_proxy[i] = tex;
            else if (i == 3) g_gxCataWater.m_proxySky = tex;
            else if (i == 4) g_gxCataWater.m_proxyBlur = tex;
            else g_gxCataRipples.m_proxy[i - 5] = tex;   // HD: Cata's displacement maps: linear, clamped
            if (!tex) all = false;
        }
        return all;
    }

    bool LoadShader(cw::ShaderKind kind, const char* family, uint32_t permutation, const std::map<std::string, std::string>& index,
                    CGxShader*& out) {
        char key[96];
        snprintf(key, sizeof(key), "%s %s %u", kind == cw::ShaderKind::Vertex ? "vertex" : "pixel", family, permutation);
        auto entry = index.find(key);
        std::vector<uint8_t> code;
        if (entry == index.end() || !ReadRuntime(entry->second.c_str(), code)) {
            SetInfo("ERROR: %s:%u not in CataWater\\shaders.txt or its file is missing", family, permutation);
            return false;
        }
        std::string error;
        out = cw::Gx::Acquire(kind, family, permutation, code, error);
        if (!out) {
            SetInfo("ERROR: %s:%u: %s (are the Cata modules in the Vulkan manifest?)", family, permutation, error.c_str());
            return false;
        }
        return true;
    }

    bool LoadOnce() {
        if (s_load != Load::NotTried) return s_load == Load::Ready;
        s_load = Load::Failed;

        std::vector<uint8_t> dbc;
        std::string error;
        if (!ReadRuntime("DBFilesClient\\LiquidType.dbc", dbc) || !s_types.Read(dbc, error)) {
            SetInfo("ERROR: CataWater\\DBFilesClient\\LiquidType.dbc: %s", error.empty() ? "missing" : error.c_str());
            return false;
        }

        // shaders.txt: "pixel CataProcWaterAbove 39 bytecode\pixel\<md5>.bin"
        std::map<std::string, std::string> index;
        std::vector<uint8_t> text;
        if (!ReadRuntime("shaders.txt", text)) {
            SetInfo("ERROR: CataWater\\shaders.txt missing");
            return false;
        }
        text.push_back('\n');
        std::string line;
        for (uint8_t c : text) {
            if (c != '\n' && c != '\r') { line.push_back(static_cast<char>(c)); continue; }
            char kind[16], family[48], path[256];
            unsigned permutation = 0;
            if (sscanf(line.c_str(), "%15s %47s %u %255s", kind, family, &permutation, path) == 4) {
                char key[96];
                snprintf(key, sizeof(key), "%s %s %u", kind, family, permutation);
                index[key] = path;
            }
            line.clear();
        }

        cw::Permutation real, fake, none;
        if (!cw::SelectPermutation(kDetail, kDepthMode, kDisplacements, cw::ReflectionKind::Real, true, cw::WaterPass::Above, real, error) ||
            !cw::SelectPermutation(kDetail, kDepthMode, kDisplacements, cw::ReflectionKind::Fake, true, cw::WaterPass::Above, fake, error) ||
            !cw::SelectPermutation(kDetail, kDepthMode, kDisplacements, cw::ReflectionKind::None, true, cw::WaterPass::Above, none, error)) {
            SetInfo("ERROR: %s", error.c_str());
            return false;
        }
        if (!LoadShader(cw::ShaderKind::Vertex, real.vertexName, real.vertex, index, s_vertex) ||
            !LoadShader(cw::ShaderKind::Pixel, real.pixelName, real.pixel, index, s_pixelReal) ||
            !LoadShader(cw::ShaderKind::Pixel, fake.pixelName, fake.pixel, index, s_pixelFake) ||
            !LoadShader(cw::ShaderKind::Pixel, none.pixelName, none.pixel, index, s_pixelNone)) {
            return false;
        }
        // HD: tranche 2. Optional: without these the water draws as before, without the ripples.
        cw::Permutation rippleReal, rippleFake, rippleNone;
        std::string rippleError;
        s_ripplesLoaded =
            cw::SelectPermutation(kDetail, kDepthMode, kRippleMaps, cw::ReflectionKind::Real, true, cw::WaterPass::Above, rippleReal, rippleError) &&
            cw::SelectPermutation(kDetail, kDepthMode, kRippleMaps, cw::ReflectionKind::Fake, true, cw::WaterPass::Above, rippleFake, rippleError) &&
            cw::SelectPermutation(kDetail, kDepthMode, kRippleMaps, cw::ReflectionKind::None, true, cw::WaterPass::Above, rippleNone, rippleError) &&
            LoadShader(cw::ShaderKind::Vertex, rippleReal.vertexName, rippleReal.vertex, index, s_rippleVertex) &&
            LoadShader(cw::ShaderKind::Pixel, rippleReal.pixelName, rippleReal.pixel, index, s_ripplePixelReal) &&
            LoadShader(cw::ShaderKind::Pixel, rippleFake.pixelName, rippleFake.pixel, index, s_ripplePixelFake) &&
            LoadShader(cw::ShaderKind::Pixel, rippleNone.pixelName, rippleNone.pixel, index, s_ripplePixelNone);
        // HD: tranche 4. Optional too: without them the camera in a liquid keeps the 3.3.5 water, as before. Cata never
        // has a real reflection below (Liquid::HasRealReflection), and the module is the same for all three kinds.
        cw::Permutation below, rippleBelow;
        std::string belowError;
        if (cw::SelectPermutation(kDetail, kDepthMode, kDisplacements, cw::ReflectionKind::None, true, cw::WaterPass::Below, below, belowError) &&
            !LoadShader(cw::ShaderKind::Pixel, below.pixelName, below.pixel, index, s_pixelBelow)) {
            s_pixelBelow = nullptr;
        }
        if (s_ripplesLoaded &&
            cw::SelectPermutation(kDetail, kDepthMode, kRippleMaps, cw::ReflectionKind::None, true, cw::WaterPass::Below, rippleBelow, belowError) &&
            !LoadShader(cw::ShaderKind::Pixel, rippleBelow.pixelName, rippleBelow.pixel, index, s_ripplePixelBelow)) {
            s_ripplePixelBelow = nullptr;
        }
        s_load = Load::Ready;
        return true;
    }

    // The 3.3.5 LiquidType id behind a settings object: the bank keeps one per id, at that index.
    uint32_t LiquidTypeOf(const Liquid::CMaterialSettings* settings) {
        static const Liquid::CMaterialSettings* s_last = nullptr;
        static uint32_t s_lastId = 0;
        if (settings == s_last) return s_lastId;
        const auto& bank = Liquid::CSettingsBank::s_settings;
        for (uint32_t i = 0; i < bank.m_count; i++) {
            if (bank.m_data[i] == settings) {
                s_last = settings;
                s_lastId = i;
                return i;
            }
        }
        return 0;
    }

    cw::Matrix ToMatrix(const C44Matrix& m) {
        cw::Matrix out;
        static_assert(sizeof(C44Matrix) == sizeof(cw::Matrix), "C44Matrix rows are four floats");
        memcpy(&out, &m, sizeof(out));
        return out;
    }

    // Cata Liquid::SetShaderLighting (VS0..4, PS0..4), with 3.3.5's CM2Lighting.
    void SetLighting(cw::MaterialConstants& k, const CM2Lighting& lighting, const C44Matrix& view) {
        C3Vector dir = lighting.m_sunDir * C33Matrix(view);
        k.vertex[1] = {dir.x, dir.y, dir.z, 1.0f};
        k.vertex[2] = {lighting.m_sunAmbient.x, lighting.m_sunAmbient.y, lighting.m_sunAmbient.z, 1.0f};
        k.vertex[3] = {lighting.m_sunDiffuse.x, lighting.m_sunDiffuse.y, lighting.m_sunDiffuse.z, 1.0f};
        k.vertex[4] = {lighting.m_sunSpecular.x, lighting.m_sunSpecular.y, lighting.m_sunSpecular.z, 6.0f};
        k.pixel[1] = {-lighting.m_sunDir.x, -lighting.m_sunDir.y, -lighting.m_sunDir.z, -1.0f};
        k.pixel[2] = {lighting.m_sunAmbient.x, lighting.m_sunAmbient.y, lighting.m_sunAmbient.z, 1.0f};
        k.pixel[3] = {lighting.m_sunDiffuse.x, lighting.m_sunDiffuse.y, lighting.m_sunDiffuse.z, 1.0f};
        k.pixel[4] = {lighting.m_sunSpecular.x, lighting.m_sunSpecular.y, lighting.m_sunSpecular.z, 50.0f};
        if (g_theGxDevicePtr->MasterEnable(GxMasterEnable_Fog) && fabsf(lighting.m_fogScale) >= 0.00000023841858f &&
            lighting.m_fogEnd > lighting.m_fogStart) {
            float scale = 1.0f / (lighting.m_fogEnd - lighting.m_fogStart);
            k.vertex[0] = {-(scale * CShaderEffect::s_fogMul), scale * lighting.m_fogEnd, lighting.m_fogDensity, 0.0f};
        } else {
            k.vertex[0] = {0.0f, 1.0f, 1.0f, 0.0f};
        }
        k.pixel[0] = {lighting.m_fogColor.x, lighting.m_fogColor.y, lighting.m_fogColor.z, 1.0f};
    }

    // Cata ViewSettings::CalculateParams and Liquid::SetScreenConstants (PS22, PS24..26), the near and far planes
    // read back from the projection the depth buffer was drawn with (D3D: c2 = f/(f-n), d2 = -n*f/(f-n)).
    bool SetScreen(cw::MaterialConstants& k, std::string& error) {
        const C44Matrix& p = g_theGxDevicePtr->m_projNative;
        if (!(p.c2 > 1.0f) || !(p.d2 < 0.0f)) {
            error = "unexpected projection";
            return false;
        }
        cw::ScreenSettings screen;
        screen.nearClip = -p.d2 / p.c2;
        screen.farClip = screen.nearClip * p.c2 / (p.c2 - 1.0f);
        float minX, maxX, minY, maxY, minZ, maxZ;
        GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);
        screen.viewportDepthMax = maxZ;
        screen.minX = minX;
        screen.maxX = maxX;
        screen.minY = minY;
        screen.maxY = maxY;
        CRect rect;
        g_theGxDevicePtr->CapsWindowSize(rect);
        screen.width = static_cast<uint32_t>(rect.maxX > 1.0f ? rect.maxX : 1.0f);
        screen.height = static_cast<uint32_t>(rect.maxY > 1.0f ? rect.maxY : 1.0f);
        return cw::SetScreenConstants(k, screen, error);
    }
}

namespace {
    // The sky for the reflections (GxCataSky, GxSeam.h): DayNight's dome as it is drawn this frame (DNSky::GenSphere,
    // SetColors), a unit sphere around the camera lowered by cos(45 degrees). Its vertices come in rings of equal
    // height, top first; each ring gives the elevation seen from the camera and its mean colour (the ring's colours
    // only vary with the side facing the sun, which this leaves out).
    void FillSky() {
        GxCataSky& sky = g_gxCataSky;
        sky.m_count = 0;
        const DayNight::DNSky& dome = DayNight::g_sky;
        // HD: tranche 3, no sky where the frame shows none (inside a map object without an outdoor portal in view).
        if (CWorldScene::s_unk_ADF570.m_maxViewDepth < 0.0f) return;
        if (!DayNight::g_dnInfo.m_showSky || !dome.m_geoVerts.m_data || !dome.m_clrVerts.m_data || !dome.m_nVerts) return;
        uint32_t i = 0;
        while (i < dome.m_nVerts && sky.m_count < 8) {
            const C3Vector& first = dome.m_geoVerts.m_data[i];
            uint32_t end = i, r = 0, g = 0, b = 0;
            while (end < dome.m_nVerts && fabsf(dome.m_geoVerts.m_data[end].z - first.z) < 1e-4f) {
                const CImVector& c = dome.m_clrVerts.m_data[end];
                r += c.r; g += c.g; b += c.b;
                end++;
            }
            const uint32_t n = end - i;
            const float length = sqrtf(first.x * first.x + first.y * first.y + first.z * first.z);
            if (!(length > 1e-6f)) return;
            sky.m_sinElevation[sky.m_count] = first.z / length;
            sky.m_color[sky.m_count] = (r / n) | ((g / n) << 8) | ((b / n) << 16);
            sky.m_count++;
            i = end;
        }
        // Top first, whatever order the dome was built in (CataSky.frag walks down).
        for (uint32_t a = 1; a < sky.m_count; a++)
            for (uint32_t b = a; b > 0 && sky.m_sinElevation[b] > sky.m_sinElevation[b - 1]; b--) {
                std::swap(sky.m_sinElevation[b], sky.m_sinElevation[b - 1]);
                std::swap(sky.m_color[b], sky.m_color[b - 1]);
            }
        if (sky.m_count < 2) sky.m_count = 0;
    }
}

namespace {
    // HD: tranche 3, the game's own sky in the reflections. Cata draws its reflections by rendering the world again
    // from the mirrored camera (CWorldScene::RenderReflection), the sky included. The sky only depends on where the
    // camera looks, not on the water's height, so it is drawn here once a frame: DayNight::RenderSky (the stars, the
    // sun and the moons, the dome, the clouds, the skybox models) into a render target, with the view mirrored in the
    // horizontal plane through the camera and the projection's y negated (two mirrors: the winding is kept, the picture
    // is upside down; the device flips it back, CataSkyTex.frag). The device puts it under the mirror and uses it for
    // the water without one (CataMirror.cpp).
    constexpr uint32_t kSkyWidth = 1024, kSkyHeight = 512;
    CTexture* s_skyTarget = nullptr;
    bool s_skyTargetFailed = false;

    void RenderMirroredSky() {
        g_gxCataSky.m_tex = nullptr;
        if (!g_gxCataWater.m_enabled || !HD::VulkanDevice() || s_submerged || s_skyTargetFailed || !DayNight::g_dnInfo.m_showSky ||
            CWorldScene::s_unk_ADF570.m_maxViewDepth < 0.0f) {
            return;
        }
        if (!s_skyTarget) {
            CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 1, 1, 0, 0, 0);
            s_skyTarget = TextureCreate(kSkyWidth, kSkyHeight, GxTex_Argb8888, GxTex_Argb8888, flags, nullptr, GxTexNullUserFunc,
                                        "hdCataMirroredSky", 0);
        }
        CGxTex* target = s_skyTarget ? TextureGetGxTex(s_skyTarget, 1, nullptr) : nullptr;
        if (!target || !target->m_flags.m_renderTarget) {
            s_skyTargetFailed = true;
            SetInfo("ERROR: the mirrored sky's render target could not be made (the rings stay)");
            return;
        }

        MaterialConstantsScope restoreConstants(g_theGxDevicePtr);
        CGxTex* savedColor = nullptr;
        g_theGxDevicePtr->DeviceGetRenderTarget(GxBuffers_Color, savedColor);
        float minX, maxX, minY, maxY, minZ, maxZ;
        GxXformViewport(minX, maxX, minY, maxY, minZ, maxZ);
        C44Matrix view, projection;
        GxXformView(view);
        GxXformProjection(projection);

        // The view mirrored in the plane z = h through the camera (h = 0 when the view is camera relative): a world
        // point p is seen as (p.x, p.y, 2h - p.z). Row vectors: row c (world z) negated, 2h * row c added to row d.
        const float h = -(view.d0 * view.c0 + view.d1 * view.c1 + view.d2 * view.c2);
        C44Matrix mirrored = view;
        mirrored.d0 = view.d0 + 2.0f * h * view.c0;
        mirrored.d1 = view.d1 + 2.0f * h * view.c1;
        mirrored.d2 = view.d2 + 2.0f * h * view.c2;
        mirrored.d3 = view.d3 + 2.0f * h * view.c3;
        mirrored.c0 = -view.c0;
        mirrored.c1 = -view.c1;
        mirrored.c2 = -view.c2;
        mirrored.c3 = -view.c3;
        C44Matrix flipped = projection;   // clip y negated
        flipped.a1 = -projection.a1;
        flipped.b1 = -projection.b1;
        flipped.c1 = -projection.c1;
        flipped.d1 = -projection.d1;

        g_gxCataSky.m_tex = target; // Identifies the private depth owner before native target binding.
        GxRenderTargetSet(GxBuffers_Color, target, 0);
        GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
        CImVector black;
        black.b = black.g = black.r = 0;
        black.a = 0xFF;
        GxSceneClear(3, black);
        GxXformSetView(mirrored);
        GxXformSetProjection(flipped);
        CShaderEffect::UpdateProjMatrix();
        CPortalView whole;   // the whole target (the frame's own view is cut to its outdoor portals)
        whole.m_rect.minX = 0.0f;
        whole.m_rect.minY = 0.0f;
        whole.m_rect.maxX = 1.0f;
        whole.m_rect.maxY = 1.0f;
        whole.m_maxViewDepth = CWorldScene::s_unk_ADF570.m_maxViewDepth;
        DayNight::RenderSky(&whole);
        GxXformSetProjection(projection);
        CShaderEffect::UpdateProjMatrix();
        GxXformSetView(view);
        GxRenderTargetSet(GxBuffers_Color, savedColor, 0);
        GxXformSetViewport(minX, maxX, minY, maxY, minZ, maxZ);
        g_gxCataSky.m_tex = target;
    }
}

bool HD::NativeWaterService::PrepareMaterialProxies(){return HD::VulkanDevice() && Proxies();}

bool HD::CataWaterRippleMaterialReady(){return s_ripplesLoaded;}

void HD::CataWaterBeginLiquidPass() {
    if (!s_info) {
        s_info = CVar::Register("hdCataWaterInfo", "HD: hdCataWater status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false); if (s_info) s_info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
    }
    if (!s_planeRange) {   // HD L19
        s_planeRange = CVar::Register("hdCataWaterPlaneRange", "HD: 2 = water joined to the reflected water takes the reflection, 1 = only batches touching its height, 0 = old test", 1, "2", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_waterLog = CVar::Register("hdCataWaterLog", "HD: water journal Logs\\hd-eau.txt, 1 on / 0 off", 1, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        {static bool hdDef1Once=false;if(!hdDef1Once){hdDef1Once=true;if(s_waterLog&&s_waterLog->m_intValue!=0)s_waterLog->InternalSet("0",true,false,false,false);}} // HD DEF1: diagnostic journal off at every start (a test never leaves it on)
    }
    {   // HD L19: last frame's water, once a second, while hdCataWaterLog is 1
        const uint32_t now = OsGetAsyncTimeMs();
        if (s_waterLog && s_waterLog->m_intValue && s_logLines < 6000 && now - s_logLast >= 1000) {
            s_logLast = now;
            if (std::FILE* f = std::fopen("Logs\\hd-eau.txt", "a")) {
                std::fprintf(f, "--- camera %.1f %.1f %.1f | plane %s %.2f | mirror %d | regle plage %d | %u eaux a moins de 120 m\n", s_logCamera[0], s_logCamera[1], s_logCamera[2],
                             g_gxCataPlane.m_valid ? "a" : "aucun, dernier", g_gxCataPlane.m_height, g_gxCataWater.m_mirror, s_planeRange ? s_planeRange->m_intValue : -1,
                             static_cast<unsigned>(s_logEntries.size()));
                s_logLines++;
                for (const LogEntry& e : s_logEntries) {
                    if (s_logLines >= 6000) break;
                    std::fprintf(f, "  centre %.1f %.1f | type %u %s | centre z %.2f | eau %s%.2f..%.2f | ancien test %s | rendu %s\n", e.x, e.y, e.type,
                                 e.mesh ? "objet" : "chunk", e.cz, e.haveRange ? "" : "(inconnue) ", e.low, e.high,
                                 e.oldMirror ? "reflet" : "pas de reflet", e.mirrored ? "REFLET" : e.sky ? "CIEL" : "SANS");
                    s_logLines++;
                }
                std::fclose(f);
            }
        }
        s_logEntries.clear();
        s_logCamera[0] = CWorldScene::s_activeWorldView.x;
        s_logCamera[1] = CWorldScene::s_activeWorldView.y;
        s_logCamera[2] = CWorldScene::s_activeWorldView.z;
    }
    g_gxCataWater.m_ready = 0;
    g_gxCataWater.m_mirror = 0;
    g_gxCataWater.m_sky = 0;
    FillSky();
    HD::CataRipplesBeginFrame(CWorldScene::s_activeWorldView);   // HD: tranche 2, before the device's stages run
    s_submerged = g_gxCataWater.m_enabled && World::SceneCamLiquidStatus(nullptr) != 0;
    RenderMirroredSky();   // HD: tranche 3, before the device's stages (they read it)
    if (!g_gxCataWater.m_enabled) SetInfo("off");
}

bool HD::CataWaterRender(Liquid::IEnvironment* environment, Liquid::IGeomFactory* geomFactory, const C3Vector& cameraPos,
                         const C44Matrix* transform, const CAaSphere* bounds, Liquid::CMaterialSettings* settings) {
    if (!g_gxCataWater.m_enabled || !HD::VulkanDevice() || !environment || !geomFactory || !transform || !bounds || !settings) return false;
    if (!LoadOnce()) return KeepOld(Kept_Files);
    // HD: tranche 4, Cata's Below shader when the camera is in a liquid (CMaterialWater::SetupRender, CameraSubmerged).
    if (s_submerged && !s_pixelBelow) { SetInfo("camera in a liquid: 3.3.5 water (CataProcWaterBelow not loaded)"); return KeepOld(Kept_Submerged); }
    const bool proxiesBefore = g_gxCataWater.m_proxy[0] && g_gxCataWater.m_proxy[1] && g_gxCataWater.m_proxy[2] && g_gxCataWater.m_proxySky &&
                               g_gxCataWater.m_proxyBlur && g_gxCataRipples.m_proxy[0] && g_gxCataRipples.m_proxy[1];
    if (!Proxies() || !proxiesBefore) {   // made this frame: the bridge hands them over from the next one
        SetInfo("the proxy textures are not ready yet");
        return KeepOld(Kept_Proxies);
    }
    if (!g_gxCataWater.m_ready) { SetInfo("no scene copy this frame: 3.3.5 water"); return KeepOld(Kept_NoCopy); }

    const uint32_t liquidType = LiquidTypeOf(settings);
    const cw::LiquidType* type = cw::ResolveType(s_types, liquidType, {});
    if (!type || type->material != 1) {   // magma, slime, Cata's other materials: 3.3.5
        SetInfo("LiquidType %u is not a Cata water (material 1) row: 3.3.5 water", liquidType);
        return KeepOld(Kept_Type);
    }

    // A chunk's factory bakes the transform into its vertices (SetTransform true: identity model); a map object's
    // (CMeshGeomFactory, false) keeps them in the group's space and the instance transform is Cata's model matrix.
    const bool identity = geomFactory->SetTransform(const_cast<C44Matrix*>(transform));

    CTexture* rampTexture = TextureCacheGetTexture(type->textures[1].c_str());
    CGxTex* ramp = rampTexture ? TextureGetGxTex(rampTexture, 1, nullptr) : nullptr;
    if (!ramp) ramp = settings->GetTexture(1, 1250);
    CGxTex* surface = BlpTexture(type->textures[2]);
    CGxTex* foam = BlpTexture(type->textures[3]);
    if (!ramp || !surface || !foam) {
        if (!ramp) SetInfo("LiquidType %u: no depth ramp texture (%s)", liquidType, type->textures[1].c_str());
        else if (!surface || !foam) SetInfo("LiquidType %u: surface or foam texture not loaded (%s, %s)", liquidType,
                                            type->textures[2].c_str(), type->textures[3].c_str());
        return KeepOld(Kept_Textures);
    }

    // The water's height: a map object's from its group (the bounds are the whole group's sphere), a chunk's from them.
    CAaBox meshBox;
    const bool mesh = HD::CataMirrorMeshWaterBox(geomFactory, *transform, meshBox);
    const float height = mesh ? (meshBox.b.z + meshBox.t.z) * 0.5f : bounds->c.z;
    const bool oldMirror = !s_submerged && g_gxCataWater.m_mirror && g_gxCataPlane.m_valid && fabsf(height - g_gxCataPlane.m_height) < kPlaneMatch;
    // HD L19: a chunk batch whose water reaches the plane takes the reflection too (see kRangeMax).
    float rangeLow = height, rangeHigh = height;
    const bool haveRange = !mesh && HD::CataMirrorInstanceRange(geomFactory, rangeLow, rangeHigh);
    const int rule = s_planeRange ? s_planeRange->m_intValue : 0;
    const bool canMirror = !oldMirror && haveRange && !s_submerged && g_gxCataWater.m_mirror && g_gxCataPlane.m_valid;
    // 1 (L19): the batch's own water reaches the plane. 2 (L19b): the batch is joined to the plane's water by its
    // neighbours (HdCataMirror.cpp, CataMirrorInChain): the whole stream out of the lake, no seam half way up.
    const bool rangeMirror = canMirror && ((rule == 1 && rangeHigh - rangeLow <= kRangeMax &&
                             g_gxCataPlane.m_height >= rangeLow - kPlaneMatch && g_gxCataPlane.m_height <= rangeHigh + kPlaneMatch) ||
                             (rule >= 2 && HD::CataMirrorInChain(geomFactory)));
    const bool mirrored = oldMirror || rangeMirror;

    // Constants: what Cata's SetupRender, SetupLighting and SetInstanceTransform put in s_vsConsts / s_psConsts.
    C44Matrix view;
    GxXformView(view);
    cw::MaterialConstants k;
    CM2Lighting lighting(*bounds);
    environment->SelectLights(&lighting);
    SetLighting(k, lighting, view);
    const bool relative = Liquid::UseCameraRelativeCoords() != 0;
    const cw::Vec3 camera{cameraPos.x, cameraPos.y, cameraPos.z};
    cw::SetTransformConstants(k, identity ? cw::Identity() : ToMatrix(*transform), ToMatrix(view), ToMatrix(g_theGxDevicePtr->m_projNative), camera, relative);
    std::string error;
    // HD: tranche 2, Cata SetDisplacementConstants: where the device's two maps lie this frame (it stepped them).
    std::vector<cw::Displacement> displacement;
    const GxCataRipples& ripples = g_gxCataRipples;
    if (s_ripplesLoaded && ripples.m_ready && ripples.m_proxy[0] && ripples.m_proxy[1]) {
        for (uint32_t i = 0; i < kRippleMaps; i++) {
            cw::Displacement d;
            d.position = {ripples.m_rawPos[i][0], ripples.m_rawPos[i][1], ripples.m_rawPos[i][2]};
            d.sizeX = ripples.m_size[i][0];
            d.sizeY = ripples.m_size[i][1];
            d.texWidth = d.texHeight = ripples.m_texSize[i];
            d.interpolation = ripples.m_interpolation[i];
            displacement.push_back(d);
        }
    }
    if (!cw::SetMaterialConstants(k, *type, OsGetAsyncTimeMs(), camera, Liquid::WorldCoordScale(), 1.0f, relative, displacement, error) ||
        !SetScreen(k, error)) {
        SetInfo("ERROR: LiquidType %u: %s", liquidType, error.c_str());
        return KeepOld(Kept_Constants);
    }

    g_theGxDevicePtr->RsPush();

    CGxBuf* vertexBuf = nullptr;
    CGxBuf* indexBuf = nullptr;
    CGxBatch batch;
    batch.m_primType = GxPrims_Last;
    batch.m_start = 0;
    batch.m_count = 0;
    batch.m_minIndex = 0;
    batch.m_maxIndex = 0;
    // Cata draws GxVBF_PNCT2 (its enum 7; 6 here): position, normal, colour, the 0.06-yard UVs and (0, depth).
    geomFactory->GetBuffers(GxVBF_PNCT2, &vertexBuf, &indexBuf, &batch);
    if (!vertexBuf || !indexBuf || !batch.m_count) {
        g_theGxDevicePtr->RsPop();
        SetInfo("LiquidType %u: the chunk gave no vertices in Cata's format", liquidType);
        return KeepOld(Kept_Buffers);
    }
    GxPrimVertexPtr(vertexBuf, GxVBF_PNCT2);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    g_theGxDevicePtr->RsSet(GxRs_Culling, 0);
    g_theGxDevicePtr->RsSet(GxRs_DepthWrite, 0);
    g_theGxDevicePtr->RsSet(GxRs_BlendingMode, 2);
    GxUpdateAlphaRef(GxRs_AlphaRef);
    g_theGxDevicePtr->RsSet(GxRs_Fog, 1);

    CGxTex* sceneColor = static_cast<CGxTex*>(g_gxCataWater.m_proxy[0]);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, sceneColor);
    // Cata (CMaterialWater::SetupRender): s1 the mirror for the water it was drawn for, else the sky's reflection.
    const bool sky = !s_submerged && !mirrored && g_gxCataWater.m_sky && s_pixelNone;
    if (s_waterLog && s_waterLog->m_intValue && s_logEntries.size() < 64) {   // HD L19: journal only
        const float dx = bounds->c.x - s_logCamera[0], dy = bounds->c.y - s_logCamera[1];
        if (dx * dx + dy * dy < 120.0f * 120.0f) {
            LogEntry e{bounds->c.x, bounds->c.y, bounds->c.z, mesh ? meshBox.b.z : rangeLow, mesh ? meshBox.t.z : rangeHigh, haveRange || mesh ? 1 : 0,
                       mesh ? 1 : 0, oldMirror ? 1 : 0, mirrored ? 1 : 0, sky ? 1 : 0, liquidType};
            s_logEntries.push_back(e);
        }
    }
    g_theGxDevicePtr->RsSet(GxRs_Texture1, static_cast<CGxTex*>(sky ? g_gxCataWater.m_proxySky : g_gxCataWater.m_proxy[1]));
    g_theGxDevicePtr->RsSet(GxRs_Texture2, ramp);
    // HD: Cata's blurred back buffer (the device's KawaseBlur, CataSceneCapture.cpp; the sharp copy without it). The
    // mirrored water's shader does not sample s3.
    g_theGxDevicePtr->RsSet(GxRs_Texture3, static_cast<CGxTex*>(g_gxCataWater.m_proxyBlur));
    g_theGxDevicePtr->RsSet(GxRs_Texture5, surface);
    g_theGxDevicePtr->RsSet(GxRs_Texture6, static_cast<CGxTex*>(g_gxCataWater.m_proxy[2]));
    g_theGxDevicePtr->RsSet(GxRs_Texture7, foam);

    // HD: tranche 2, Cata (CMaterialWater::SetupRender, HasDynamicDisplMap): s4 and s8 the maps, the shaders that read them.
    const bool rippled = displacement.size() == kRippleMaps && (!s_submerged || s_ripplePixelBelow);
    if (rippled) {
        g_theGxDevicePtr->RsSet(GxRs_Texture4, static_cast<CGxTex*>(ripples.m_proxy[0]));
        g_theGxDevicePtr->RsSet(GxRs_Texture8, static_cast<CGxTex*>(ripples.m_proxy[1]));
        g_theGxDevicePtr->RsSet(GxRs_VertexShader, s_rippleVertex);
        g_theGxDevicePtr->RsSet(GxRs_PixelShader, s_submerged ? s_ripplePixelBelow : mirrored ? s_ripplePixelReal : sky ? s_ripplePixelNone : s_ripplePixelFake);
    } else {
        g_theGxDevicePtr->RsSet(GxRs_VertexShader, s_vertex);
        g_theGxDevicePtr->RsSet(GxRs_PixelShader, s_submerged ? s_pixelBelow : mirrored ? s_pixelReal : sky ? s_pixelNone : s_pixelFake);
    }

    // Native per-draw HD pixel aliasing preserves the shared vertex/bone register file.
    MaterialConstantsScope restoreConstants(g_theGxDevicePtr);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(k.vertex.data()), kVertexRows);
    HD::PixelConstantsSet(0, reinterpret_cast<const float*>(k.pixel.data()), kPixelRows);

    g_theGxDevicePtr->Draw(&batch, 1);
    g_theGxDevicePtr->RsPop();
    g_gxCataWater.m_drawn++;

    if (s_submerged) SetInfo("on: LiquidType %u (%s), seen from below%s", liquidType, type->name.c_str(), rippled ? ", ripples" : "");
    else if (mirrored) SetInfo("on: LiquidType %u (%s), reflection%s", liquidType, type->name.c_str(), rippled ? ", ripples" : "");
    else SetInfo("on: LiquidType %u (%s), %s%s (%s %.1f..%.1f, plane %s %.1f)", liquidType, type->name.c_str(),
                 sky ? "sky reflection" : "no reflection", rippled ? ", ripples" : "", mesh ? "map object water" : "water", mesh ? meshBox.b.z : bounds->c.z,
                 mesh ? meshBox.t.z : bounds->c.z, g_gxCataPlane.m_valid ? "at" : "none, last", g_gxCataPlane.m_height);
    return true;
}

// HD: Cata's procedural magma (lava and slime) under Vulkan, hdCataMagma 1 (HdCataWater.h). Cata 4.3.4's
// CMaterialMagma::SetupRender at water detail > 0 (wowdecomp15595-w15595, LiquidMaterial.cpp): its shaders ProcMagma,
// its LiquidType row's floats 2..11 and int 2 (the colour), s0 the noise volume, s1..s3 the row's three layers.
namespace {
    constexpr uint32_t kMagmaVertexRows = 51;       // Cata uploads VS0..50
    constexpr uint32_t kMagmaPixelHighRows = 5;     // PS13..17: PS17 is the colour and the phase
    constexpr uint32_t kNoiseBytes = 128 * 128 * 16 * 4;

    enum class MagmaLoad { NotTried, Ready, Failed };
    MagmaLoad s_magmaLoad = MagmaLoad::NotTried;
    cw::LiquidTypes s_magmaTypes;
    CGxShader* s_magmaVertex = nullptr;
    CGxShader* s_magmaPixel = nullptr;
    std::vector<uint8_t> s_noiseBytes;
    CTexture* s_noise = nullptr;
    CVar* s_magma = nullptr;
    CVar* s_magmaInfo = nullptr;
    char s_magmaLastInfo[256] = "";

    void MagmaInfo(const char* format, ...) {
        char text[256];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        if (!s_magmaInfo || !strcmp(s_magmaLastInfo, text)) return;
        snprintf(s_magmaLastInfo, sizeof(s_magmaLastInfo), "%s", text);
        s_magmaInfo->Set(text, true, false, false, false);
    }

    // Cata's proceduralNoiseVolTex (XTextures\FX\noise.blob, 16 slices of 128x128 ARGB) as one 128x2048 strip: the
    // translated ProcMagma pixel module fetches it as the volume (CataProcMagma's GLSL, noiseVol).
    void NoiseFill(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg,
                   uint32_t& texelStrideInBytes, const void*& texels) {
        if (command != GxTex_Latch) return;
        texelStrideInBytes = 128 * 4;
        texels = s_noiseBytes.size() == kNoiseBytes ? s_noiseBytes.data() : nullptr;
    }

    bool MagmaShader(cw::ShaderKind kind, const std::map<std::string, std::string>& index, CGxShader*& out) {
        const char* key = kind == cw::ShaderKind::Vertex ? "vertex CataProcMagma 0" : "pixel CataProcMagma 0";
        auto entry = index.find(key);
        std::vector<uint8_t> code;
        if (entry == index.end() || !ReadRuntime(entry->second.c_str(), code)) {
            MagmaInfo("ERROR: %s not in CataWater\\shaders.txt or its file is missing", key);
            return false;
        }
        std::string error;
        out = cw::Gx::Acquire(kind, "CataProcMagma", 0, code, error);
        if (!out) MagmaInfo("ERROR: %s: %s (are the Cata modules in the Vulkan manifest?)", key, error.c_str());
        return out != nullptr;
    }

    bool MagmaLoadOnce() {
        if (s_magmaLoad != MagmaLoad::NotTried) return s_magmaLoad == MagmaLoad::Ready;
        s_magmaLoad = MagmaLoad::Failed;

        std::vector<uint8_t> dbc;
        std::string error;
        if (!ReadRuntime("DBFilesClient\\LiquidType.dbc", dbc) || !s_magmaTypes.Read(dbc, error)) {
            MagmaInfo("ERROR: CataWater\\DBFilesClient\\LiquidType.dbc: %s", error.empty() ? "missing" : error.c_str());
            return false;
        }

        std::map<std::string, std::string> index;
        std::vector<uint8_t> text;
        if (!ReadRuntime("shaders.txt", text)) {
            MagmaInfo("ERROR: CataWater\\shaders.txt missing");
            return false;
        }
        text.push_back('\n');
        std::string line;
        for (uint8_t c : text) {
            if (c != '\n' && c != '\r') { line.push_back(static_cast<char>(c)); continue; }
            char kind[16], family[48], path[256];
            unsigned permutation = 0;
            if (sscanf(line.c_str(), "%15s %47s %u %255s", kind, family, &permutation, path) == 4) {
                char key[96];
                snprintf(key, sizeof(key), "%s %s %u", kind, family, permutation);
                index[key] = path;
            }
            line.clear();
        }
        if (!MagmaShader(cw::ShaderKind::Vertex, index, s_magmaVertex) || !MagmaShader(cw::ShaderKind::Pixel, index, s_magmaPixel)) {
            return false;
        }

        if (!ReadRuntime("Textures\\XTextures\\FX\\noise.blob", s_noiseBytes) || s_noiseBytes.size() != kNoiseBytes) {
            MagmaInfo("ERROR: CataWater\\Textures\\XTextures\\FX\\noise.blob missing or not 1048576 bytes");
            return false;
        }
        CGxTexFlags flags(GxTex_Nearest, 0, 0, 0, 0, 0, 1, 0, 0, 0);
        s_noise = TextureCreate(128, 128 * 16, GxTex_Argb8888, GxTex_Argb8888, flags, nullptr, NoiseFill, "hdCataMagmaNoise", 0);
        if (!s_noise) {
            MagmaInfo("ERROR: the noise texture was refused");
            return false;
        }
        s_magmaLoad = MagmaLoad::Ready;
        return true;
    }

    // Cata Liquid::GetTexScrollMtx(time, (speed, 0)): the source truncates the float period to uint32.
    C44Matrix TexScroll(uint32_t time, float speed) {
        C44Matrix m;
        if (speed != 0.0f) {
            const uint32_t period = static_cast<uint32_t>(static_cast<int64_t>(1000.0f / speed));
            if (period) m.d0 = static_cast<float>(static_cast<double>(time % period) / static_cast<double>(period));
        }
        return m;
    }

    void PutMagmaMatrix(cw::MaterialConstants& k, uint32_t index, const C44Matrix& m) {
        const cw::Matrix rows = ToMatrix(m);
        for (uint32_t r = 0; r < 4; r++) k.vertex[26 + 4 * index + r] = rows[r];
    }
}

bool HD::CataMagmaRender(Liquid::IEnvironment* environment, Liquid::IGeomFactory* geomFactory, const C3Vector& cameraPos,
                         const C44Matrix* transform, const CAaSphere* bounds, Liquid::CMaterialSettings* settings) {
    if (!s_magma) {
        s_magma = CVar::Register("hdCataMagma", "HD: 1 = Cata 4.3.4 lava and slime (Vulkan), 0 = the 3.3.5 ones", 1, "1", nullptr,
                                 CATEGORY_GRAPHICS, false, nullptr, false);
        s_magmaInfo = CVar::Register("hdCataMagmaInfo", "HD: hdCataMagma status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS,
                                     false, nullptr, false); if (s_magmaInfo) s_magmaInfo->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
    }
    if (!s_magma || s_magma->m_intValue != 1) { MagmaInfo("off"); return false; }
    if (!HD::VulkanDevice()) { MagmaInfo("off: Vulkan only (gxApi vulkan)"); return false; }
    if (!environment || !geomFactory || !transform || !bounds || !settings) return false;
    if (!MagmaLoadOnce()) return false;

    const uint32_t liquidType = LiquidTypeOf(settings);
    const cw::LiquidType* type = s_magmaTypes.Find(liquidType);
    if (!type || type->material != 2) {
        MagmaInfo("LiquidType %u is not a Cata magma (material 2) row: 3.3.5 magma", liquidType);
        return false;
    }

    // Cata SetupRender: s0 the noise volume, s1..s3 settings textures 1..3 (the row's layers, no extension in the DBC).
    CGxTex* layers[3];
    for (uint32_t i = 0; i < 3; i++) {
        const std::string& name = type->textures[i + 1];
        layers[i] = name.empty() ? nullptr : BlpTexture(name + ".blp");
        if (!layers[i]) {
            MagmaInfo("LiquidType %u: layer %u not loaded (CataWater\\Textures\\%s.blp)", liquidType, i + 1, name.c_str());
            return false;
        }
    }
    CGxTex* noise = TextureGetGxTex(s_noise, 1, nullptr);
    if (!noise) { MagmaInfo("the noise texture is not ready yet"); return false; }

    // Constants: Cata's SetupLighting, SetInstanceTransform (a chunk's factory bakes the transform: identity model) and
    // SetupRender.
    const bool identity = geomFactory->SetTransform(const_cast<C44Matrix*>(transform));
    C44Matrix view;
    GxXformView(view);
    cw::MaterialConstants k;
    CM2Lighting lighting(*bounds);
    environment->SelectLights(&lighting);
    SetLighting(k, lighting, view);
    const bool relative = Liquid::UseCameraRelativeCoords() != 0;
    const cw::Vec3 camera{cameraPos.x, cameraPos.y, cameraPos.z};
    cw::SetTransformConstants(k, identity ? cw::Identity() : ToMatrix(*transform), ToMatrix(view), ToMatrix(g_theGxDevicePtr->m_projNative), camera, relative);

    const uint32_t time = OsGetAsyncTimeMs();
    const auto& f = type->floats;
    const float scale1 = Liquid::WorldCoordScale() * f[2] * 0.61699998f;
    const float scale2 = Liquid::WorldCoordScale() * f[2] * 0.47400001f;
    const float rotation = f[3] * 0.017453292f;
    const float speed = f[11];

    C44Matrix matrix0;
    matrix0.Scale(0.050000001f);
    matrix0 *= TexScroll(time, speed);
    PutMagmaMatrix(k, 0, matrix0);

    C44Matrix matrix1 = C44Matrix::RotationAroundZ(rotation);
    matrix1.Scale(scale1);
    matrix1 *= TexScroll(time, speed * 0.5f);
    PutMagmaMatrix(k, 1, matrix1);

    C44Matrix matrix2 = C44Matrix::RotationAroundZ(rotation);
    matrix2.Scale(scale2);
    matrix2 *= TexScroll(time, speed * 0.5f);
    PutMagmaMatrix(k, 2, matrix2);

    for (uint32_t i = 0; i < 3; i++) {
        C44Matrix matrix = C44Matrix::RotationAroundZ(f[i + 7] * 0.017453292f);
        matrix.Scale(f[i + 4]);
        matrix *= TexScroll(time, speed);
        PutMagmaMatrix(k, i + 3, matrix);
    }

    const float phase = static_cast<float>(static_cast<double>(time % 600000) * 0.0000016666667);
    const uint32_t color = static_cast<uint32_t>(type->ints[2]);
    k.vertex[50] = {f[10], 0.0f, 0.0f, phase};
    k.pixel[17] = {static_cast<float>((color >> 16) & 0xFF) * 0.0039215689f, static_cast<float>((color >> 8) & 0xFF) * 0.0039215689f,
                   static_cast<float>(color & 0xFF) * 0.0039215689f, phase};

    g_theGxDevicePtr->RsPush();

    CGxBuf* vertexBuf = nullptr;
    CGxBuf* indexBuf = nullptr;
    CGxBatch batch;
    batch.m_primType = GxPrims_Last;
    batch.m_start = 0;
    batch.m_count = 0;
    batch.m_minIndex = 0;
    batch.m_maxIndex = 0;
    // Cata draws its GxVBF_PT (position, the liquid UVs); the 3.3.5 magma's GxVBF_PCT carries the same two.
    geomFactory->GetBuffers(GxVBF_PCT, &vertexBuf, &indexBuf, &batch);
    if (!vertexBuf || !indexBuf || !batch.m_count) {
        g_theGxDevicePtr->RsPop();
        MagmaInfo("LiquidType %u: the instance gave no vertices", liquidType);
        return false;
    }
    GxPrimVertexPtr(vertexBuf, GxVBF_PCT);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    g_theGxDevicePtr->RsSet(GxRs_BlendingMode, 0);
    g_theGxDevicePtr->RsSet(GxRs_DepthWrite, 1);
    GxUpdateAlphaRef(GxRs_AlphaRef);
    g_theGxDevicePtr->RsSet(GxRs_Fog, 1);
    g_theGxDevicePtr->RsSet(GxRs_Culling, 0);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, noise);
    g_theGxDevicePtr->RsSet(GxRs_Texture1, layers[0]);
    g_theGxDevicePtr->RsSet(GxRs_Texture2, layers[1]);
    g_theGxDevicePtr->RsSet(GxRs_Texture3, layers[2]);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, s_magmaVertex);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, s_magmaPixel);

    // Native per-draw pixel aliasing; restore the engine shadows after this material draw.
    MaterialConstantsScope restoreConstants(g_theGxDevicePtr);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(k.vertex.data()), kMagmaVertexRows);
    HD::PixelConstantsSet(0, reinterpret_cast<const float*>(k.pixel.data()), kPixelLowRows + kMagmaPixelHighRows);

    g_theGxDevicePtr->Draw(&batch, 1);
    g_theGxDevicePtr->RsPop();

    MagmaInfo("on: LiquidType %u (%s)", liquidType, type->name.c_str());
    return true;
}
