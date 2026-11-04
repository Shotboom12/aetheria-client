#include "WoW/Source/WorldClient/HD/HdTime.h"
#include "WoW/Source/WorldClient/HD/HdDayNightMath.h"
#include "WoW/Source/WorldClient/HD/HdForever.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "Engine/Source/Tempest/CMath.h"
#include "WoW/Source/DB/Rec/LightParamsRec.h"
#include "WoW/Source/DB/Rec/LightRec.h"

namespace {
    // HD records get IDs above every real LightParams ID: kMarker + index into s_records.
    const int32_t kMarker = 0x40000000;
    const int32_t kMaxKeys = 32;
    const int32_t kBands = 20; // HD: 0-17 the 3.3.5 bands, 18 horizon ambient, 19 ground ambient (hd_forever_ambient.txt)

    struct Band {
        int32_t count = 0;
        int32_t time[kMaxKeys];
        uint32_t color[kMaxKeys];
    };

    struct Setting {
        Band bands[kBands];
    };

    struct LightEntry {
        int32_t map;
        int32_t slots[8];
    };

    struct HdRecord {
        LightParamsRec rec;
        int32_t realId;
        const Setting* setting;
    };

    // HD: Forever fog layers (hdFog), one key per time of day
    struct FogKey {
        int32_t time;
        int32_t count;
        HD::ForeverFogLayer layers[3];
    };

    std::unordered_map<int32_t, std::vector<FogKey>> s_fog;

    // HD: Forever colour grading (hdGrade), one LUT per time of day
    struct GradeKey {
        int32_t time;
        int32_t lut;
    };

    std::unordered_map<int32_t, std::vector<GradeKey>> s_grading;

    CVar* s_enabled = nullptr;
    CVar* s_info = nullptr;
    CVar* s_force = nullptr;
    CVar* s_light = nullptr; // HD: hdForeverLight
    bool s_triedLoad = false;
    std::unordered_map<int32_t, LightEntry> s_lights;
    std::unordered_map<int32_t, Setting*> s_settings;
    std::unordered_map<uint64_t, HdRecord*> s_recordByPair;
    std::vector<HdRecord*> s_records;

    void RegisterCVars() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        s_enabled = CVar::Register("hdForever", "HD: 1 = WoW Forever light colours where Forever has the light, 0 = original", 1, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_force = CVar::Register("hdForeverForce", "HD: 0 = Forever setting by light, else use this Forever setting everywhere (test)", 1, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_light = CVar::Register("hdForeverLight", "HD: 1 = Forever's sun and ambient on the world, 0 = 3.3.5, between = mix", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_info = CVar::Register("hdForeverInfo", "HD: what hdForever loaded (read only)", 0, "not loaded yet", nullptr, CATEGORY_GRAPHICS, false, nullptr, false); if (s_info) s_info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
    }

    void SetInfo(const char* text) {
        if (s_info) {
            s_info->Set(text, true, false, false, false);
        }
    }

    FILE* OpenData(const char* name) {
        char path[260];
        snprintf(path, sizeof(path), "forever_data\\%s", name);
        FILE* file = fopen(path, "rb");
        if (file) return file;
        snprintf(path, sizeof(path), "..\\..\\forever_data\\%s", name);
        return fopen(path, "rb");
    }

    // "L light map slot0..slot7" and "B setting band count time colour ..." lines (hd_forever_light.txt,
    // hd_forever_ambient.txt). Returns the number of lines skipped.
    int32_t LoadBands(FILE* file) {
        static char line[8192];
        int32_t badLines = 0;
        while (fgets(line, sizeof(line), file)) {
            char* p = line;
            if (*p == 'L') {
                LightEntry entry;
                int32_t id = strtol(p + 1, &p, 10);
                entry.map = strtol(p, &p, 10);
                for (int32_t i = 0; i < 8; i++) entry.slots[i] = strtol(p, &p, 10);
                s_lights[id] = entry;
            } else if (*p == 'B') {
                int32_t setting = strtol(p + 1, &p, 10);
                int32_t band = strtol(p, &p, 10);
                int32_t count = strtol(p, &p, 10);
                if (band < 0 || band >= kBands || count < 1 || count > kMaxKeys) {
                    badLines++;
                    continue;
                }
                Setting*& s = s_settings[setting];
                if (!s) s = new Setting();
                Band& b = s->bands[band];
                b.count = count;
                for (int32_t i = 0; i < count; i++) {
                    b.time[i] = strtol(p, &p, 10);
                    b.color[i] = static_cast<uint32_t>(strtoul(p, &p, 16));
                }
            }
        }
        fclose(file);
        return badLines;
    }

    void SetColor(float* dst, uint32_t rgb) {
        dst[0] = ((rgb >> 16) & 0xFF) * (1.0f / 255.0f);
        dst[1] = ((rgb >> 8) & 0xFF) * (1.0f / 255.0f);
        dst[2] = (rgb & 0xFF) * (1.0f / 255.0f);
    }

    // forever_data\hd_forever_fog.txt (V 2): "F setting time layer flags diffuse emissive shadow start density
    // shadowMul topRate topHeight bottomRate bottomHeight intensity g strength exponent"
    int32_t LoadFog() {
        static const char* paths[] = {
            "forever_data\\hd_forever_fog.txt",
            "..\\..\\forever_data\\hd_forever_fog.txt",
        };
        FILE* file = nullptr;
        for (const char* path : paths) {
            file = fopen(path, "rb");
            if (file) break;
        }
        if (!file) return -1;

        char line[512];
        int32_t layers = 0;
        while (fgets(line, sizeof(line), file)) {
            if (line[0] != 'F') continue;
            char* p = line + 1;
            int32_t setting = strtol(p, &p, 10);
            int32_t time = strtol(p, &p, 10);
            int32_t index = strtol(p, &p, 10);
            if (index < 0 || index > 2) continue;
            HD::ForeverFogLayer layer;
            layer.flags = strtol(p, &p, 10);
            SetColor(layer.diffuse, static_cast<uint32_t>(strtoul(p, &p, 16)));
            SetColor(layer.emissive, static_cast<uint32_t>(strtoul(p, &p, 16)));
            SetColor(layer.shadow, static_cast<uint32_t>(strtoul(p, &p, 16)));
            layer.start = strtof(p, &p);
            layer.density = strtof(p, &p);
            layer.shadowMul = strtof(p, &p);
            layer.topRate = strtof(p, &p);
            layer.topHeight = strtof(p, &p);
            layer.bottomRate = strtof(p, &p);
            layer.bottomHeight = strtof(p, &p);
            layer.intensity = strtof(p, &p);
            layer.g = strtof(p, &p);
            layer.strength = strtof(p, &p);
            layer.exponent = strtof(p, &p);

            std::vector<FogKey>& keys = s_fog[setting];
            if (keys.empty() || keys.back().time != time) {
                FogKey key;
                memset(&key, 0, sizeof(key));
                key.time = time;
                keys.push_back(key);
            }
            FogKey& key = keys.back();
            key.layers[index] = layer;
            if (key.count < index + 1) key.count = index + 1;
            layers++;
        }
        fclose(file);
        return layers;
    }

    // forever_data\hd_forever_grading.txt: "G setting time lut"
    int32_t LoadGrading() {
        static const char* paths[] = {
            "forever_data\\hd_forever_grading.txt",
            "..\\..\\forever_data\\hd_forever_grading.txt",
        };
        FILE* file = nullptr;
        for (const char* path : paths) {
            file = fopen(path, "rb");
            if (file) break;
        }
        if (!file) return -1;

        char line[128];
        int32_t count = 0;
        while (fgets(line, sizeof(line), file)) {
            if (line[0] != 'G') continue;
            char* p = line + 1;
            GradeKey key;
            int32_t setting = strtol(p, &p, 10);
            key.time = strtol(p, &p, 10);
            key.lut = strtol(p, &p, 10);
            if (key.lut <= 0) continue;
            s_grading[setting].push_back(key);
            count++;
        }
        fclose(file);
        return count;
    }

    void Load() {
        s_triedLoad = true;
        FILE* file = OpenData("hd_forever_light.txt");
        if (!file) {
            SetInfo("ERROR: forever_data\\hd_forever_light.txt not found next to WowClient64.exe");
            return;
        }
        int32_t badLines = LoadBands(file);
        FILE* ambientFile = OpenData("hd_forever_ambient.txt"); // HD: horizon and ground ambient (bands 18, 19)
        bool haveAmbient = ambientFile != nullptr;
        if (haveAmbient) badLines += LoadBands(ambientFile);

        FILE* skyFile = OpenData("hd_forever_sky.txt"); // HD: hdSky, cloud base colour (band 12) from Forever's CloudEmissiveColor
        bool haveSky = skyFile != nullptr;
        if (haveSky) badLines += LoadBands(skyFile);

        int32_t fogLayers = LoadFog(); // HD: Forever fog for hdFog
        int32_t gradeKeys = LoadGrading(); // HD: Forever colour grading for hdGrade

        char info[200]; // HD: was 160 (hdSky note added)
        snprintf(info, sizeof(info), "loaded %d lights, %d Forever settings, %d fog layers, %d grading keys%s%s%s", static_cast<int>(s_lights.size()), static_cast<int>(s_settings.size()), fogLayers, gradeKeys, haveAmbient ? "" : ", no hd_forever_ambient.txt", haveSky ? "" : ", no hd_forever_sky.txt", badLines ? " (some lines skipped)" : ""); // HD: hdSky
        SetInfo(info);
    }

    // Same search as the client's GetBandTimeRange (Lights.cpp), with up to 32 keys.
    void BandTimeRange(const int32_t* times, int32_t count, float time, int32_t& index, float& t, int32_t& next) {
    HD::DayNightMath::Range(times,count,time,index,t,next,HD::DayNightSmoothingEnabled());
}

    // A band's colour at this time (0..1), blended between its two time keys like ForeverIntBand. False when empty.
    bool BandColorAt(const Band& b, float time, float* out) {
        if (!b.count) return false;
        int32_t index;
        float t;
        int32_t next;
        BandTimeRange(b.time, b.count, time, index, t, next);
        float from[3];
        float to[3];
        SetColor(from, b.color[index]);
        SetColor(to, b.color[next]);
        for (int32_t c = 0; c < 3; c++) out[c] = from[c] + (to[c] - from[c]) * t;
        return true;
    }

    // HD: hdForeverForce, a Forever setting to use for every light (0 = none). For zones Forever has no light
    // for in our data (Eversong, Ghostlands): try a setting by hand and compare with a Forever capture.
    int32_t ForcedSetting() {
        return s_force && s_force->m_intValue > 0 ? s_force->m_intValue : 0;
    }

    // The Forever setting for this light and slot (0 = none). A slot with none falls back to slot 0 when asked.
    int32_t SettingFor(const LightRec* light, int32_t slot, bool fallbackToSlot0) {
        int32_t forced = ForcedSetting();
        if (forced) return forced;
        auto found = s_lights.find(light->m_id);
        if (found == s_lights.end() || found->second.map != light->m_continentID) return 0;
        int32_t foreverId = found->second.slots[slot];
        if (!foreverId && fallbackToSlot0 && slot != 0) foreverId = found->second.slots[0]; // e.g. no storm setting: clear weather
        return foreverId;
    }

    const HdRecord* FindRecord(const LightParamsRec* params) {
        if (!params || params->m_id < kMarker) return nullptr;
        uint32_t index = static_cast<uint32_t>(params->m_id - kMarker);
        return index < s_records.size() ? s_records[index] : nullptr;
    }
}

const LightParamsRec* HD::ForeverLightParams(const LightRec* light, int32_t slot, const LightParamsRec* original) {
    RegisterCVars();
    if (!s_enabled || !s_enabled->m_intValue || !light || !original || slot < 0 || slot > 7) return original;
    if (!s_triedLoad) Load();

    int32_t foreverId = SettingFor(light, slot, false);
    if (!foreverId) return original;
    auto setting = s_settings.find(foreverId);
    if (setting == s_settings.end()) return original;

    uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(original->m_id)) << 32) | static_cast<uint32_t>(foreverId);
    HdRecord*& record = s_recordByPair[key];
    if (!record) {
        record = new HdRecord();
        memcpy(&record->rec, original, sizeof(LightParamsRec)); // skybox, glow, water alpha stay 3.3.5
        record->realId = original->m_id;
        record->setting = setting->second;
        record->rec.m_id = kMarker + static_cast<int32_t>(s_records.size());
        s_records.push_back(record);
    }
    return &record->rec;
}

bool HD::ForeverHasLight(const LightRec* light) {
    if (!s_triedLoad) Load();
    if (!light) return false;
    const auto found = s_lights.find(light->m_id);
    return found != s_lights.end() && found->second.map == light->m_continentID;
}

int32_t HD::RealLightParamsId(const LightParamsRec* params) {
    const HdRecord* record = FindRecord(params);
    return record ? record->realId : params->m_id;
}

bool HD::ForeverIntBand(const LightParamsRec* params, int32_t band, float time, CImVector* color) {
    const HdRecord* record = FindRecord(params);
    if (!record || band < 0 || band >= kBands) return false;
    const Band& b = record->setting->bands[band];
    if (!b.count) return false;

    int32_t index;
    float t;
    int32_t next;
    BandTimeRange(b.time, b.count, time, index, t, next);

    uint32_t from = b.color[index];
    uint32_t to = b.color[next];
    int32_t fr = (from >> 16) & 0xFF, fg = (from >> 8) & 0xFF, fb = from & 0xFF;
    int32_t tr = (to >> 16) & 0xFF, tg = (to >> 8) & 0xFF, tb = to & 0xFF;
    int32_t r = CMath::fint_((tr - fr) * t + fr);
    int32_t g = CMath::fint_((tg - fg) * t + fg);
    int32_t bl = CMath::fint_((tb - fb) * t + fb);
    *color = CImVector(static_cast<uint8_t>(bl & 0xFF), static_cast<uint8_t>(g & 0xFF), static_cast<uint8_t>(r & 0xFF), 0xFF);
    return true;
}

float HD::ForeverBandWeight(int32_t band) {
    if (band != 0 && band != 1) return 1.0f;
    RegisterCVars();
    float weight = s_light ? s_light->m_floatValue : 0.0f;
    return weight < 0.0f ? 0.0f : (weight > 1.0f ? 1.0f : weight);
}

bool HD::ForeverAmbientsAt(const LightRec* light, int32_t slot, float time, float* sky, float* horizon, float* ground) {
    RegisterCVars();
    if (!s_enabled || !s_enabled->m_intValue || !light || slot < 0 || slot > 7) return false;
    if (!s_triedLoad) Load();

    int32_t foreverId = SettingFor(light, slot, true);
    if (!foreverId) return false;
    auto setting = s_settings.find(foreverId);
    if (setting == s_settings.end() || !BandColorAt(setting->second->bands[1], time, sky)) return false;
    // A colour Forever leaves black (or has no key for) is the sky colour
    if (!BandColorAt(setting->second->bands[18], time, horizon) || horizon[0] + horizon[1] + horizon[2] <= 0.0f) {
        for (int32_t c = 0; c < 3; c++) horizon[c] = sky[c];
    }
    if (!BandColorAt(setting->second->bands[19], time, ground) || ground[0] + ground[1] + ground[2] <= 0.0f) {
        for (int32_t c = 0; c < 3; c++) ground[c] = sky[c];
    }
    return true;
}

bool HD::ForeverFogAt(const LightRec* light, int32_t slot, float time, ForeverFog* out) {
    RegisterCVars();
    if (!s_triedLoad) Load();
    if (!light || slot < 0 || slot > 7) return false;

    int32_t foreverId = SettingFor(light, slot, true);
    if (!foreverId) return false;
    auto fog = s_fog.find(foreverId);
    if (fog == s_fog.end() || fog->second.empty()) return false;

    const std::vector<FogKey>& keys = fog->second;
    int32_t times[kMaxKeys];
    int32_t count = static_cast<int32_t>(keys.size() < kMaxKeys ? keys.size() : kMaxKeys);
    for (int32_t i = 0; i < count; i++) times[i] = keys[i].time;

    int32_t index;
    float t;
    int32_t next;
    BandTimeRange(times, count, time, index, t, next);
    const FogKey& a = keys[index];
    const FogKey& b = keys[next];

    out->setting = foreverId;
    out->count = a.count > b.count ? a.count : b.count;
    for (int32_t i = 0; i < 3; i++) {
        out->layers[i] = a.layers[i];
        LerpFogLayer(out->layers[i], b.layers[i], t);
    }
    return true;
}

void HD::LerpFogLayer(ForeverFogLayer& a, const ForeverFogLayer& b, float t) {
    if (!a.flags && !b.flags) return;
    if (!b.flags) {
        // HD: the layer fades out: keep its look, lose its density
        a.density *= 1.0f - t;
        return;
    }
    if (!a.flags) {
        float density = b.density * t;
        a = b;
        a.density = density;
        return;
    }
    // HD: flags remain discrete metadata, but shader-visible shadow and height features blend continuously.
    const float sunA=FogShadowWeight(a),sunB=FogShadowWeight(b);
    const float relA=FogRelativeHeightWeight(a),relB=FogRelativeHeightWeight(b);
    a.shadowWeight=sunA+(sunB-sunA)*t;
    a.relativeHeightWeight=relA+(relB-relA)*t;
    a.featureWeightsValid=true;
    if (t >= 0.5f) a.flags = b.flags;
    for (int32_t c = 0; c < 3; c++) {
        a.diffuse[c] += (b.diffuse[c] - a.diffuse[c]) * t;
        a.emissive[c] += (b.emissive[c] - a.emissive[c]) * t;
        a.shadow[c] += (b.shadow[c] - a.shadow[c]) * t;
    }
    a.start += (b.start - a.start) * t;
    a.density += (b.density - a.density) * t;
    a.shadowMul += (b.shadowMul - a.shadowMul) * t;
    a.topRate += (b.topRate - a.topRate) * t;
    a.topHeight += (b.topHeight - a.topHeight) * t;
    a.bottomRate += (b.bottomRate - a.bottomRate) * t;
    a.bottomHeight += (b.bottomHeight - a.bottomHeight) * t;
    a.intensity += (b.intensity - a.intensity) * t;
    a.g += (b.g - a.g) * t;
    a.strength += (b.strength - a.strength) * t;
    a.exponent += (b.exponent - a.exponent) * t;
}

bool HD::ForeverGradingAt(const LightRec* light, int32_t slot, float time, int32_t* lutA, int32_t* lutB, float* t) {
    RegisterCVars();
    if (!s_triedLoad) Load();
    if (!light || slot < 0 || slot > 7) return false;

    int32_t foreverId = SettingFor(light, slot, true);
    if (!foreverId) return false;
    auto grading = s_grading.find(foreverId);
    if (grading == s_grading.end() || grading->second.empty()) return false;

    const std::vector<GradeKey>& keys = grading->second;
    int32_t times[kMaxKeys];
    int32_t count = static_cast<int32_t>(keys.size() < kMaxKeys ? keys.size() : kMaxKeys);
    for (int32_t i = 0; i < count; i++) times[i] = keys[i].time;

    int32_t index;
    int32_t next;
    BandTimeRange(times, count, time, index, *t, next);
    *lutA = keys[index].lut;
    *lutB = keys[next].lut;
    return true;
}
