#include "WoW/Source/WorldClient/HD/HdSky.h"
#include "WoW/Source/Ui/Visuals.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <windows.h>
#include <d3dcompiler.h>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/Types.h"
#include "WoW/Source/WorldClient/HD/HdGxShader.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"

namespace {
    // Source of truth: scratchpad sky/impl/shaders/hd_sky.hlsl (checked against Forever's dnsky / dnclouds DXIL).
    const char kShader[] = R"HLSL(
// Waystone HD sky shaders (vs_3_0 / ps_3_0): WoW Forever's dnsky and dnclouds on the 3.3.5 sky dome and cloud dome.
// Entry points: SkyVS + SkyPS (DNSky::Render), CloudVS + CloudPS (DNClouds::Render). Bound through Gx, so the
// client keeps its own states (blending, depth range, scissor, cloud texture and sampler).
// Forever renders in linear light and encodes to gamma at the end; the client draws in gamma. Each Forever step is
// done here in linear light and encoded back, so on screen it gives what Forever's shader + gamma encode gives.
// With the Forever terms at 0 (hdSky 2) the output is exactly the fixed-function result.

float4 c_mvp[4]     : register(c240); // VS: rows of world * view * projection (pos = x*r0 + y*r1 + z*r2 + r3)

float4 c_sun        : register(c200); // PS: xyz = direction to the sun (or the moon at night), w = glow start (a cosine)
float4 c_glow       : register(c201); // PS: rgb = glow colour, LINEAR; w = glow strength at and below the horizon
float4 c_parm       : register(c202); // PS: x = glow strength above the horizon, y = dither amplitude (output units),
                                      //     z = per-frame noise offset, w = 1 for the test view
float4 c_cloudLight : register(c203); // PS: xyz = direction to the sun (unit), w = cloud sun scatter strength
float4 c_cloudSun   : register(c204); // PS: rgb = cloud sun colour (gamma, 0..1)

sampler2D s_cloud   : register(s0);   // the client's cloud texture (DNClouds), with the client's sampler state

struct SkyVertex {
    float4 pos : POSITION;
    float4 col : COLOR0;    // the client's dome colour (gamma), interpolated in gamma like Forever's
    float4 dir : TEXCOORD0; // xyz = dome point (the camera is at the origin), w = its height (0 = horizon)
};

struct CloudVertex {
    float4 pos : POSITION;
    float4 col : COLOR0;
    float2 uv  : TEXCOORD0;
    float3 nrm : TEXCOORD1; // direction of the dome point, normalized per vertex (not again per pixel, like Forever)
};

float3 Lin(float3 c) { return pow(max(c, 0.0), 2.2); }
float3 Gam(float3 c) { return pow(max(c, 0.0), 1.0 / 2.2); }

// Forever's triangular-PDF dither (sign(n) * (1 - sqrt(1 - |n|)), n uniform in -1..1 per channel). Forever reads n
// from a 128x128 blue noise texture; here n is interleaved gradient noise (no texture in the 3.3.5 data).
float3 Dither(float2 vpos) {
    float k = dot(vpos + c_parm.zz, float2(0.06711056, 0.00583715));
    float3 n = frac(52.9829189 * frac(k + float3(0.0, 0.3183099, 0.6180340))) * 2.0 - 1.0;
    return sign(n) * (1.0 - sqrt(1.0 - abs(n)));
}

SkyVertex SkyVS(float3 p : POSITION, float4 col : COLOR0) {
    SkyVertex o;
    o.pos = p.x * c_mvp[0] + p.y * c_mvp[1] + p.z * c_mvp[2] + c_mvp[3];
    o.col = col;
    o.dir = float4(p, p.z);
    return o;
}

// dnsky: glow toward the sun, cubic from the cone edge to the sun, stronger at the horizon; then the dither.
float4 SkyPS(SkyVertex i, float2 vpos : VPOS) : COLOR0 {
    float3 c = i.col.rgb;
    float t = saturate(dot(normalize(i.dir.xyz), normalize(c_sun.xyz)) - c_sun.w);
    float k = 0.0;
    if (t > 0.0) {
        float g = t / (1.0 - c_sun.w);
        g = saturate(g * g * g);
        k = g * lerp(c_glow.w, c_parm.x, saturate(i.dir.w * 50.0));
        float3 l = Lin(c);
        c = Gam(l + k * (c_glow.rgb - l));
    }
    if (c_parm.w > 0.5) c = k; // test view: the glow amount in grey
    c += Dither(vpos) * c_parm.y;
    return float4(c, i.col.a);
}

CloudVertex CloudVS(float3 p : POSITION, float4 col : COLOR0, float2 uv : TEXCOORD0) {
    CloudVertex o;
    o.pos = p.x * c_mvp[0] + p.y * c_mvp[1] + p.z * c_mvp[2] + c_mvp[3];
    o.col = col;
    o.uv = uv;
    o.nrm = normalize(p);
    return o;
}

// dnclouds: texture x vertex colour (the fixed-function modulate), then the sun shining through thin cloud near
// the sun: base * (1 + sun * N.L^2 * (1 - alpha) * strength), in linear light.
float4 CloudPS(CloudVertex i, float2 vpos : VPOS) : COLOR0 {
    float4 t = tex2D(s_cloud, i.uv);
    float3 c = i.col.rgb * t.rgb;
    float a = t.a * i.col.a;
    float d = saturate(dot(i.nrm, c_cloudLight.xyz));
    float s = d * d * saturate(1.0 - a) * abs(c_cloudLight.w);
    if (s > 0.0) {
        c *= pow(1.0 + Lin(c_cloudSun.rgb) * s, 1.0 / 2.2);
    }
    if (c_parm.w > 0.5) c = float3(s, 0.0, 0.0); // test view: the sun scatter in red
    c += Dither(vpos) * c_parm.y;
    return float4(c, a);
}
)HLSL";

    // Registers no 3.3.5 shader reads (VS c240-c243, PS c200-c204); the client's values are put back after each draw.
    const uint32_t kVsFirst = 240;
    const uint32_t kVsCount = 4;
    const uint32_t kPsFirst = 200;
    const uint32_t kPsCount = 5;

    const float kNightStart = 0.9236111f; // the client's sun/moon switch (DNClouds::SetupBumpMap, hdSunPath): 22:10
    const float kNightEnd = 0.2013889f;   // 4:50
    const float kSwitchFade = 0.0138889f; // the glow fades out and in over 20 game minutes around the switch

    enum { StateIdle, StateCompiling, StateReady, StateFailed };
    enum { Sh_SkyVS, Sh_SkyPS, Sh_CloudVS, Sh_CloudPS, Sh_Count };
    const char* const kEntry[Sh_Count] = { "SkyVS", "SkyPS", "CloudVS", "CloudPS" };
    const char* const kName[Sh_Count] = { "HdSky:SkyVS", "HdSky:SkyPS", "HdSky:CloudVS", "HdSky:CloudPS" };

    typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

    std::atomic<int32_t> s_state(StateIdle);
    std::vector<uint8_t> s_code[Sh_Count]; // written by the compile thread before s_state = StateReady
    char s_threadInfo[300] = "";           // written by the compile thread before s_state changes to Ready/Failed
    CGxShader* s_shaders[Sh_Count] = {};
    bool s_shadersFailed = false;

    float s_savedVs[kVsCount * 4];
    float s_savedPs[kPsCount * 4];
    uint32_t s_frame = 0;
    char s_lastInfo[300] = "";

    CVar* s_sky = nullptr;
    CVar* s_glow = nullptr;
    CVar* s_glowHorizon = nullptr;
    CVar* s_glowSize = nullptr;
    CVar* s_cloudSun = nullptr;
    CVar* s_dither = nullptr;
    CVar* s_info = nullptr;

    void RegisterCVars() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        HD::ShaderCVars(); // HD: hdShaderDump
        s_sky = CVar::Register("hdSky", "HD: 1 = WoW Forever sky and clouds, 2 = our shaders with the 3.3.5 look, 3 = test view, 0 = Blizzard sky", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_glow = CVar::Register("hdSkyGlow", "HD: sun glow strength above the horizon (0 = off)", 1, "0.35", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_glowHorizon = CVar::Register("hdSkyGlowHorizon", "HD: sun glow strength at and below the horizon", 1, "0.8", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_glowSize = CVar::Register("hdSkyGlowSize", "HD: sun glow cone half-angle in degrees", 1, "50", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_cloudSun = CVar::Register("hdSkyCloudSun", "HD: sun shining through thin clouds near the sun (0 = off)", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_dither = CVar::Register("hdSkyDither", "HD: sky dither in 1/255 steps (0 = off)", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_info = CVar::Register("hdSkyInfo", "HD: hdSky status (read only)", 0, "not started", nullptr, CATEGORY_GRAPHICS, false, nullptr, false); if (s_info) s_info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
    }

    void SetInfo(const char* text) {
        if (!s_info || !strcmp(s_lastInfo, text)) return;
        snprintf(s_lastInfo, sizeof(s_lastInfo), "%s", text);
        s_info->Set(text, true, false, false, false);
    }

    float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    float CVarValue(CVar* cvar, float fallback, float lo, float hi) {
        return cvar ? Clamp(cvar->m_floatValue, lo, hi) : fallback;
    }

    // Compile thread: the four entry points, all or nothing.
    void CompileAll() {
        ULONGLONG start = GetTickCount64();
        HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
        D3DCompileFn compile = module ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
        if (!compile) {
            snprintf(s_threadInfo, sizeof(s_threadInfo), "ERROR: d3dcompiler_47.dll not found, Blizzard sky kept");
            s_state.store(StateFailed);
            return;
        }
        for (int32_t i = 0; i < Sh_Count; i++) {
            bool vertex = i == Sh_SkyVS || i == Sh_CloudVS;
            ID3DBlob* code = nullptr;
            ID3DBlob* errors = nullptr;
            HRESULT hr = compile(kShader, strlen(kShader), "HdSky", nullptr, nullptr, kEntry[i], vertex ? "vs_3_0" : "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            if (FAILED(hr) || !code) {
                snprintf(s_threadInfo, sizeof(s_threadInfo), "ERROR: shader %s: %s", kEntry[i], errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown");
                if (errors) errors->Release();
                if (code) code->Release();
                s_state.store(StateFailed);
                return;
            }
            if (errors) errors->Release();
            const uint8_t* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
            s_code[i].assign(bytes, bytes + code->GetBufferSize());
            HD::ShaderDump("HdSky", !vertex, i, 0, bytes, code->GetBufferSize()); // HD: hdShaderDump
            code->Release();
        }
        snprintf(s_threadInfo, sizeof(s_threadInfo), "%d shaders compiled in %u ms", static_cast<int>(Sh_Count), static_cast<uint32_t>(GetTickCount64() - start));
        s_state.store(StateReady);
    }

    // Our shaders, made once. They live in the device's own shader list, so the device releases them and makes them
    // again from their code like its own (device reset or re-creation).
    bool EnsureShaders() {
        if (s_shaders[Sh_Count - 1]) return true;
        if (s_shadersFailed) return false;
        int32_t state = s_state.load();
        if (state == StateIdle) {
            int32_t expected = StateIdle;
            if (s_state.compare_exchange_strong(expected, StateCompiling)) std::thread(CompileAll).detach();
            SetInfo("compiling (Blizzard sky until done)");
            return false;
        }
        if (state == StateCompiling) return false;
        if (state == StateFailed) {
            s_shadersFailed = true;
            SetInfo(s_threadInfo);
            return false;
        }
        CGxShader* made[Sh_Count] = {};
        for (int32_t i = 0; i < Sh_Count; i++) {
            EGxShTarget target = i == Sh_SkyVS || i == Sh_CloudVS ? GxSh_Vertex : GxSh_Pixel;
            CGxShader* shader = g_theGxDevicePtr->m_shaderList[target].Ptr(kName[i]);
            if (!shader) {
                shader = g_theGxDevicePtr->m_shaderList[target].New(kName[i], 0, 0);
                shader->m_refCount++;
                shader->m_target = target;
            }
            const std::vector<uint8_t>& code = s_code[i];
            shader->m_code.SetCount(static_cast<uint32_t>(code.size()));
            memcpy(shader->m_code.m_data, &code[0], code.size());
            shader->m_loaded = 0;
            if (!shader->Valid()) {
                char text[300];
                snprintf(text, sizeof(text), "ERROR: the device refused shader %s, Blizzard sky kept", kEntry[i]);
                SetInfo(text);
                s_shadersFailed = true;
                return false;
            }
            made[i] = shader;
        }
        for (int32_t i = 0; i < Sh_Count; i++) s_shaders[i] = made[i];
        return true;
    }

    // Unit direction to the sun by day, to the moon at night (the client's own choice for the cloud lighting), and
    // how far from the sun/moon switch we are (0 at the switch, 1 from 20 game minutes away).
    void SkyLight(float* dir, float* fade, bool* moon) {
        DayNight::DNInfo& info = DayNight::g_dnInfo;
        float t = info.m_dayProgression;
        bool night = t < kNightEnd || t > kNightStart;
        const C3Vector& pos = night ? DayNight::g_planets.m_moon.m_position : DayNight::g_planets.m_sun.m_position;
        float x = pos.x - info.m_cameraPos.x;
        float y = pos.y - info.m_cameraPos.y;
        float z = pos.z - info.m_cameraPos.z;
        float len = sqrtf(x * x + y * y + z * z);
        if (len < 1e-4f) { x = 0.0f; y = 0.0f; z = 1.0f; len = 1.0f; }
        dir[0] = x / len;
        dir[1] = y / len;
        dir[2] = z / len;
        float toSwitch;
        if (!night) toSwitch = fminf(t - kNightEnd, kNightStart - t);
        else if (t < 0.5f) toSwitch = fminf(kNightEnd - t, t + 1.0f - kNightStart);
        else toSwitch = fminf(t - kNightStart, 1.0f - t + kNightEnd);
        *fade = Clamp(toSwitch / kSwitchFade, 0.0f, 1.0f);
        *moon = night;
    }

    float Linear(uint8_t c) {
        return powf(c * (1.0f / 255.0f), 2.2f);
    }
}

bool HD::SkyBeginDraw(int32_t pass) {
    if (Visuals::Current() != Visuals::AzerothRemastered) return false;
    RegisterCVars();
    int32_t mode = s_sky ? s_sky->m_intValue : 0;
    if (mode <= 0) {
        SetInfo("off (Blizzard sky)");
        return false;
    }
    if (!HD::ShaderSwapDevice()) { // HD: D3D9, or Vulkan through the translated modules
        SetInfo("off: needs gxApi d3d9 or vulkan");
        return false;
    }
    if (!EnsureShaders()) return false;

    bool forever = mode != 2;
    bool test = mode == 3;
    DayNight::DNInfo& info = DayNight::g_dnInfo;

    float dir[3];
    float fade;
    bool moon;
    SkyLight(dir, &fade, &moon);

    // Forever's terms. hdSky 2: all off, and an empty glow cone (start cosine 2), so the output is the dome colour
    // (and the texture x colour for the clouds) exactly.
    float storm = 1.0f - Clamp(info.m_stormBlend, 0.0f, 1.0f);
    float glow = forever ? CVarValue(s_glow, 0.35f, 0.0f, 4.0f) * fade * storm : 0.0f;
    float glowHorizon = forever ? CVarValue(s_glowHorizon, 0.8f, 0.0f, 4.0f) * fade * storm : 0.0f;
    float glowCos = cosf(CVarValue(s_glowSize, 50.0f, 1.0f, 179.0f) * 0.017453292f);
    if (glowCos > 0.999f) glowCos = 0.999f;
    if (glow <= 0.0f && glowHorizon <= 0.0f) glowCos = 2.0f;
    float cloudSun = forever ? CVarValue(s_cloudSun, 1.0f, 0.0f, 8.0f) * fade : 0.0f;
    float dither = forever ? CVarValue(s_dither, 1.0f, 0.0f, 8.0f) * (1.0f / 255.0f) : 0.0f;
    if (pass == HD::SkyPass_Dome) s_frame = (s_frame + 1) & 63; // Forever moves its noise every frame

    const CImVector& sun = info.m_bands.m_sunCloudColors[0];      // sun and moon colour (band 9): the glow colour
    const CImVector& cloudColor = info.m_bands.m_sunCloudColors[1]; // cloud sun colour (band 10)
    float ps[kPsCount * 4] = {
        dir[0], dir[1], dir[2], glowCos,
        Linear(sun.r), Linear(sun.g), Linear(sun.b), glowHorizon,
        glow, dither, static_cast<float>(s_frame) * 5.588238f, test ? 1.0f : 0.0f,
        dir[0], dir[1], dir[2], cloudSun,
        cloudColor.r * (1.0f / 255.0f), cloudColor.g * (1.0f / 255.0f), cloudColor.b * (1.0f / 255.0f), 0.0f,
    };

    // world * view * projection, as the fixed-function pipeline gets them (rows; the shader does x*r0 + y*r1 + z*r2 + r3)
    const CGxMatrixStack& world = g_theGxDevicePtr->m_xforms[GxXform_World];
    const CGxMatrixStack& view = g_theGxDevicePtr->m_xforms[GxXform_View];
    C44Matrix mvp = world.m_mtx[world.m_level] * view.m_mtx[view.m_level] * g_theGxDevicePtr->m_projNative;
    float vs[kVsCount * 4];
    memcpy(vs, &mvp, sizeof(vs));

    memcpy(s_savedVs, &CGxDevice::s_shadowConstants[1].constants[kVsFirst * 4], sizeof(s_savedVs));
    memcpy(s_savedPs, &CGxDevice::s_shadowConstants[0].constants[kPsFirst * 4], sizeof(s_savedPs));
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, kVsFirst, vs, kVsCount);
    HD::PixelConstantsSet(kPsFirst, ps, kPsCount); // HD: also the vertex rows the Vulkan modules read
    bool clouds = pass == HD::SkyPass_Clouds;
    GxRsSet(GxRs_VertexShader, s_shaders[clouds ? Sh_CloudVS : Sh_SkyVS]);
    GxRsSet(GxRs_PixelShader, s_shaders[clouds ? Sh_CloudPS : Sh_SkyPS]);

    char text[300];
    if (!forever) {
        snprintf(text, sizeof(text), "on, 3.3.5 look (hdSky 2), %s", s_threadInfo);
    } else {
        snprintf(text, sizeof(text), "on%s, glow %.2f / %.2f (horizon), %.0f deg around the %s (x%.1f), cloud sun %.2f, dither %.1f/255, %s",
                 test ? " (test view)" : "", CVarValue(s_glow, 0.35f, 0.0f, 4.0f), CVarValue(s_glowHorizon, 0.8f, 0.0f, 4.0f),
                 CVarValue(s_glowSize, 50.0f, 1.0f, 179.0f), moon ? "moon" : "sun", fade * storm,
                 CVarValue(s_cloudSun, 1.0f, 0.0f, 8.0f), dither * 255.0f, s_threadInfo);
    }
    SetInfo(text);
    return true;
}

void HD::SkyEndDraw() {
    if (!g_theGxDevicePtr) return;
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, kVsFirst, s_savedVs, kVsCount);
    HD::PixelConstantsSet(kPsFirst, s_savedPs, kPsCount); // HD
}
