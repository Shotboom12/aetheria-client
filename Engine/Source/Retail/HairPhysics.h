#ifndef RETAIL_HAIR_PHYSICS_H
#define RETAIL_HAIR_PHYSICS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Retail/CapePhysics.h"
#include "Engine/Source/Retail/JigglePhysics.h"
#include "Engine/Source/Tempest/C44Matrix.h"

// Spring motion for long hair (Claude 2026-11-14), the same method as the cloak (CapePhysics.h) and the jiggle.
//
// Retail player skeletons carry the hair as chains of unnamed bones (key bone id -1) that hang from the head bone (key 6),
// 3 to 9 links long; the hairstyle geosets (ponytails, braids, long hair, some beards and tendrils) are skinned to them.
// Nothing in a file says which chain belongs to which style, and it does not have to: a chain whose geoset is not on screen
// moves no vertex. So every chain that hangs from the head, is at least three links long, and drops at least minDrop below
// its root (hair that falls, not ears, horns or tusks that point up or sideways) is driven.
//
// Each link gets a damped spring on its tip (the pivot of the next link), anchored where the animated head would carry it;
// the lag becomes a small rotation of the link about its own pivot, with what the links above already did subtracted, and
// the swing is weighted by how far down the chain the link is (the tip swings most). Models without such chains, or that
// have no head bone, are left alone.
namespace HairPhysics {

constexpr int MaxChains = 12;
constexpr int MaxLinks = 9;
constexpr int MinLinks = 3;
constexpr int32_t HeadKey = 6;
constexpr uint32_t ProcedurallyDriven = 0x80;

struct Settings {
    bool enabled = true;
    // Natural frequency of the first link (Hz); each further link is hzStep slower, never below hzMin.
    // Owner-tuned in game 2026-11-14 (heavy, slow, almost no ringing: it reads as hair, not as soft tissue).
    float hz0 = 2.6f;
    float hzStep = 0.15f;
    float hzMin = 1.6f;
    float zeta = 0.85f;
    float gain = 2.4f;
    float heightPower = 1.3f;
    // Limits on the tip's travel in the head's frame (model units). Back is -x.
    float maxBack = 0.12f;
    float maxForward = 0.04f;
    // A chain that hangs in front of the head (a beard, face tendrils) rests against the chest, so it may not swing forward into it.
    // "In front" = its last pivot is more than frontMargin ahead of the head pivot.
    float maxForwardFront = 0.004f;
    float frontMargin = 0.03f;
    float maxSide = 0.10f;
    float windMix = 0.02f;
    float windMax = 0.06f;
    float worldMix = 0.10f;
    float turnMix = 0.5f;
    // A chain must drop at least this far from its root pivot to its last pivot (model units) to count as hair.
    float minDrop = 0.12f;
};

inline Settings& Config() {
    static Settings settings;
    return settings;
}

// Interface > Aetheria Physics: "hairPhysics" (default 1) and "hairStrength" 1..4 (default 2).
inline bool Active() {
    static CVar* master = nullptr;
    if (!master) {
        master = CVar::LookupRegistered("hairPhysics");
    }
    return Config().enabled && (!master || master->m_intValue != 0);
}

inline float StrengthScale() {
    static CVar* strength = nullptr;
    if (!strength) {
        strength = CVar::LookupRegistered("hairStrength");
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
    uint32_t head = 0;
    int chainCount = 0;
    int linkCount[MaxChains] = {};
    uint32_t bone[MaxChains][MaxLinks] = {};
    float pivot[MaxChains][MaxLinks][3] = {};
    float tip[MaxChains][MaxLinks][3] = {}; // bind position of the point each link's spring tracks
    bool front[MaxChains] = {}; // the chain hangs in front of the head
    JigglePhysics::Spring spring[MaxChains][MaxLinks];
    double lastTime = 0.0;
    bool worldPrimed = false;
    bool hasLastVel = false;
    float lastPos[3] = { 0.0f, 0.0f, 0.0f };
    float lastWorldVel[3] = { 0.0f, 0.0f, 0.0f };
    float smoothVel[3] = { 0.0f, 0.0f, 0.0f };
    bool hasFacing = false;
    bool hasYawRate = false;
    float lastFacing = 0.0f;
    float lastYawRate = 0.0f;
    bool applied = false;
};

inline void Detect(CM2Model* model, State& s) {
    s.searched = true;
    s.found = false;
    s.chainCount = 0;
    s.boneCount = model->GetBoneCount();
    const uint32_t head = model->FindBoneIndexByKey(HeadKey);
    if (head == 0xFFFFFFFF || head >= s.boneCount) {
        return;
    }
    s.head = head;

    std::vector<int32_t> keys(s.boneCount, -2), parents(s.boneCount, -1);
    std::vector<int> childCount(s.boneCount, 0);
    std::vector<C3Vector> pivots(s.boneCount);
    for (uint32_t i = 0; i < s.boneCount; ++i) {
        int32_t key, parent;
        C3Vector pivot;
        if (model->GetBoneInfoByIndex(i, key, parent, pivot)) {
            keys[i] = key;
            parents[i] = parent;
            pivots[i] = pivot;
            if (parent >= 0 && static_cast<uint32_t>(parent) < s.boneCount) {
                ++childCount[parent];
            }
        }
    }

    const Settings& cfg = Config();
    for (uint32_t b0 = 0; b0 < s.boneCount && s.chainCount < MaxChains; ++b0) {
        if (keys[b0] != -1 || parents[b0] != static_cast<int32_t>(head)) {
            continue;
        }
        uint32_t chain[MaxLinks];
        int len = 0;
        chain[len++] = b0;
        while (len < MaxLinks && childCount[chain[len - 1]] == 1) {
            uint32_t next = 0xFFFFFFFF;
            for (uint32_t i = 0; i < s.boneCount; ++i) {
                if (parents[i] == static_cast<int32_t>(chain[len - 1])) {
                    next = i;
                    break;
                }
            }
            if (next == 0xFFFFFFFF || keys[next] != -1) {
                break;
            }
            chain[len++] = next;
        }
        if (len < MinLinks) {
            continue;
        }
        if (pivots[chain[0]].z - pivots[chain[len - 1]].z < cfg.minDrop) {
            continue; // not hair that falls (ears, horns, tusks, a bun)
        }
        const int c = s.chainCount++;
        s.linkCount[c] = len;
        s.front[c] = pivots[chain[len - 1]].x - pivots[head].x > cfg.frontMargin;
        for (int k = 0; k < len; ++k) {
            s.bone[c][k] = chain[k];
            s.pivot[c][k][0] = pivots[chain[k]].x;
            s.pivot[c][k][1] = pivots[chain[k]].y;
            s.pivot[c][k][2] = pivots[chain[k]].z;
        }
        for (int k = 0; k < len; ++k) {
            for (int a = 0; a < 3; ++a) {
                if (k + 1 < len) {
                    s.tip[c][k][a] = s.pivot[c][k + 1][a];
                } else {
                    // The last link continues in the direction of the one before it.
                    s.tip[c][k][a] = s.pivot[c][k][a] + (s.pivot[c][k][a] - s.pivot[c][k - 1][a]);
                }
            }
        }
    }
    s.found = s.chainCount > 0;
}

inline void Release(CM2Model* model, State& s) {
    if (s.applied && model && model->IsLoaded() && s.found && model->GetBoneCount() == s.boneCount) {
        for (int c = 0; c < s.chainCount; ++c) {
            for (int k = 0; k < s.linkCount[c]; ++k) {
                model->SetBoneFlagsByIndex(s.bone[c][k], 0, ProcedurallyDriven);
            }
        }
    }
    s.applied = false;
    for (int c = 0; c < MaxChains; ++c) {
        for (int k = 0; k < MaxLinks; ++k) {
            s.spring[c][k].Reset();
        }
    }
    s.worldPrimed = s.hasLastVel = s.hasFacing = s.hasYawRate = false;
    s.smoothVel[0] = s.smoothVel[1] = s.smoothVel[2] = 0.0f;
}

// Call once a frame per model, after the engine has animated it (same place as the cloak pass).
// run = false releases the hair (far away, or physics off).
inline void Update(CM2Model* model, State& s, bool run, const JigglePhysics::Motion* motion) {
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

    // The character's own acceleration, speed and turning, in the model's axes (x forward, y left): as for the cloak.
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
            const float a = std::min(1.0f, dt / 0.15f);
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

    C44Matrix head;
    if (!model->GetBoneAnimatedMatrix(s.head, head)) {
        return;
    }

    const float windX = -std::max(0.0f, std::min(cfg.windMax, cfg.windMix * s.smoothVel[0]));
    const float windY = -std::max(-cfg.windMax * 0.5f, std::min(cfg.windMax * 0.5f, cfg.windMix * 0.5f * s.smoothVel[1]));

    for (int c = 0; c < s.chainCount; ++c) {
        const int links = s.linkCount[c];
        float w[MaxLinks][3];
        for (int k = 0; k < links; ++k) {
            const float height = static_cast<float>(k + 1) / static_cast<float>(links);
            float anchor[3];
            JigglePhysics::Anchor(C3Vector(s.tip[c][k][0], s.tip[c][k][1], s.tip[c][k][2]), head, anchor);
            const float tk[3] = { kick[0] - cfg.turnMix * turn * anchor[1], kick[1] + cfg.turnMix * turn * anchor[0], kick[2] };
            s.spring[c][k].Kick(tk);
            const float hz = std::max(cfg.hzMin, cfg.hz0 - cfg.hzStep * static_cast<float>(k));
            float d[3], local[3];
            s.spring[c][k].Step(anchor, dt, hz, cfg.zeta, d);
            d[0] += windX * height;
            d[1] += windY * height;
            JigglePhysics::ToBoneFrame(head, d, local);
            const float weight = std::pow(height, cfg.heightPower) * cfg.gain * strength;
            local[0] = std::max(-cfg.maxBack, std::min(s.front[c] ? cfg.maxForwardFront : cfg.maxForward, local[0] * weight));
            local[1] = JigglePhysics::Clamp(local[1] * weight, cfg.maxSide);
            local[2] = 0.0f; // a rigid link only swings

            float r[3] = { s.tip[c][k][0] - s.pivot[c][k][0], s.tip[c][k][1] - s.pivot[c][k][1], s.tip[c][k][2] - s.pivot[c][k][2] };
            float residual[3] = { local[0], local[1], local[2] };
            for (int j = 0; j < k; ++j) {
                const float arm[3] = { s.tip[c][k][0] - s.pivot[c][j][0], s.tip[c][k][1] - s.pivot[c][j][1], s.tip[c][k][2] - s.pivot[c][j][2] };
                float moved[3];
                CapePhysics::Cross(w[j], arm, moved);
                for (int a = 0; a < 3; ++a) {
                    residual[a] -= moved[a];
                }
            }
            const float r2 = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
            float cross[3];
            CapePhysics::Cross(r, residual, cross);
            for (int a = 0; a < 3; ++a) {
                w[k][a] = r2 > 1e-6f ? cross[a] / r2 : 0.0f;
            }
            const float len = std::sqrt(w[k][0] * w[k][0] + w[k][1] * w[k][1] + w[k][2] * w[k][2]);
            if (len > 0.6f) {
                for (int a = 0; a < 3; ++a) {
                    w[k][a] *= 0.6f / len;
                }
            }
        }
        for (int k = 0; k < links; ++k) {
            model->SetBoneProceduralTransformByIndex(s.bone[c][k], CapePhysics::RotationMatrix(w[k]));
            model->SetBoneFlagsByIndex(s.bone[c][k], ProcedurallyDriven, ProcedurallyDriven);
        }
    }
    s.applied = true;
}

} // namespace HairPhysics

#endif
