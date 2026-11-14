#ifndef RETAIL_JIGGLE_PHYSICS_H
#define RETAIL_JIGGLE_PHYSICS_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Tempest/C44Matrix.h"

// Soft-tissue motion for female Retail models: a damped spring per breast bone, plus one spring that drives the
// rear (butt) mesh deformation. Nothing is stored in a file. The breast bones are found from the skeleton (the
// replacer models carry physics bodies on exactly these bones, see handoffs/2026-11-12-claude-phys-decode-findings.md).
//
// Each spring is a mass tied to an anchor that follows the animated skeleton in model space:
//     q'' = -k (q - anchor) - c (q' - anchor')
// so the displacement q - anchor is what lags behind the body, which is the jiggle. Walking, running, turning and
// emotes all move the anchor through the animation; nothing here needs to know which animation is playing.
// Steps are fixed (1/120 s) so it looks the same at any frame rate.
namespace JigglePhysics {

struct Settings {
    bool enabled = true;
    // Breasts: natural frequency (Hz), damping ratio, how much the displacement is exaggerated, hard limit (model units).
    float breastHz = 3.6f;
    float breastZeta = 0.38f;
    float breastGain = 1.4f;
    float breastMax = 0.025f;
    float breastSide = 0.9f; // side-to-side travel relative to up/down and forward/back
    // 0 = the breast bone slides (translation), 1 = it turns about its own pivot, which is how Rwise's physics works:
    // a capsule hung from the chest on a ball joint (mass about 0.7, cone 60 degrees). In between blends the two.
    float breastRotate = 1.0f;
    // The two sides get slightly different speeds (left +, right -) so one trails the other by a few frames, as in
    // Rwise's video, while the main motion stays together. 0 = identical. And an extra weight on the up/down motion.
    float breastDetune = 0.06f;
    float breastUp = 1.15f;
    // How much of the character's own acceleration through the world (starting, stopping, strafing, jumping, landing)
    // reaches the springs. 0 = animation only. Turning or moving the camera adds nothing by itself.
    float worldMix = 0.15f;
    // Small swing when the character turns: the tissue lags the turn (sideways, and a little forward/back in opposite
    // directions on the two sides). Only a CHANGE of turn speed counts, so a steady turn or the camera alone adds
    // nothing. 0 = off.
    float turnMix = 1.0f;
    // Rear: same, but the result is a change of the ButtSize mesh value (slider units, 0..1 scale), not a distance.
    float buttHz = 2.6f;
    float buttZeta = 0.32f;
    float buttGain = 14.0f; // slider units per model unit of displacement
    float buttMax = 0.10f;
};

inline Settings& Config() {
    static Settings settings;
    return settings;
}

// The player's options (Interface > Aetheria Physics, saved in Config.wtf): "jigglePhysics" is the master switch and is
// OFF by default; "jiggleButt" (default on) adds the rear jiggle once the master is on. Before the CVar is registered
// everything stays off.
inline bool CVarOn(const char* name, CVar*& cached) {
    if (!cached) {
        cached = CVar::LookupRegistered(name);
    }
    return cached && cached->m_intValue != 0;
}

inline bool Active() {
    static CVar* master = nullptr;
    return Config().enabled && CVarOn("jigglePhysics", master);
}

// Interface > Aetheria Physics > Jiggle strength, 1..4 (CVar "jiggleStrength", default 2). Only the amount of travel
// changes, not the speed or the settle time: 1 Subtle, 2 Natural (the tuned feel), 3 Strong, 4 Exaggerated.
inline float StrengthScale() {
    static CVar* strength = nullptr;
    if (!strength) {
        strength = CVar::LookupRegistered("jiggleStrength");
    }
    static const float table[4] = { 0.35f, 1.0f, 1.5f, 2.0f };
    int step = strength ? static_cast<int>(strength->m_floatValue + 0.5f) : 2;
    step = std::max(1, std::min(4, step));
    return table[step - 1];
}

inline bool ButtActive() {
    static CVar* butt = nullptr;
    return CVarOn("jiggleButt", butt);
}

inline double Now() {
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

// The unit's pose in the world this frame (position in yards, facing in radians). Optional.
struct Motion {
    bool valid = false;
    float pos[3] = { 0.0f, 0.0f, 0.0f };
    float facing = 0.0f;
};

struct Spring {
    bool primed = false;
    float q[3] = { 0.0f, 0.0f, 0.0f };
    float v[3] = { 0.0f, 0.0f, 0.0f };
    float last[3] = { 0.0f, 0.0f, 0.0f };

    void Reset() {
        primed = false;
    }

    // The body's velocity changed by dv (model axes): the mass keeps its own velocity, so relative to the body it lost dv.
    void Kick(const float dv[3]) {
        if (primed) {
            for (int i = 0; i < 3; ++i) {
                v[i] -= dv[i];
            }
        }
    }

    // Advances by dt seconds; out = displacement of the mass from the anchor, in model space.
    void Step(const float anchor[3], float dt, float hz, float zeta, float out[3]) {
        out[0] = out[1] = out[2] = 0.0f;
        float jump = 0.0f;
        if (primed) {
            for (int i = 0; i < 3; ++i) {
                jump = std::max(jump, std::fabs(anchor[i] - last[i]));
            }
        }
        // First frame, a long pause, or the model snapping somewhere else: start at rest.
        if (!primed || !(dt > 0.0f) || dt > 0.25f || jump > 0.6f) {
            primed = true;
            for (int i = 0; i < 3; ++i) {
                q[i] = last[i] = anchor[i];
                v[i] = 0.0f;
            }
            return;
        }

        const float w = 6.2831853f * std::max(0.5f, hz);
        const float k = w * w;
        const float c = 2.0f * std::max(0.05f, zeta) * w;
        float va[3];
        for (int i = 0; i < 3; ++i) {
            va[i] = (anchor[i] - last[i]) / dt;
        }

        const float simDt = std::min(dt, 0.05f);
        const int steps = std::max(1, static_cast<int>(std::ceil(simDt * 120.0f)));
        const float h = simDt / static_cast<float>(steps);
        for (int n = 0; n < steps; ++n) {
            const float t = static_cast<float>(n + 1) / static_cast<float>(steps);
            for (int i = 0; i < 3; ++i) {
                const float pa = last[i] + (anchor[i] - last[i]) * t;
                const float acc = -k * (q[i] - pa) - c * (v[i] - va[i]);
                v[i] += acc * h;
                q[i] += v[i] * h;
            }
        }

        for (int i = 0; i < 3; ++i) {
            last[i] = anchor[i];
            out[i] = q[i] - anchor[i];
            // Pull a runaway back in (very large anchor motion), never leave a huge offset behind.
            if (std::fabs(out[i]) > 0.3f) {
                q[i] = anchor[i];
                v[i] = 0.0f;
                out[i] = 0.0f;
            }
        }
    }
};

// What one frame wants. breast[i] is a translation in the bone's parent frame (what the procedural transform adds).
struct Frame {
    int breastCount = 0;
    uint32_t breastBone[2] = { 0, 0 };
    float breast[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };
    float breastRot[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } }; // rotation vector (radians) about the bone pivot, bone frame
    float butt = 0.0f; // add to the ButtSize mesh value
};

struct State {
    bool searched = false;
    bool hasBreasts = false;
    uint32_t breastBone[2] = { 0, 0 };
    uint32_t breastParent[2] = { 0, 0 };
    C3Vector breastPivot[2];
    bool hasPelvis = false;
    uint32_t pelvis = 0;
    C3Vector pelvisPivot;
    Spring breastSpring[2];
    Spring buttSpring;
    double lastTime = 0.0;
    bool worldPrimed = false;
    bool hasLastVel = false;
    float lastPos[3] = { 0.0f, 0.0f, 0.0f };
    float lastWorldVel[3] = { 0.0f, 0.0f, 0.0f };
    bool hasSpine = false;
    C3Vector spinePivot;
    bool hasFacing = false;
    bool hasYawRate = false;
    float lastFacing = 0.0f;
    float lastYawRate = 0.0f;
};

constexpr int32_t PelvisKey = 51;

// Breast bones of the Retail female skeletons: (23, 26) or (28, 32). Both must be childless siblings, mirrored
// left/right, high on the body. Checked against the Human, Blood Elf, Night Elf, Void Elf, Nightborne, Draenei and
// Lightforged skeletons.
inline void Detect(CM2Model* model, State& s) {
    s.searched = true;
    const uint32_t count = model->GetBoneCount();
    static const uint32_t pairs[2][2] = { { 23, 26 }, { 28, 32 } };

    std::vector<char> hasChild(count, 0);
    for (uint32_t i = 0; i < count; ++i) {
        int32_t key, parent;
        C3Vector pivot;
        if (model->GetBoneInfoByIndex(i, key, parent, pivot) && parent >= 0 && static_cast<uint32_t>(parent) < count) {
            hasChild[parent] = 1;
        }
    }

    for (const auto& pair : pairs) {
        const uint32_t a = pair[0], b = pair[1];
        if (b >= count || hasChild[a] || hasChild[b]) {
            continue;
        }
        int32_t keyA, parentA, keyB, parentB;
        C3Vector pa, pb;
        if (!model->GetBoneInfoByIndex(a, keyA, parentA, pa) || !model->GetBoneInfoByIndex(b, keyB, parentB, pb)) {
            continue;
        }
        const bool ok = parentA >= 0 && parentA == parentB && static_cast<uint32_t>(parentA) < count &&
            pa.y * pb.y < 0.0f && std::fabs(pa.y + pb.y) < 0.01f && std::fabs(pa.y) > 0.03f && std::fabs(pa.y) < 0.2f &&
            std::fabs(pa.x - pb.x) < 0.01f && std::fabs(pa.z - pb.z) < 0.01f && pa.z > 0.8f;
        if (ok) {
            s.hasBreasts = true;
            s.breastBone[0] = a;
            s.breastBone[1] = b;
            s.breastParent[0] = s.breastParent[1] = static_cast<uint32_t>(parentA);
            s.breastPivot[0] = pa;
            s.breastPivot[1] = pb;
            break;
        }
    }

    int32_t key, parent;
    C3Vector pivot;
    if (count > 2 && model->GetBoneInfoByIndex(2, key, parent, pivot) && key == PelvisKey) {
        s.hasPelvis = true;
        s.pelvis = 2;
        s.pelvisPivot = pivot;
    }

    // The engine twists the spine toward the camera/facing; the jiggle should not react to that.
    const uint32_t spine = model->FindBoneIndexByKey(4);
    if (spine != 0xFFFFFFFF && model->GetBoneInfoByIndex(spine, key, parent, pivot)) {
        s.hasSpine = true;
        s.spinePivot = pivot;
    }
}

// Undo the engine's spine twist (twist = the matrix it gave the spine bone) on a model-space point, about the spine pivot.
inline void Untwist(const C44Matrix& r, const C3Vector& pivot, float a[3]) {
    const float x = a[0] - pivot.x, y = a[1] - pivot.y, z = a[2] - pivot.z;
    a[0] = pivot.x + x * r.a0 + y * r.a1 + z * r.a2;
    a[1] = pivot.y + x * r.b0 + y * r.b1 + z * r.b2;
    a[2] = pivot.z + x * r.c0 + y * r.c1 + z * r.c2;
}

inline void Anchor(const C3Vector& pivot, const C44Matrix& m, float out[3]) {
    out[0] = pivot.x * m.a0 + pivot.y * m.b0 + pivot.z * m.c0 + m.d0;
    out[1] = pivot.x * m.a1 + pivot.y * m.b1 + pivot.z * m.c1 + m.d1;
    out[2] = pivot.x * m.a2 + pivot.y * m.b2 + pivot.z * m.c2 + m.d2;
}

// A model-space vector into the frame of a bone matrix (rows a, b, c are the frame axes; divide out uniform scale).
inline void ToBoneFrame(const C44Matrix& m, const float v[3], float out[3]) {
    const float rows[3][3] = { { m.a0, m.a1, m.a2 }, { m.b0, m.b1, m.b2 }, { m.c0, m.c1, m.c2 } };
    for (int i = 0; i < 3; ++i) {
        const float len2 = rows[i][0] * rows[i][0] + rows[i][1] * rows[i][1] + rows[i][2] * rows[i][2];
        out[i] = len2 > 1.0e-6f ? (v[0] * rows[i][0] + v[1] * rows[i][1] + v[2] * rows[i][2]) / len2 : 0.0f;
    }
}

// Smooth limit: follows the value closely for small motion and eases into +/-limit, with no flat top or sudden stop.
inline float Clamp(float value, float limit) {
    return limit > 0.0f ? limit * std::tanh(value / limit) : 0.0f;
}

// Call once a frame per model, after the engine has animated it. wantButt: also produce the rear value.
// Returns false (and a zero frame) when the model has nothing to drive or jiggle is off.
inline bool Update(CM2Model* model, State& s, bool wantButt, Frame& frame, const Motion* motion = nullptr, const C44Matrix* spineTwist = nullptr) {
    frame = Frame();
    const Settings& cfg = Config();
    const float strength = StrengthScale();
    const float breastGain = cfg.breastGain * strength;
    const float breastMax = cfg.breastMax * strength;
    const float buttGain = cfg.buttGain * strength;
    const float buttMax = cfg.buttMax * std::min(strength, 1.5f); // the rear value is a mesh slider; keep it modest
    if (!model || !model->IsLoaded()) {
        return false;
    }
    if (!s.searched) {
        Detect(model, s);
    }

    const double now = Now();
    const float dt = s.lastTime > 0.0 ? static_cast<float>(now - s.lastTime) : 0.0f;
    s.lastTime = now;

    if (!Active()) {
        s.breastSpring[0].Reset();
        s.breastSpring[1].Reset();
        s.buttSpring.Reset();
        s.worldPrimed = s.hasLastVel = false;
        s.hasFacing = s.hasYawRate = false;
        return false;
    }

    // The character's own acceleration through the world, in the model's axes. Only a change of velocity counts:
    // turning on the spot, or moving the camera, adds nothing.
    float kick[3] = { 0.0f, 0.0f, 0.0f };
    float turn = 0.0f;
    if (motion && motion->valid) {
        bool haveVel = false;
        float vw[3] = { 0.0f, 0.0f, 0.0f };
        if (s.worldPrimed && dt > 0.0f && dt < 0.25f) {
            float dp[3];
            for (int i = 0; i < 3; ++i) {
                dp[i] = motion->pos[i] - s.lastPos[i];
            }
            if (dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2] < 9.0f) { // not a teleport
                for (int i = 0; i < 3; ++i) {
                    vw[i] = dp[i] / dt;
                }
                haveVel = true;
            }
        }
        if (haveVel && s.hasLastVel) {
            const float dvw[3] = { vw[0] - s.lastWorldVel[0], vw[1] - s.lastWorldVel[1], vw[2] - s.lastWorldVel[2] };
            const float cf = std::cos(motion->facing), sf = std::sin(motion->facing);
            kick[0] = cfg.worldMix * (cf * dvw[0] + sf * dvw[1]);
            kick[1] = cfg.worldMix * (-sf * dvw[0] + cf * dvw[1]);
            kick[2] = cfg.worldMix * dvw[2];
            for (int i = 0; i < 3; ++i) {
                kick[i] = Clamp(kick[i], 0.8f);
            }
        }
        for (int i = 0; i < 3; ++i) {
            s.lastWorldVel[i] = vw[i];
            s.lastPos[i] = motion->pos[i];
        }
        s.hasLastVel = haveVel;
        s.worldPrimed = true;

        // Change of turn speed (rad/s per frame step), for the turn swing.
        if (s.hasFacing && dt > 0.0f && dt < 0.25f) {
            float dyaw = motion->facing - s.lastFacing;
            const float pi = 3.14159265f;
            while (dyaw > pi) {
                dyaw -= 2.0f * pi;
            }
            while (dyaw < -pi) {
                dyaw += 2.0f * pi;
            }
            const float rate = dyaw / dt;
            if (s.hasYawRate) {
                turn = Clamp(rate - s.lastYawRate, 30.0f);
            }
            s.lastYawRate = rate;
            s.hasYawRate = true;
        } else {
            s.hasYawRate = false;
        }
        s.lastFacing = motion->facing;
        s.hasFacing = true;
    } else {
        s.worldPrimed = s.hasLastVel = false;
        s.hasFacing = s.hasYawRate = false;
    }

    bool any = false;
    if (s.hasBreasts) {
        C44Matrix parent;
        if (model->GetBoneAnimatedMatrix(s.breastParent[0], parent)) {
            for (int i = 0; i < 2; ++i) {
                float anchor[3], d[3], local[3];
                Anchor(s.breastPivot[i], parent, anchor);
                if (spineTwist && s.hasSpine) {
                    Untwist(*spineTwist, s.spinePivot, anchor);
                }
                // Turning: the body's velocity at this point changed by (-dw * y, dw * x); the tissue keeps its own.
                const float tk[3] = { kick[0] - cfg.turnMix * turn * anchor[1], kick[1] + cfg.turnMix * turn * anchor[0], kick[2] };
                s.breastSpring[i].Kick(tk);
                s.breastSpring[i].Step(anchor, dt, cfg.breastHz * (i == 0 ? 1.0f + cfg.breastDetune : 1.0f - cfg.breastDetune), cfg.breastZeta, d);
                ToBoneFrame(parent, d, local);
                frame.breastBone[i] = s.breastBone[i];
                const float dx = Clamp(local[0] * breastGain, breastMax);
                const float dy = Clamp(local[1] * breastGain * cfg.breastSide, breastMax);
                const float dz = Clamp(local[2] * breastGain * cfg.breastUp, breastMax);
                const float mix = std::max(0.0f, std::min(1.0f, cfg.breastRotate));
                frame.breast[i][0] = dx * (1.0f - mix);
                frame.breast[i][1] = dy * (1.0f - mix);
                frame.breast[i][2] = dz * (1.0f - mix);
                if (mix > 0.0f) {
                    // The breast's centre of mass sits about (0.068, 0, -0.078) from its pivot (Rwise's capsule).
                    // Turning by w = (r x d) / |r|^2 moves that point by d.
                    const float rx = 0.068f, rz = -0.078f, r2 = rx * rx + rz * rz;
                    frame.breastRot[i][0] = mix * (-rz * dy) / r2;
                    frame.breastRot[i][1] = mix * (rz * dx - rx * dz) / r2;
                    frame.breastRot[i][2] = mix * (rx * dy) / r2;
                }
            }
            frame.breastCount = 2;
            any = true;
        }
    }

    if (wantButt && ButtActive() && s.hasPelvis) {
        C44Matrix pelvis;
        if (model->GetBoneAnimatedMatrix(s.pelvis, pelvis)) {
            float anchor[3], d[3];
            Anchor(s.pelvisPivot, pelvis, anchor);
            s.buttSpring.Kick(kick);
            s.buttSpring.Step(anchor, dt, cfg.buttHz, cfg.buttZeta, d);
            // Up/down is the main motion; forward/back adds a little.
            frame.butt = Clamp((d[2] + 0.5f * d[0]) * buttGain, buttMax);
            any = true;
        }
    }
    return any;
}

} // namespace JigglePhysics

#endif
