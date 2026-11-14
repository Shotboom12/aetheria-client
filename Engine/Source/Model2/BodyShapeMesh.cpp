#include "Engine/Source/Retail/BodyShapeMesh.h"

#include <cmath>
#include <cstring>

#include "Engine/Source/Retail/BodyShapeData/Profiles.hpp"

namespace {

// Every audited profile's bounds are the mesh's bind-pose bounds padded by this much on each side.
constexpr float BoundsPad = 0.2f;
constexpr float BoundsTolerance = 0.01f;

body_shape::Vec PositionOf(const float* bind, uint32_t i) {
    return { bind[6 * i], bind[6 * i + 1], bind[6 * i + 2] };
}

// Profiles for the friend's "Idealistic Physics" female replacer meshes (Human, Blood Elf, Night Elf, Draenei,
// Nightborne, Void Elf, Lightforged). Same skeleton
// and torso layout as the retail meshes they replace, but a different vertex set, so there is no per-vertex mask:
// the bind-pose bounds are the fingerprint and the Deform field limits the effect to the chest and rear. The
// deformation parameters are copied from the retail profile of the same race. `strength` scales the sliders so
// that full slider travel stays realistic on these already fuller meshes.
struct ReplacerProfile {
    body_shape::Profile profile;
    float strength;
};

const ReplacerProfile ReplacerProfiles[] = {
    { { "replacer/human",
        {{0.000000000f,0.000000000f,0.000000000f},{1.000000000f,1.000000000f,1.000000000f}}, {{0.000000000f,0.000000000f,0.000000000f},{1.000000000f,1.000000000f,1.000000000f}},
        { -0.798679841f, -0.870214593f, -0.209725075f }, { 0.417197552f, 0.871325088f, 2.178961110f },
        nullptr, 0, "replacer/human", false,1.330f,0.965f,0.075f,0.080f }, 0.8f },
    { { "replacer/bloodelf",
        {{-0.037505936f,0.000481503f,-0.224731085f},{0.871452583f,0.871452583f,1.182807811f}}, {{-0.044710917f,0.000000000f,-0.003835191f},{0.946841581f,0.946841581f,1.026972751f}},
        { -0.966318083f, -0.720302474f, -0.214144623f }, { 0.381844249f, 0.720302892f, 2.284223509f },
        nullptr, 0, "replacer/bloodelf", true,1.396f,0.980f,0.060f,0.035f }, 0.8f },
    { { "replacer/nightelf",
        {{-0.014176524f,0.000571621f,0.164680692f},{1.030137054f,1.030137054f,1.093528936f}}, {{-0.034358350f,0.000000004f,0.001955687f},{1.134493927f,1.134493927f,1.190388475f}},
        { -0.673472267f, -1.133077693f, -0.209366791f }, { 0.438254815f, 1.133078229f, 2.553027582f },
        nullptr, 0, "replacer/nightelf", true,1.391f,0.956f,0.080f,0.035f }, 0.8f },
    { { "replacer/draenei",
        {{0.057434268f,0.000543145f,-0.321234164f},{0.978722434f,0.978722434f,1.458885308f}}, {{0.045051533f,0.000000000f,0.076243186f},{1.150033811f,1.150033811f,1.154680353f}},
        { -1.273901057f, -1.102837217f, -0.198629746f }, { 0.521424603f, 1.102837336f, 2.761364889f },
        nullptr, 0, "replacer/draenei", true,1.399f,0.941f,0.065f,0.035f }, 0.8f },
    { { "replacer/nightborne",
        {{-0.014176524f,0.000571621f,0.164680692f},{1.030137054f,1.030137054f,1.093528936f}}, {{-0.034358350f,0.000000000f,0.001955746f},{1.134493885f,1.134493885f,1.190388475f}},
        { -0.579838318f, -1.133078110f, -0.209366791f }, { 0.438254339f, 1.133078110f, 2.549845648f },
        nullptr, 0, "replacer/nightborne", true,1.396f,0.953f,0.085f,0.035f }, 0.8f },
    { { "replacer/voidelf",
        {{-0.037505936f,0.000481507f,-0.224731085f},{0.871452628f,0.871452628f,1.182807811f}}, {{-0.044710917f,0.000000000f,-0.003835126f},{0.946841581f,0.946841581f,1.026972692f}},
        { -0.825126660f, -0.720302474f, -0.214144623f }, { 0.381844249f, 0.720302892f, 2.185725880f },
        nullptr, 0, "replacer/voidelf", true,1.395f,0.980f,0.060f,0.035f }, 0.8f },
    { { "replacer/lightforged",
        {{0.057434268f,0.000543145f,-0.321234164f},{0.978722434f,0.978722434f,1.458885308f}}, {{0.045051533f,0.000000000f,0.076243186f},{1.150033811f,1.150033811f,1.154680353f}},
        { -1.266928506f, -1.102837217f, -0.198629746f }, { 0.521424603f, 1.102837336f, 2.764995527f },
        nullptr, 0, "replacer/lightforged", true,1.399f,0.941f,0.065f,0.035f }, 0.8f },
    { { "replacer/harronir",
        {{-0.014177049f,0.000571621f,0.164680337f},{1.030137054f,1.030137054f,1.093529139f}}, {{-0.034358775f,0.000000000f,0.001955757f},{1.134493885f,1.134493885f,1.190388356f}},
        { -0.561357987f, -1.140408826f, -0.210096197f }, { 0.460832489f, 1.140408826f, 2.454424810f },
        nullptr, 0, "replacer/harronir", true,1.374f,0.972f,0.075f,0.035f }, 0.8f },
    { { "replacer/dracthyr",
        {{0.000000000f,0.000000000f,0.000000000f},{1.000000000f,1.000000000f,1.000000000f}}, {{0.000000000f,0.000000000f,0.000000000f},{1.000000000f,1.000000000f,1.000000000f}},
        { -0.459594977f, -0.870214593f, -0.209725075f }, { 0.417197552f, 0.871325088f, 2.165855241f },
        nullptr, 0, "replacer/dracthyr", true,1.406f,0.965f,0.075f,0.035f }, 0.8f },
};

float StrengthOf(const body_shape::Profile* profile) {
    for (const ReplacerProfile& r : ReplacerProfiles) {
        if (&r.profile == profile) {
            return r.strength;
        }
    }
    return 1.0f;
}

} // namespace

namespace BodyShapeMesh {

const void* FindProfile(const float* bind, uint32_t count) {
    if (!bind || !count) {
        return nullptr;
    }

    float lo[3] = { 1e9f, 1e9f, 1e9f };
    float hi[3] = { -1e9f, -1e9f, -1e9f };

    for (uint32_t i = 0; i < count; ++i) {
        for (int a = 0; a < 3; ++a) {
            float v = bind[6 * i + a];

            if (std::isfinite(v)) {
                lo[a] = std::fmin(lo[a], v);
                hi[a] = std::fmax(hi[a], v);
            }
        }
    }

    const body_shape::Profile* best = nullptr;
    size_t bestHits = 0;

    for (const body_shape::Profile& profile : body_shape::Profiles) {
        const float* min = &profile.minimum.x;
        const float* max = &profile.maximum.x;
        bool bounds = true;

        for (int a = 0; a < 3; ++a) {
            bounds = bounds && std::fabs(lo[a] - (min[a] + BoundsPad)) < BoundsTolerance &&
                std::fabs(hi[a] - (max[a] - BoundsPad)) < BoundsTolerance;
        }

        if (!bounds) {
            continue;
        }

        if (!profile.mask) {
            // No per-vertex mask (human, dracthyr): the bounds are the whole fingerprint.
            if (!best) {
                best = &profile;
            }
            continue;
        }

        // Masked profiles match vertices by exact bind position, so count how many of the model's vertices hit.
        size_t hits = 0;

        for (uint32_t i = 0; i < count; ++i) {
            if (body_shape::VertexMask(PositionOf(bind, i), profile)) {
                ++hits;
            }
        }

        if (hits >= profile.maskSize * 95 / 100 && hits > bestHits) {
            best = &profile;
            bestHits = hits;
        }
    }

    if (!best) {
        for (const ReplacerProfile& r : ReplacerProfiles) {
            const float* min = &r.profile.minimum.x;
            const float* max = &r.profile.maximum.x;
            bool bounds = true;

            for (int a = 0; a < 3; ++a) {
                bounds = bounds && std::fabs(lo[a] - (min[a] + BoundsPad)) < BoundsTolerance &&
                    std::fabs(hi[a] - (max[a] - BoundsPad)) < BoundsTolerance;
            }

            if (bounds) {
                best = &r.profile;
                break;
            }
        }
    }

    return best;
}

const char* Name(const void* profile) {
    return profile ? static_cast<const body_shape::Profile*>(profile)->path : "";
}

bool Apply(const void* profileData, const float* bind, uint32_t count, float breast, float butt, float* out) {
    const body_shape::Profile& profile = *static_cast<const body_shape::Profile*>(profileData);
    bool moved = false;
    const float strengthScale = StrengthOf(&profile);
    breast *= strengthScale;
    butt *= strengthScale;

    memcpy(out, bind, sizeof(float) * 6 * count);

    for (uint32_t i = 0; i < count; ++i) {
        body_shape::Vec p = PositionOf(bind, i);
        uint32_t mask = body_shape::VertexMask(p, profile);
        float strength = static_cast<float>(mask >> 8) / 255.0f;
        float a = (mask & 1) ? breast * strength : 0.0f;
        float b = (mask & 2) ? butt * strength : 0.0f;

        if (a == 0.0f && b == 0.0f) {
            continue;
        }

        body_shape::Vec q = body_shape::Deform(p, a, b, profile);
        body_shape::Vec n = body_shape::Normal(p, { bind[6 * i + 3], bind[6 * i + 4], bind[6 * i + 5] }, a, b, profile);
        out[6 * i] = q.x;
        out[6 * i + 1] = q.y;
        out[6 * i + 2] = q.z;
        out[6 * i + 3] = n.x;
        out[6 * i + 4] = n.y;
        out[6 * i + 5] = n.z;
        moved = moved || q.x != p.x || q.y != p.y || q.z != p.z;
    }

    return moved;
}

void AffectedVertices(const void* profileData, const float* bind, uint32_t count, std::vector<uint32_t>& out) {
    const body_shape::Profile& profile = *static_cast<const body_shape::Profile*>(profileData);
    out.clear();

    for (uint32_t i = 0; i < count; ++i) {
        body_shape::Vec p = PositionOf(bind, i);
        uint32_t mask = body_shape::VertexMask(p, profile);
        if (!(mask & 3) || (mask >> 8) == 0) {
            continue;
        }
        body_shape::Vec a = body_shape::Deform(p, (mask & 1) ? 1.0f : 0.0f, 0.0f, profile);
        body_shape::Vec b = body_shape::Deform(p, 0.0f, (mask & 2) ? 1.0f : 0.0f, profile);
        if (a.x != p.x || a.y != p.y || a.z != p.z || b.x != p.x || b.y != p.y || b.z != p.z) {
            out.push_back(i);
        }
    }
}

void ApplyList(const void* profileData, const float* bind, uint32_t count, const std::vector<uint32_t>& list, float breast, float butt, float* out) {
    const body_shape::Profile& profile = *static_cast<const body_shape::Profile*>(profileData);
    const float strengthScale = StrengthOf(&profile);
    breast *= strengthScale;
    butt *= strengthScale;

    memcpy(out, bind, sizeof(float) * 6 * count);

    for (uint32_t i : list) {
        if (i >= count) {
            continue;
        }
        body_shape::Vec p = PositionOf(bind, i);
        uint32_t mask = body_shape::VertexMask(p, profile);
        float strength = static_cast<float>(mask >> 8) / 255.0f;
        float a = (mask & 1) ? breast * strength : 0.0f;
        float b = (mask & 2) ? butt * strength : 0.0f;

        if (a == 0.0f && b == 0.0f) {
            continue;
        }

        body_shape::Vec q = body_shape::Deform(p, a, b, profile);
        body_shape::Vec n = body_shape::Normal(p, { bind[6 * i + 3], bind[6 * i + 4], bind[6 * i + 5] }, a, b, profile);
        out[6 * i] = q.x;
        out[6 * i + 1] = q.y;
        out[6 * i + 2] = q.z;
        out[6 * i + 3] = n.x;
        out[6 * i + 4] = n.y;
        out[6 * i + 5] = n.z;
    }
}

} // namespace BodyShapeMesh
