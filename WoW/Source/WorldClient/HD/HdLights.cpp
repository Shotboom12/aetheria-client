#include "Engine/Source/Gx/CGxDeviceVulkan/hd/lamp/HdLampHooks.h"
#include "WoW/Source/WorldClient/HD/HdLights.h"
#include "Engine/Source/Model2/HdExtraLamps.h"
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CAaSphere.h"

// Pixel constants of one light (4 float4, what HdPointLight in the terrain pixel shader reads):
//   0: xyz = position - camera (world, yards), w = 1 legacy falloff colour * N.L / (0.7 d + 0.03 d^2)
//   1: rgb = colour A, w = attenuation start            (windowed falloff, lightbufferomni permutation 0:
//   2: rgb = colour B, w = 1 / (attenuation end - start)  (1 - saturate((d - start) / (end - start)))^2 * N.L,
//   3: x, y = colour blend t = saturate(d * x + y)         colour = lerp(B, A, smoothstep(t)); not used by M2 lights)
//      z = radius where the light ends, w = 1 / fade width (our cut: Forever's legacy falloff never reaches 0, its
//      light volume ends it; its size is not known yet)

namespace {
    struct FrameLight {
        uint64_t owner=0;
        C3Vector pos;      // world
        C3Vector color;    // CM2Light::m_dirColor (diffuse colour * intensity * model alpha), as 3.3.5 uses it
        float strength;    // brightest channel
        float radius;      // where it ends (hdLightsRadius, or from its colour)
        float fade;        // fade width at the end
        uint8_t known;     // 1 = from s_known (its model was not animated this frame)
    };

    const float kGatherDistance = 300.0f; // lights farther than this from the camera are left out
    const float kEndLight = 0.02f;        // automatic radius: where colour / (0.7 d + 0.03 d^2) falls to this
    const float kMinRadius = 2.0f;
    const float kMaxRadius = 32.0f;
    const uint32_t kNoFrame = 0xFFFFFFFFu;

    CVar* s_mode = nullptr;
    CVar* s_radius = nullptr;
    CVar* s_info = nullptr;
    CVar* s_log = nullptr;

    // HD: lights the per-pixel path cannot use (non-finite position or colour, negative colour channel). One such
    // light picked by a draw turns its whole buffer into NaN or below zero: the chunk went black in the test view
    // (pow of a negative) and lost every lamp pool in the normal view, whatever the other lights.
    int32_t s_rejected = 0;
    int32_t s_clamped = 0;
    FILE* s_logFile = nullptr;
    int32_t s_logDumps = 0;
    ULONGLONG s_logTime = 0;
    bool s_logFrame = false;    // this frame's lights were written: also write the draws near the camera

    std::vector<FrameLight> s_frame;       // this frame's lights
    // HD: the last animated state of each model light. A lamp's model is animated only while it is drawn, and
    // SelectLights hides (unlinks) a light its model did not animate this frame: a lamp just off screen stopped
    // lighting the ground in view, so the ground's colour changed with the camera angle. Kept until the model
    // leaves the scene (HD::LightsForget).
    struct KnownLight {
        C3Vector pos;
        C3Vector color;
        uint32_t frame;    // the frame it was last seen in the grid
    };
    std::unordered_map<const CM2Light*, KnownLight> s_known;
    const CM2Scene* s_knownScene = nullptr;
    const CM2Scene* s_frameScene = nullptr;
    uint32_t s_frameStamp = kNoFrame;
    int32_t s_otherFalloff = 0;            // this frame's lights whose attenuation is not CM2Light's 0 / 0.7 / 0.03
    int32_t s_mostPerDraw = 0;             // most lights one draw took since the info was last shown
    ULONGLONG s_infoTime = 0;

    void RegisterCVars() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        s_mode = CVar::Register("hdLights", "HD: 1 = lamps and fires light the ground per pixel (WoW Forever), 2 = light path with no light (must equal 0), 3 = test view (lights only), 0 = 3.3.5 per-vertex lights", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_radius = CVar::Register("hdLightsRadius", "HD: yards where a lamp's light ends, 0 = from its colour", 1, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_info = CVar::Register("hdLightsInfo", "HD: hdLights status (read only)", 0, "no light yet", nullptr, CATEGORY_GRAPHICS, false, nullptr, false); if (s_info) s_info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
        s_log = CVar::Register("hdLightsLog", "HD: 1 = write this frame's lights and the ground draws near the camera to Logs\\HdLights.log (once a second, 20 times, then back to 0)", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        HD::ExtraLamps::RegisterCVars(); // HD: main thread (models may load before the console exists)
    }

    bool Finite(const C3Vector& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    void LogLine(const char* text) {
        if (!s_logFile) {
            CreateDirectoryA("Logs", nullptr);
            s_logFile = fopen("Logs\\HdLights.log", "w");
            if (!s_logFile) return;
        }
        fputs(text, s_logFile);
        fputc('\n', s_logFile);
        fflush(s_logFile);
    }

    float Radius(float strength) {
        float fixed = s_radius ? s_radius->m_floatValue : 0.0f;
        if (fixed > 0.0f) return fixed;
        // 0.03 r^2 + 0.7 r = strength / kEndLight
        float r = (sqrtf(0.49f + 0.12f * strength / kEndLight) - 0.7f) / 0.06f;
        return r < kMinRadius ? kMinRadius : (r > kMaxRadius ? kMaxRadius : r);
    }

    void AddFrameLight(const C3Vector& pos, const C3Vector& colour, const C3Vector& camera, const CM2Light* owner, uint8_t known) {
        char text[300];
        if (!Finite(pos) || !Finite(colour)) {
            s_rejected++;
            if (s_logFrame) {
                snprintf(text, sizeof(text), "  REJECTED light %p known %d pos (%g, %g, %g) colour (%g, %g, %g)", static_cast<const void*>(owner), known, pos.x, pos.y, pos.z, colour.x, colour.y, colour.z);
                LogLine(text);
            }
            return;
        }
        C3Vector c = colour;
        if (c.x < 0.0f || c.y < 0.0f || c.z < 0.0f) {
            s_clamped++;
            if (s_logFrame) {
                snprintf(text, sizeof(text), "  NEGATIVE light %p known %d pos (%.2f, %.2f, %.2f) colour (%.3f, %.3f, %.3f)", static_cast<const void*>(owner), known, pos.x, pos.y, pos.z, colour.x, colour.y, colour.z);
                LogLine(text);
            }
            if (c.x < 0.0f) c.x = 0.0f;
            if (c.y < 0.0f) c.y = 0.0f;
            if (c.z < 0.0f) c.z = 0.0f;
        }
        float dx = pos.x - camera.x;
        float dy = pos.y - camera.y;
        float dz = pos.z - camera.z;
        if (dx * dx + dy * dy + dz * dz > kGatherDistance * kGatherDistance) return;
        float strength = c.x > c.y ? c.x : c.y;
        if (c.z > strength) strength = c.z;
        if (!(strength > 0.0f)) return;
        FrameLight frameLight;
        frameLight.owner=reinterpret_cast<uintptr_t>(owner);
        frameLight.pos = pos;
        frameLight.color = c;
        frameLight.strength = strength;
        frameLight.radius = Radius(strength);
        frameLight.fade = frameLight.radius * 0.25f;
        frameLight.known = known;
        s_frame.push_back(frameLight);
        if (s_logFrame && dx * dx + dy * dy + dz * dz < 80.0f * 80.0f) {
            snprintf(text, sizeof(text), "  light #%u %p known %d pos (%.2f, %.2f, %.2f) colour (%.3f, %.3f, %.3f) radius %.1f", static_cast<uint32_t>(s_frame.size() - 1), static_cast<const void*>(owner), known, pos.x, pos.y, pos.z, c.x, c.y, c.z, frameLight.radius);
            LogLine(text);
        }
    }

    // The lights CM2Scene::SelectLights would give anything this frame: point lights in the light grid, visible,
    // and animated this frame (or not owned by a model). Unlike SelectLights it never hides a stale light.
    // HD: plus the lamps whose model was not animated this frame, at their last animated state (s_known).
    void GatherFrame(const CM2Scene* scene, const C3Vector& camera) {
        s_frame.clear();
        s_otherFalloff = 0;
        s_rejected = 0;
        s_clamped = 0;
        s_logFrame = false;
        if (s_log && s_log->m_intValue == 1) {
            ULONGLONG now = GetTickCount64();
            if (!s_logTime || now - s_logTime >= 1000) {
                s_logTime = now;
                s_logFrame = true;
                char text[200];
                snprintf(text, sizeof(text), "frame %u time %llu camera (%.2f, %.2f, %.2f) hdLights %d radius cvar %.1f", scene->m_frameStamp, static_cast<unsigned long long>(now), camera.x, camera.y, camera.z, HD::LightsMode(), s_radius ? s_radius->m_floatValue : 0.0f);
                LogLine(text);
                if (++s_logDumps >= 20) { s_log->Set("0", true, false, false, false); s_logDumps = 0; }
            }
        }
        if (scene != s_knownScene) {
            s_known.clear();
            s_knownScene = scene;
        }
        uint32_t frame = scene->m_frameStamp;
        if (scene->m_lightGrid) {
            for (uint32_t cell = 0; cell < 64 * 64; cell++) {
                for (const CM2Light* light = scene->m_lightGrid[cell]; light; light = light->m_lightNext) {
                    if (light->m_scene && light->m_stamp != frame) continue; // stale: from s_known below
                    if (!light->m_visible || light->m_type != M2LIGHT_POINT) continue;
                    if (light->m_scene) {
                        KnownLight& known = s_known[light];
                        known.pos = light->m_pos;
                        known.color = light->m_dirColor;
                        known.frame = frame;
                    }
                    if (light->m_constantAttenuation != 0.0f || light->m_linearAttenuation != 0.7f || light->m_quadraticAttenuation != 0.03f) s_otherFalloff++;
                    AddFrameLight(light->m_pos, light->m_dirColor, camera, light, 0);
                }
            }
        }
        for (std::unordered_map<const CM2Light*, KnownLight>::iterator it = s_known.begin(); it != s_known.end();) {
            if (it->second.frame == frame) { ++it; continue; }
            // animated this frame but not in the grid: its model hid it (switched off)
            if (it->first->m_stamp == frame) { it = s_known.erase(it); continue; }
            AddFrameLight(it->second.pos, it->second.color, camera, it->first, 1);
            ++it;
        }
    }

    // HD: lamps a step apart (a lantern on its post, a cluster of event lanterns) become one light: colour summed at
    // their colour-weighted centre. A draw takes only kLightsMax lights, and a cluster used to fill every slot of
    // the ground around it, leaving the rest of that ground black.
    const float kMergeDistance = 2.5f;
    int32_t s_merged = 0;
    void MergeClose() {
        s_merged = 0;
        for (size_t i = 0; i < s_frame.size(); i++) {
            FrameLight& a = s_frame[i];
            for (size_t j = i + 1; j < s_frame.size();) {
                const FrameLight& b = s_frame[j];
                float dx = a.pos.x - b.pos.x, dy = a.pos.y - b.pos.y, dz = a.pos.z - b.pos.z;
                if (dx * dx + dy * dy + dz * dz >= kMergeDistance * kMergeDistance) { j++; continue; }
                float wa = a.strength, wb = b.strength, w = wa + wb;
                a.pos = C3Vector((a.pos.x * wa + b.pos.x * wb) / w, (a.pos.y * wa + b.pos.y * wb) / w, (a.pos.z * wa + b.pos.z * wb) / w);
                a.color = C3Vector(a.color.x + b.color.x, a.color.y + b.color.y, a.color.z + b.color.z);
                a.strength = a.color.x > a.color.y ? a.color.x : a.color.y;
                if (a.color.z > a.strength) a.strength = a.color.z;
                a.radius = Radius(a.strength);
                a.fade = a.radius * 0.25f;
                s_frame[j] = s_frame.back();
                s_frame.pop_back();
                s_merged++;
            }
        }
    }

    void ShowInfo(const C3Vector& camera) {
        ULONGLONG now = GetTickCount64();
        if (!s_info || (s_infoTime && now - s_infoTime < 500)) return;
        s_infoTime = now;
        char text[300];
        int32_t nearest = -1;
        float best = 0.0f;
        for (size_t i = 0; i < s_frame.size(); i++) {
            float dx = s_frame[i].pos.x - camera.x;
            float dy = s_frame[i].pos.y - camera.y;
            float dz = s_frame[i].pos.z - camera.z;
            float d = dx * dx + dy * dy + dz * dz;
            if (nearest < 0 || d < best) { nearest = static_cast<int32_t>(i); best = d; }
        }
        int32_t n = snprintf(text, sizeof(text), "hdLights %d: %u lights, up to %d per draw, %d other falloff, %d invalid, %d negative, %d merged", HD::LightsMode(), static_cast<uint32_t>(s_frame.size()), s_mostPerDraw, s_otherFalloff, s_rejected, s_clamped, s_merged);
        if (nearest >= 0 && n > 0 && n < static_cast<int32_t>(sizeof(text))) {
            const FrameLight& l = s_frame[nearest];
            snprintf(text + n, sizeof(text) - n, "; nearest %.1f yd at (%.2f, %.2f, %.2f) colour (%.3f, %.3f, %.3f) radius %.1f", sqrtf(best), l.pos.x, l.pos.y, l.pos.z, l.color.x, l.color.y, l.color.z, l.radius);
        }
        s_info->Set(text, true, false, false, false);
        s_mostPerDraw = 0;
    }
}

int32_t HD::LightsMode() {
    RegisterCVars();
    int32_t mode = s_mode ? s_mode->m_intValue : 1;
    return mode < 0 || mode > 3 ? 0 : mode;
}

int32_t HD::LightsForSphere(CM2Scene* scene, const C3Vector& camera, const CAaSphere& sphere, float* out) {
    return HD::LightsForGround(scene, camera, sphere, nullptr, 0, out);
}

int32_t HD::LightsForGround(CM2Scene* scene, const C3Vector& camera, const CAaSphere& sphere, const C3Vector* ground, int32_t groundCount, float* out) {
    RegisterCVars();
#ifdef AETHERIA_HD_LAMP
    HD::Lamp::Native::SnapshotPixelDraw(0);
#endif
    if (!scene) return 0;
    if (scene != s_frameScene || scene->m_frameStamp != s_frameStamp) {
        s_frameScene = scene;
        s_frameStamp = scene->m_frameStamp;
        GatherFrame(scene, camera);
        MergeClose();
        ShowInfo(camera);
    }

    int32_t picked[kLightsMax];
    float score[kLightsMax];
    int32_t count = 0;
    if (ground && groundCount > 0) {
        // HD: the ground's lights, one at a time: each time the light that adds the most where the ground is still
        // dark (square root of the light at each point: a point already lit gains little). The most light overall
        // went to one corner's cluster of lamps and left the rest of the chunk black.
        const int32_t kMaxGround = 64, kMaxCandidates = 48;
        if (groundCount > kMaxGround) groundCount = kMaxGround;
        static float gain[kMaxCandidates][kMaxGround];
        int32_t candidate[kMaxCandidates];
        int32_t candidates = 0;
        for (size_t i = 0; i < s_frame.size() && candidates < kMaxCandidates; i++) {
            const FrameLight& l = s_frame[i];
            float dx = l.pos.x - sphere.c.x, dy = l.pos.y - sphere.c.y, dz = l.pos.z - sphere.c.z;
            float reach = l.radius + sphere.r;
            if (dx * dx + dy * dy + dz * dz >= reach * reach) continue;
            float any = 0.0f;
            for (int32_t p = 0; p < groundCount; p++) {
                float gx = l.pos.x - ground[p].x, gy = l.pos.y - ground[p].y, gz = l.pos.z - ground[p].z;
                float d = sqrtf(gx * gx + gy * gy + gz * gz);
                float v = 0.0f;
                if (d < l.radius) {
                    float cut = (l.radius - d) / l.fade;
                    if (cut > 1.0f) cut = 1.0f;
                    if (d < 1.0f) d = 1.0f;
                    v = l.strength * cut / (0.7f * d + 0.03f * d * d);
                }
                gain[candidates][p] = v;
                any += v;
            }
            if (any > 0.0f) candidate[candidates++] = static_cast<int32_t>(i);
        }
        float lit[kMaxGround];
        for (int32_t p = 0; p < groundCount; p++) lit[p] = 0.0f;
        bool used[kMaxCandidates] = {};
        while (count < kLightsMax) {
            int32_t best = -1;
            float bestGain = 0.0f;
            for (int32_t k = 0; k < candidates; k++) {
                if (used[k]) continue;
                float g = 0.0f;
                for (int32_t p = 0; p < groundCount; p++) {
                    if (gain[k][p] > 0.0f) g += sqrtf(lit[p] + gain[k][p]) - sqrtf(lit[p]);
                }
                if (g > bestGain) { bestGain = g; best = k; }
            }
            if (best < 0) break;
            used[best] = true;
            for (int32_t p = 0; p < groundCount; p++) lit[p] += gain[best][p];
            score[count] = bestGain;
            picked[count++] = candidate[best];
        }
    }
    // the strongest lights at the sphere's nearest point (brightest first)
    for (size_t i = 0; i < s_frame.size() && !(ground && groundCount > 0); i++) {
        const FrameLight& l = s_frame[i];
        float dx = l.pos.x - sphere.c.x;
        float dy = l.pos.y - sphere.c.y;
        float dz = l.pos.z - sphere.c.z;
        float reach = l.radius + sphere.r;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq >= reach * reach) continue;
        float d = sqrtf(distSq) - sphere.r;
        if (d < 1.0f) d = 1.0f;
        float s = l.strength / (0.7f * d + 0.03f * d * d);
        if (count == kLightsMax && s <= score[kLightsMax - 1]) continue;
        int32_t j = count < kLightsMax ? count++ : kLightsMax - 1;
        while (j > 0 && score[j - 1] < s) { score[j] = score[j - 1]; picked[j] = picked[j - 1]; j--; }
        score[j] = s;
        picked[j] = static_cast<int32_t>(i);
    }

    uint32_t lampSlots=0;
    for (int32_t k = 0; k < count; k++) {
        const FrameLight& l = s_frame[picked[k]];
#ifdef AETHERIA_HD_LAMP
        if(HD::Lamp::Native::IsChosenOwner(l.owner,l.pos.x,l.pos.y,l.pos.z))lampSlots|=1u<<k;
#endif
        float* c = out + 16 * k;
        c[0] = l.pos.x - camera.x;  c[1] = l.pos.y - camera.y;  c[2] = l.pos.z - camera.z;  c[3] = 1.0f;
        c[4] = l.color.x;           c[5] = l.color.y;           c[6] = l.color.z;           c[7] = 0.0f;
        c[8] = l.color.x;           c[9] = l.color.y;           c[10] = l.color.z;          c[11] = 0.0f;
        c[12] = 0.0f;               c[13] = 1.0f;               c[14] = l.radius;           c[15] = 1.0f / l.fade;
    }
    if (count > s_mostPerDraw) s_mostPerDraw = count;
    if (s_logFrame) {
        float cx = sphere.c.x - camera.x, cy = sphere.c.y - camera.y;
        if (cx * cx + cy * cy < 60.0f * 60.0f) {
            char text[300];
            int32_t n = snprintf(text, sizeof(text), "  draw sphere (%.2f, %.2f, %.2f) r %.1f ground %d: %d light(s)", sphere.c.x, sphere.c.y, sphere.c.z, sphere.r, groundCount, count);
            for (int32_t k = 0; k < count && n > 0 && n < static_cast<int32_t>(sizeof(text)); k++)
                n += snprintf(text + n, sizeof(text) - n, " #%d", picked[k]);
            LogLine(text);
        }
    }
#ifdef AETHERIA_HD_LAMP
    HD::Lamp::Native::SnapshotPixelDraw(lampSlots);
#endif
    return count;
}

void HD::LightsForget(const CM2Light* light) {
    s_known.erase(light);
}


bool HD::ReadCachedLights(const CM2Scene* scene,uint32_t expectedStamp,
                         CachedLightVisitor visit,void* user,CachedLightsInfo& info) {
    info={};
    if(!scene||expectedStamp==kNoFrame||scene!=s_frameScene||
       scene->m_frameStamp!=expectedStamp||s_frameStamp!=expectedStamp)return false;
    info.sceneFrameStamp=expectedStamp;info.total=static_cast<uint32_t>(s_frame.size());
    info.nativeMode=s_mode?s_mode->m_intValue:0;
    if(!visit)return true; // availability/count only; caller must still audit coverage
    for(const auto& source:s_frame){
        CachedLight light;light.owner=source.owner;
        light.position[0]=source.pos.x;light.position[1]=source.pos.y;light.position[2]=source.pos.z;
        light.colourTimesIntensity[0]=source.color.x;light.colourTimesIntensity[1]=source.color.y;light.colourTimesIntensity[2]=source.color.z;
        light.radius=source.radius;light.fadeWidth=source.fade;light.lastAnimatedState=source.known;
        if(!visit(user,light))return false; // never call a truncated snapshot complete
        ++info.visited;
    }
    return true;
}
