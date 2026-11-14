#ifndef RETAIL_CAPE_PHYSICS_H
#define RETAIL_CAPE_PHYSICS_H

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Retail/JigglePhysics.h"
#include "Engine/Source/Tempest/C44Matrix.h"

// Spring motion for cloaks (Claude 2026-11-13). Retail player skeletons carry a five-bone cape chain (key bones 36..40,
// Cape1..Cape5, each the child of the one before; Cape1 hangs from the chest). The cloak geosets 1502..1510 are skinned to
// it, so turning those bones turns the cloak. Nothing is stored in a file.
//
// Each link gets a damped spring on its tip (the pivot of the next link; the last one uses a point 0.27 below it). The
// spring's anchor is the tip where the animated body would carry it, so what lags behind the body is the cloak's swing:
// the same model as JigglePhysics. The lag is turned into a small rotation of the link about its own pivot, with the
// motion of the links above subtracted, so a link never double counts what its parents already did. A little wind
// (the cloak streams back while running) is added to the lag, and the cloak is not allowed to swing forward into the
// body.
//
// The bones are found by key id, scanning the bones, not through the key lookup table, because custom models (the
// Sylvari) add the chain without touching that table. A model without all five bones, or whose chain is not linked in
// order, is left alone.
namespace CapePhysics {

constexpr int Links = 5;
constexpr int32_t FirstKey = 36;
constexpr uint32_t ProcedurallyDriven = 0x80;

struct Settings {
    bool enabled = true;
    // Natural frequency per link (Hz), top to bottom: the lower links swing slower.
    float hz[Links] = { 3.2f, 2.8f, 2.4f, 2.1f, 1.9f };
    float zeta = 0.42f;
    // How much of the lag becomes swing, and the limits on the tip's travel in the chest frame (model units):
    // back (-x) is far, forward (+x) only a little because the body is there, sideways both ways.
    float gain = 2.0f;
    // The lag is weighted by (depth below the shoulders)^heightPower, so the hem swings further than the top and the
    // cloak bends instead of swinging like a stick. 1 = a rigid stick.
    float heightPower = 1.6f;
    float maxBack = 0.30f;
    float maxForward = 0.012f;
    float maxSide = 0.10f;
    // Speed through the world (yards/s, along the facing) that streams the cloak backwards, and its cap.
    float windMix = 0.045f;
    float windMax = 0.22f;
    // Share of the character's own acceleration that reaches the springs.
    float worldMix = 0.10f;
    // Extra swing when the character turns.
    float turnMix = 0.6f;
    // Rest flare: a fixed backward turn (radians, summed over the whole chain, so the hem stands off the body by about
    // 0.37 * flare yards). customFlare applies to models whose cape chain was appended to the end of the bone list (the
    // custom Sylvari), whose rear sticks out further than the retail bodies; flare applies to every other model.
    float flare = 0.0f;
    float customFlare = 0.82f; // owner-tuned in game 2026-11-13 (0.80-0.85 clears the tail)
};

inline Settings& Config() {
    static Settings settings;
    return settings;
}

// Interface > Aetheria Physics: "capePhysics" (default 1) and "capeStrength" 1..4 (default 2).
inline bool Active() {
    static CVar* master = nullptr;
    if (!master) {
        master = CVar::LookupRegistered("capePhysics");
    }
    return Config().enabled && (!master || master->m_intValue != 0);
}

inline float StrengthScale() {
    static CVar* strength = nullptr;
    if (!strength) {
        strength = CVar::LookupRegistered("capeStrength");
    }
    static const float table[4] = { 0.4f, 1.0f, 1.5f, 2.0f };
    int step = strength ? static_cast<int>(strength->m_floatValue + 0.5f) : 2;
    step = std::max(1, std::min(4, step));
    return table[step - 1];
}

struct State {
    bool searched = false;
    bool found = false;
    uint32_t boneCount = 0;
    uint32_t bone[Links] = {};
    uint32_t parent = 0; // what Cape1 hangs from (the chest)
    float pivot[Links][3] = {};
    float tip[Links][3] = {}; // bind position of the point each link's spring tracks
    JigglePhysics::Spring spring[Links];
    double lastTime = 0.0;
    bool worldPrimed = false;
    bool hasLastVel = false;
    float lastPos[3] = { 0.0f, 0.0f, 0.0f };
    float lastWorldVel[3] = { 0.0f, 0.0f, 0.0f };
    float smoothVel[3] = { 0.0f, 0.0f, 0.0f }; // along the model axes
    bool hasFacing = false;
    bool hasYawRate = false;
    float lastFacing = 0.0f;
    float lastYawRate = 0.0f;
    bool hasSpine = false;
    C3Vector spinePivot;
    bool applied = false;
};

// The flare is a posture, not motion: it follows the strength setting only a little.
inline float StrengthFlare(float strength) {
    return 0.75f + 0.25f * strength;
}

inline void Cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// Pure rotation about the bone pivot by the rotation vector w (radians), same layout as BodyShape::ToMatrix.
inline C44Matrix RotationMatrix(const float w[3]) {
    C44Matrix m;
    m.Identity();
    const float t2 = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
    if (t2 > 1e-12f) {
        const float th = std::sqrt(t2), ux = w[0] / th, uy = w[1] / th, uz = w[2] / th;
        const float c = std::cos(th), s = std::sin(th), v = 1.0f - c;
        const float R[3][3] = {
            { c + ux * ux * v, ux * uy * v - uz * s, ux * uz * v + uy * s },
            { uy * ux * v + uz * s, c + uy * uy * v, uy * uz * v - ux * s },
            { uz * ux * v - uy * s, uz * uy * v + ux * s, c + uz * uz * v } };
        m.a0 = R[0][0]; m.a1 = R[1][0]; m.a2 = R[2][0];
        m.b0 = R[0][1]; m.b1 = R[1][1]; m.b2 = R[2][1];
        m.c0 = R[0][2]; m.c1 = R[1][2]; m.c2 = R[2][2];
    }
    return m;
}

inline void Detect(CM2Model* model, State& s) {
    s.searched = true;
    s.found = false;
    s.boneCount = model->GetBoneCount();
    uint32_t index[Links];
    int32_t parentOf[Links];
    for (int k = 0; k < Links; ++k) {
        index[k] = 0xFFFFFFFF;
        parentOf[k] = -1;
    }
    for (uint32_t i = 0; i < s.boneCount; ++i) {
        int32_t key, parent;
        C3Vector pivot;
        if (!model->GetBoneInfoByIndex(i, key, parent, pivot)) {
            continue;
        }
        const int k = key - FirstKey;
        if (k >= 0 && k < Links && index[k] == 0xFFFFFFFF) {
            index[k] = i;
            parentOf[k] = parent;
            s.pivot[k][0] = pivot.x;
            s.pivot[k][1] = pivot.y;
            s.pivot[k][2] = pivot.z;
        }
    }
    for (int k = 0; k < Links; ++k) {
        if (index[k] == 0xFFFFFFFF) {
            return;
        }
        if (k > 0 && parentOf[k] != static_cast<int32_t>(index[k - 1])) {
            return; // not a plain chain
        }
    }
    if (parentOf[0] < 0 || static_cast<uint32_t>(parentOf[0]) >= s.boneCount) {
        return;
    }
    // Sanity: the chain hangs down and the pivots sit behind the body centre line.
    for (int k = 1; k < Links; ++k) {
        if (!(s.pivot[k][2] < s.pivot[k - 1][2] - 0.05f) || std::fabs(s.pivot[k][1]) > 0.1f) {
            return;
        }
    }
    for (int k = 0; k < Links; ++k) {
        s.bone[k] = index[k];
        for (int a = 0; a < 3; ++a) {
            s.tip[k][a] = k + 1 < Links ? s.pivot[k + 1][a] : s.pivot[k][a];
        }
        if (k + 1 == Links) {
            s.tip[k][2] = s.pivot[k][2] - 0.27f;
        }
    }
    s.parent = static_cast<uint32_t>(parentOf[0]);
    int32_t key, parent;
    C3Vector pivot;
    const uint32_t spine = model->FindBoneIndexByKey(4);
    if (spine != 0xFFFFFFFF && model->GetBoneInfoByIndex(spine, key, parent, pivot)) {
        s.hasSpine = true;
        s.spinePivot = pivot;
    }
    s.found = true;
}

inline void Release(CM2Model* model, State& s) {
    if (s.applied && model && model->IsLoaded() && s.found && model->GetBoneCount() == s.boneCount) {
        for (int k = 0; k < Links; ++k) {
            model->SetBoneFlagsByIndex(s.bone[k], 0, ProcedurallyDriven);
        }
    }
    s.applied = false;
    for (int k = 0; k < Links; ++k) {
        s.spring[k].Reset();
    }
    s.worldPrimed = s.hasLastVel = s.hasFacing = s.hasYawRate = false;
    s.smoothVel[0] = s.smoothVel[1] = s.smoothVel[2] = 0.0f;
}

// Call once a frame per model, after the engine has animated it (same place as the body shape / jiggle pass).
// run = false releases the cloak (far away, or physics off).
inline void Update(CM2Model* model, State& s, bool run, const JigglePhysics::Motion* motion, const C44Matrix* spineTwist) {
    if (!model || !model->IsLoaded()) {
        return;
    }
    if (!s.searched || s.boneCount != model->GetBoneCount()) {
        Release(model, s); // a different model at the same address
        s = State();
        Detect(model, s);
    }
    if (!s.found) {
        return;
    }

    const double now = JigglePhysics::Now();
    const float dt = s.lastTime > 0.0 ? static_cast<float>(now - s.lastTime) : 0.0f;
    s.lastTime = now;

    if (!run || !Active()) {
        Release(model, s);
        return;
    }

    const Settings& cfg = Config();
    const float strength = StrengthScale();

    // The character's own acceleration and speed, in the model's axes (x forward, y left).
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
            if (dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2] < 9.0f) {
                for (int i = 0; i < 3; ++i) {
                    vw[i] = dp[i] / dt;
                }
                haveVel = true;
            }
        }
        const float cf = std::cos(motion->facing), sf = std::sin(motion->facing);
        if (haveVel) {
            const float local[3] = { cf * vw[0] + sf * vw[1], -sf * vw[0] + cf * vw[1], vw[2] };
            const float a = std::min(1.0f, dt / 0.15f); // smooth over about 0.15 s so footsteps do not flutter it
            for (int i = 0; i < 3; ++i) {
                s.smoothVel[i] += (local[i] - s.smoothVel[i]) * a;
            }
            if (s.hasLastVel) {
                const float dvw[3] = { vw[0] - s.lastWorldVel[0], vw[1] - s.lastWorldVel[1], vw[2] - s.lastWorldVel[2] };
                kick[0] = cfg.worldMix * (cf * dvw[0] + sf * dvw[1]);
                kick[1] = cfg.worldMix * (-sf * dvw[0] + cf * dvw[1]);
                kick[2] = cfg.worldMix * dvw[2];
                for (int i = 0; i < 3; ++i) {
                    kick[i] = JigglePhysics::Clamp(kick[i], 0.8f);
                }
            }
        }
        for (int i = 0; i < 3; ++i) {
            s.lastWorldVel[i] = vw[i];
            s.lastPos[i] = motion->pos[i];
        }
        s.hasLastVel = haveVel;
        s.worldPrimed = true;

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
                turn = JigglePhysics::Clamp(rate - s.lastYawRate, 30.0f);
            }
            s.lastYawRate = rate;
            s.hasYawRate = true;
        } else {
            s.hasYawRate = false;
        }
        s.lastFacing = motion->facing;
        s.hasFacing = true;
    } else {
        s.worldPrimed = s.hasLastVel = s.hasFacing = s.hasYawRate = false;
    }

    C44Matrix parent;
    if (!model->GetBoneAnimatedMatrix(s.parent, parent)) {
        return;
    }

    // Wind: forward speed streams the cloak backwards, more toward the hem. Sideways speed pushes it the other way.
    const float windX = -std::max(0.0f, std::min(cfg.windMax, cfg.windMix * s.smoothVel[0]));
    const float windY = -std::max(-cfg.windMax * 0.5f, std::min(cfg.windMax * 0.5f, cfg.windMix * 0.5f * s.smoothVel[1]));

    float w[Links][3];
    for (int k = 0; k < Links; ++k) {
        const float height = static_cast<float>(k + 1) / static_cast<float>(Links);
        float anchor[3];
        JigglePhysics::Anchor(C3Vector(s.tip[k][0], s.tip[k][1], s.tip[k][2]), parent, anchor);
        if (spineTwist && s.hasSpine) {
            JigglePhysics::Untwist(*spineTwist, s.spinePivot, anchor);
        }
        const float tk[3] = { kick[0] - cfg.turnMix * turn * anchor[1], kick[1] + cfg.turnMix * turn * anchor[0], kick[2] };
        s.spring[k].Kick(tk);
        float d[3], local[3];
        s.spring[k].Step(anchor, dt, cfg.hz[k], cfg.zeta, d);
        // The wind is a steady push along the character's own x/y (the model axes), on top of the lag.
        d[0] += windX * height;
        d[1] += windY * height;
        JigglePhysics::ToBoneFrame(parent, d, local);
        const float depth = std::max(0.05f, (s.pivot[0][2] - s.tip[k][2]) / std::max(0.1f, s.pivot[0][2] - s.tip[Links - 1][2]));
        const float weight = std::pow(depth, cfg.heightPower) * cfg.gain * strength;
        local[0] = std::max(-cfg.maxBack, std::min(cfg.maxForward, local[0] * weight));
        local[1] = JigglePhysics::Clamp(local[1] * weight, cfg.maxSide);
        local[2] = 0.0f; // a rigid link only swings; its length does not change

        // Subtract what the links above already did to this tip (small angles), then turn this link about its pivot.
        float r[3] = { s.tip[k][0] - s.pivot[k][0], s.tip[k][1] - s.pivot[k][1], s.tip[k][2] - s.pivot[k][2] };
        float residual[3] = { local[0], local[1], local[2] };
        for (int j = 0; j < k; ++j) {
            const float arm[3] = { s.tip[k][0] - s.pivot[j][0], s.tip[k][1] - s.pivot[j][1], s.tip[k][2] - s.pivot[j][2] };
            float moved[3];
            Cross(w[j], arm, moved);
            for (int a = 0; a < 3; ++a) {
                residual[a] -= moved[a];
            }
        }
        const float r2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
        float cross[3];
        Cross(r, residual, cross);
        for (int a = 0; a < 3; ++a) {
            w[k][a] = r2 > 1e-6f ? cross[a] / r2 : 0.0f;
        }
        // Keep each link's own turn within about 40 degrees.
        const float len = std::sqrt(w[k][0] * w[k][0] + w[k][1] * w[k][1] + w[k][2] * w[k][2]);
        if (len > 0.7f) {
            for (int a = 0; a < 3; ++a) {
                w[k][a] *= 0.7f / len;
            }
        }
    }

    // A chain appended to the end of the bone list is a custom model's own; it gets customFlare, others get flare.
    const float restFlare = (s.bone[0] + Links == s.boneCount ? cfg.customFlare : cfg.flare) * StrengthFlare(strength);
    for (int k = 0; k < Links; ++k) {
        w[k][1] += restFlare * static_cast<float>(k + 1) / 15.0f; // +y turn moves the hem backward (-x); the weights sum to 1
        model->SetBoneProceduralTransformByIndex(s.bone[k], RotationMatrix(w[k]));
        model->SetBoneFlagsByIndex(s.bone[k], ProcedurallyDriven, ProcedurallyDriven);
    }
    s.applied = true;
}

} // namespace CapePhysics

#endif
