#include "Engine/Source/Model2/HdExtraLamps.h"
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <unordered_set>
#include "Engine/Source/Base/SIMD.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Model2/M2Data.h"

namespace {
struct Lamp {
    const char* path; // lower case, backslashes, no extension
    float x, y, z;    // model space (yards), like M2Light::m_position
    uint32_t bone;
    float r, g, b;
};

const Lamp s_lamps[] = {
#include "Engine/Source/Model2/HdExtraLampsTable.inc"
};

std::atomic<CVar*> s_enabled(nullptr);
std::atomic<CVar*> s_intensity(nullptr);
std::atomic<CVar*> s_range(nullptr);
std::atomic<CVar*> s_objects(nullptr);
std::atomic<CVar*> s_day(nullptr);
std::atomic<float> s_night(1.0f);

std::mutex s_lock;
std::unordered_set<const void*> s_patched; // CM2Shared
std::unordered_set<const void*> s_object;  // CM2Model of world objects
std::unordered_set<const void*> s_interior; // HD J1: CM2Model of doodads in interior building groups
std::atomic<CVar*> s_lampsDay(nullptr);      // HD J1: hdLampsDay

CVar* Var(const char* name, const char* help, const char* value) {
    if (CVar* old = CVar::LookupRegistered(name)) return old;
    return CVar::Register(name, help, 1, value, nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

const Lamp* Find(const char* fileName) {
    char name[260];
    size_t n = 0;
    for (; fileName[n] && n < sizeof name - 1; n++) {
        char c = fileName[n] == '/' ? '\\' : fileName[n];
        name[n] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    name[n] = 0;
    if (char* dot = std::strrchr(name, '.')) *dot = 0;
    for (const Lamp& lamp : s_lamps) {
        if (!std::strcmp(lamp.path, name)) return &lamp;
    }
    return nullptr;
}

uint32_t Align(uint32_t v, uint32_t a) { return (v + a - 1) & ~(a - 1); }
}

void HD::ExtraLamps::RegisterCVars() {
    if (s_enabled.load()) return;
    s_intensity.store(Var("hdExtraLampsIntensity", "HD: brightness of the added lamp lights (models loaded after the change)", "1.4"));
    s_objects.store(Var("hdExtraLampsObjects", "HD: 1 = the added lamp lights also on world objects (event decorations, game objects), 0 = only on map decor", "0"));
    s_day.store(Var("hdExtraLampsDay", "HD: how much of the added lamp lights stays on by day, 0 = off by day (lit at night like the windows), 1 = always on", "0"));
    s_lampsDay.store(Var("hdLampsDay", "HD: how much of the lamp and fire lights of outdoor map decor stays on by day, 0 = off by day (lit at night), 1 = Blizzard (always on); interiors always on", "0")); // HD J1
    s_range.store(Var("hdExtraLampsRange", "HD: yards where an added lamp light ends (models loaded after the change)", "7"));
    s_enabled.store(Var("hdExtraLamps", "HD: 1 = add a point light to Blizzard lamp posts and lanterns that have none (models loaded after the change), 0 = Blizzard data", "1"));
}

bool HD::ExtraLamps::Patch(M2Data*& data, uint32_t& size, const char* fileName) {
    CVar* enabled = s_enabled.load();
    if ((enabled && enabled->m_intValue != 1) || !data || !fileName) return false;
    if (size < sizeof(M2Data) || data->m_magic != 0x3032444D || data->m_version != 0x108) return false;
    if (data->m_lights.m_count || !data->m_bones.m_count || !data->m_sequences.m_count) return false;

    const Lamp* lamp = Find(fileName);
    if (!lamp) return false;

    uint32_t sequences = data->m_sequences.m_count;
    if (data->m_sequences.m_offset > size || sequences > (size - data->m_sequences.m_offset) / sizeof(M2Sequence)) return false;
    const M2Sequence* seq = reinterpret_cast<const M2Sequence*>(reinterpret_cast<const uint8_t*>(data) + data->m_sequences.m_offset);

    CVar* intensityVar = s_intensity.load();
    CVar* rangeVar = s_range.load();
    float intensity = intensityVar ? intensityVar->m_floatValue : 1.4f;
    float range = rangeVar ? rangeVar->m_floatValue : 7.0f;
    if (!(intensity >= 0.0f) || intensity > 8.0f) intensity = 1.4f;
    if (!(range > 0.5f) || range > 32.0f) range = 7.0f;

    // Tail layout (file offsets, relocated by M2Init like the rest of the file):
    // M2Light | 7 x (times outer[sequences], keys outer[sequences]) | time 0 | the 7 key values.
    const uint32_t tracks = 7;
    uint32_t lightOffset = Align(size, 16);
    uint32_t outerOffset = lightOffset + static_cast<uint32_t>(sizeof(M2Light));
    uint32_t outerBytes = sequences * static_cast<uint32_t>(sizeof(M2Array<uint32_t>));
    uint32_t timeOffset = outerOffset + tracks * 2 * outerBytes;
    uint32_t valueOffset = timeOffset + 4;
    uint32_t total = Align(valueOffset + 4 * 12 + 4, 16);

    M2Data* patched = static_cast<M2Data*>(SIMD::AllocA16(total, __FILE__, __LINE__));
    if (!patched) return false;
    std::memset(patched, 0, total);
    std::memcpy(patched, data, size);
    uint8_t* base = reinterpret_cast<uint8_t*>(patched);

    // Key values: ambient colour (1,1,1) at intensity 0, as Blizzard's lamps; diffuse colour * intensity; range; visible.
    uint32_t v = valueOffset;
    auto put = [&](const void* src, uint32_t bytes) { uint32_t at = v; std::memcpy(base + at, src, bytes); v += bytes; return at; };
    float white[3] = {1.0f, 1.0f, 1.0f}, zero = 0.0f, colour[3] = {lamp->r, lamp->g, lamp->b}, start = range * 0.3f;
    uint8_t visible = 1;
    uint32_t values[tracks] = {put(white, 12), put(&zero, 4), put(colour, 12), put(&intensity, 4), put(&start, 4), put(&range, 4), put(&visible, 1)};
    *reinterpret_cast<uint32_t*>(base + timeOffset) = 0;

    M2Light* light = reinterpret_cast<M2Light*>(base + lightOffset);
    light->m_type = 1; // point
    light->m_bone = static_cast<uint16_t>(lamp->bone < data->m_bones.m_count ? lamp->bone : 0);
    light->m_position = C3Vector(lamp->x, lamp->y, lamp->z);
    M2TrackBase* list[tracks] = {&light->m_ambientColor, &light->m_ambientIntensity, &light->m_diffuseColor, &light->m_diffuseIntensity, &light->m_attenuationStart, &light->m_attenuationEnd, &light->m_visibility};

    for (uint32_t t = 0; t < tracks; t++) {
        uint32_t timesOuter = outerOffset + t * 2 * outerBytes, keysOuter = timesOuter + outerBytes;
        M2TrackBase* track = list[t];
        track->m_trackType = 0;
        track->m_loopIndex = 0xFFFF;
        track->m_sequenceTimes.m_count = sequences;
        track->m_sequenceTimes.m_offset = timesOuter;
        // M2Track<T>::m_sequenceKeys follows m_sequenceTimes in every M2Track (M2Data.h).
        auto* keys = reinterpret_cast<M2Array<M2Array<uint8_t> >*>(reinterpret_cast<uint8_t*>(track) + 0x0C);
        keys->m_count = sequences;
        keys->m_offset = keysOuter;
        for (uint32_t s = 0; s < sequences; s++) {
            // One constant key at time 0 for every sequence stored in this file; sequences streamed from .anim
            // files get none (M2Init relocates those against the .anim buffer).
            uint32_t keyCount = (seq[s].m_flags & 0x20) ? 1 : 0;
            auto* times = reinterpret_cast<M2Array<uint32_t>*>(base + timesOuter) + s;
            auto* vals = reinterpret_cast<M2Array<uint8_t>*>(base + keysOuter) + s;
            times->m_count = keyCount;
            times->m_offset = keyCount ? timeOffset : 0;
            vals->m_count = keyCount;
            vals->m_offset = keyCount ? values[t] : 0;
        }
    }

    patched->m_lights.m_count = 1;
    patched->m_lights.m_offset = lightOffset;

    SIMD::FreeA16(data);
    data = patched;
    size = total;
    return true;
}

void HD::ExtraLamps::MarkPatched(const void* shared) {
    std::lock_guard<std::mutex> guard(s_lock);
    s_patched.insert(shared);
}

void HD::ExtraLamps::ForgetShared(const void* shared) {
    std::lock_guard<std::mutex> guard(s_lock);
    s_patched.erase(shared);
}

void HD::ExtraLamps::SetObjectModel(const void* model, bool object) {
    if (!model) return;
    std::lock_guard<std::mutex> guard(s_lock);
    if (object) s_object.insert(model);
    else s_object.erase(model);
}

bool HD::ExtraLamps::HideLight(const void* shared, const void* model, uint32_t index, uint32_t count) {
    if (!count || index != count - 1) return false; // Patch appends the last light
    CVar* objects = s_objects.load();
    if (objects && objects->m_intValue == 1) return false;
    std::lock_guard<std::mutex> guard(s_lock);
    return s_patched.count(shared) && s_object.count(model);
}

float HD::ExtraLamps::NightAtDay(float dayProgression) { // HD 2026-11-05 lot J2
    const float h = (dayProgression < 0.0f ? 0.0f : (dayProgression > 1.0f ? 1.0f : dayProgression)) * 24.0f;
    if (h < 6.0f) return 1.0f;
    if (h < 7.0f) return 7.0f - h;
    if (h < 21.5f) return 0.0f;
    if (h < 22.0f) return (h - 21.5f) / 0.5f;
    return 1.0f;
}

void HD::ExtraLamps::SetNight(float night) {
    s_night.store(night < 0.0f ? 0.0f : (night > 1.0f ? 1.0f : night));
}

float HD::ExtraLamps::LightScale(const void* shared, uint32_t index, uint32_t count, const void* model) {
    if (!count || index != count - 1) return -1.0f; // Patch appends the last light
    {
        std::lock_guard<std::mutex> guard(s_lock);
        if (!s_patched.count(shared)) return -1.0f;
        if (model && s_interior.count(model)) return 1.0f; // HD J1: an added lamp inside a building stays lit by day
    }
    CVar* dayVar = s_day.load();
    float day = dayVar ? dayVar->m_floatValue : 0.0f;
    day = day < 0.0f ? 0.0f : (day > 1.0f ? 1.0f : day);
    return day + (1.0f - day) * s_night.load();
}

void HD::ExtraLamps::SetInteriorModel(const void* model, bool interior) { // HD J1
    if (!model) return;
    std::lock_guard<std::mutex> guard(s_lock);
    if (interior) s_interior.insert(model);
    else s_interior.erase(model);
}

float HD::ExtraLamps::DoodadLightScale(const void* model) { // HD J1
    CVar* dayVar = s_lampsDay.load();
    float day = dayVar ? dayVar->m_floatValue : 0.0f;
    if (!(day < 1.0f)) return -1.0f; // 1 (or NaN) = Blizzard, untouched
    day = day < 0.0f ? 0.0f : day;
    {
        std::lock_guard<std::mutex> guard(s_lock);
        if (s_interior.count(model)) return -1.0f;
    }
    return day + (1.0f - day) * s_night.load();
}
