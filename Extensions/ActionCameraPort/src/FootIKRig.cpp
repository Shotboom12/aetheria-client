#include "Engine/Source/Model2/M2Data.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Tempest/C4Quaternion.h"
#include "WoW/Source/Extension/ScriptDefines/ModelScript.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/Movement.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/UIUtil/ActionCameraSettings.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <limits>

namespace ActionCamera {
namespace {
    struct RigCheck {
        uint32_t index;
        int32_t parent;
        uint32_t crc;
        float pivot[3];
    };
    struct SoleVertex {
        float point[3];
        uint8_t bones[4];
        uint8_t weights[4];
    };
    struct RigFingerprint {
        const char* name;
        uint32_t bones;
        uint32_t vertices;
        const RigCheck* checks;
        uint32_t count;
        uint32_t legs[2][3];
        float soleClearance;
        uint32_t pelvis;
        const SoleVertex* soles[2];
        uint32_t soleCounts[2];
    };

#include "FootIKRigData.inc"

    bool Matches(const M2Data& data, const RigFingerprint& rig) {
        if (data.m_bones.m_count != rig.bones || data.m_vertices.m_count != rig.vertices)
            return false;
        for (uint32_t i = 0; i < rig.count; ++i) {
            const auto& check = rig.checks[i];
            if (check.index >= data.m_bones.m_count)
                return false;
            const auto& bone = data.m_bones[check.index];
            if (bone.m_parentIndex != static_cast<uint16_t>(check.parent) ||
                bone.m_boneNameCRC != check.crc ||
                !(C3Vector::Distance(bone.m_pivot, C3Vector(check.pivot[0], check.pivot[1], check.pivot[2])) <= 0.001f))
                return false;
        }
        return true;
    }

    const RigFingerprint* ResolveRig(const M2Data& data) {
        for (const auto& rig : kRigs)
            if (Matches(data, rig))
                return &rig;
        return nullptr;
    }

    const char* RigMismatch(const M2Data& data, const RigFingerprint& rig, uint32_t& boneIndex) {
        if (data.m_bones.m_count != rig.bones) return "bone count";
        if (data.m_vertices.m_count != rig.vertices) return "vertex count";
        for (uint32_t i = 0; i < rig.count; ++i) {
            const auto& expected = rig.checks[i];
            boneIndex = expected.index;
            if (expected.index >= data.m_bones.m_count) return "bone index";
            const auto& actual = data.m_bones[expected.index];
            if (actual.m_parentIndex != static_cast<uint16_t>(expected.parent)) return "bone parent";
            if (actual.m_boneNameCRC != expected.crc) return "bone name";
            if (!(C3Vector::Distance(actual.m_pivot,
                C3Vector(expected.pivot[0], expected.pivot[1], expected.pivot[2])) <= 0.001f))
                return "bone pivot";
        }
        return "none";
    }

    bool PreviewLegSolve(const C3Vector& hip, const C3Vector& knee, const C3Vector& ankle,
        const C3Vector& target, C3Vector& solvedKnee, C3Vector& solvedAnkle) {
        const float upper = C3Vector::Distance(hip, knee);
        const float lower = C3Vector::Distance(knee, ankle);
        const C3Vector reach = target - hip;
        const float distance = std::sqrt(reach.SquaredMag());
        if (!(upper > 0.01f && lower > 0.01f && distance > 0.001f))
            return false;
        const C3Vector direction = reach * (1.0f / distance);
        const C3Vector bend = knee - hip;
        const float projected = bend.x * direction.x + bend.y * direction.y + bend.z * direction.z;
        const C3Vector pole = bend - direction * projected;
        const float poleLength = std::sqrt(pole.SquaredMag());
        if (!(poleLength > 0.001f))
            return false;
        const float length = std::clamp(distance, std::fabs(upper - lower) + 0.001f, upper + lower - 0.001f);
        const float along = (upper * upper - lower * lower + length * length) / (2.0f * length);
        const float side = std::sqrt(std::max(0.0f, upper * upper - along * along));
        solvedKnee = hip + direction * along + pole * (side / poleLength);
        solvedAnkle = hip + direction * length;
        return true;
    }

    bool JointTransform(const C3Vector& origin, const C3Vector& destination,
        C3Vector from, C3Vector to, C44Matrix& matrix) {
        from.NormalizeSafe();
        to.NormalizeSafe();
        const float dot = std::clamp(from.x * to.x + from.y * to.y + from.z * to.z, -1.0f, 1.0f);
        if (!(dot > 0.5f))
            return false;
        C3Vector axis = C3Vector::Cross(from, to);
        matrix.Identity();
        if (axis.SquaredMag() > 0.000001f) {
            axis.Normalize();
            C4Quaternion rotation;
            rotation.FromAngleAxis(std::acos(dot), axis);
            matrix = C44Matrix(rotation);
        }
        const C3Vector shift = destination - origin * matrix;
        matrix.d0 = shift.x;
        matrix.d1 = shift.y;
        matrix.d2 = shift.z;
        return true;
    }

    void ApplyFootPlacement(CM2Model& model) {
        const auto& sample = model.m_footPlacement;
        const auto& data = *model.m_shared->m_data;
        const auto& view = model.m_scene->m_view;
        const auto jointPosition = [&](uint32_t index) {
            return data.m_bones[index].m_pivot * model.m_boneMatrices[index];
        };
        C3Vector bodyShift(0, 0, 0);
        if (sample.pelvis < data.m_bones.m_count && sample.valid[0] && sample.valid[1]) {
            float drop = 0.0f;
            for (int foot = 0; foot < 2; ++foot) {
                const auto& chain = sample.legs[foot];
                const C3Vector hip = jointPosition(chain[0]);
                const C3Vector knee = jointPosition(chain[1]);
                const C3Vector ankle = jointPosition(chain[2]);
                const C3Vector target = sample.targets[foot] * view;
                const float reach = C3Vector::Distance(hip, knee) + C3Vector::Distance(knee, ankle);
                if (sample.targets[foot].z < (ankle * model.m_scene->m_viewInv).z)
                    drop = std::max(drop, C3Vector::Distance(hip, target) - reach);
            }
            const C3Vector origin = C3Vector(0, 0, 0) * view;
            bodyShift = C3Vector(0, 0, -std::clamp(drop, 0.0f, 0.12f)) * view - origin;
        }
        const bool lowerPelvis = bodyShift.SquaredMag() > 0.000001f;
        C44Matrix transforms[2][3];
        bool solved[2] = {};
        for (int foot = 0; foot < 2; ++foot) {
            if (!sample.valid[foot])
                continue;
            const auto& chain = sample.legs[foot];
            C3Vector joints[3];
            for (int joint = 0; joint < 3; ++joint)
                joints[joint] = jointPosition(chain[joint]) + bodyShift;
            C3Vector worldTarget = sample.targets[foot];
            if (sample.moving) {
                const C3Vector current = joints[2] * model.m_scene->m_viewInv;
                const float dx = current.x - worldTarget.x;
                const float dy = current.y - worldTarget.y;
                if (!(dx * dx + dy * dy <= 0.0625f) || !(std::fabs(current.z - worldTarget.z) <= 0.18f))
                    continue;
                worldTarget.x = current.x;
                worldTarget.y = current.y;
            }
            const C3Vector target = worldTarget * model.m_scene->m_view;
            const C3Vector adjustment = target - joints[2];
            if (!(adjustment.SquaredMag() <= 0.09f))
                continue;
            C3Vector knee, ankle;
            if (!PreviewLegSolve(joints[0], joints[1], joints[2], target, knee, ankle))
                continue;
            auto& leg = transforms[foot];
            if (!JointTransform(joints[0], joints[0], joints[1] - joints[0], knee - joints[0], leg[0]) ||
                !JointTransform(joints[1], knee, joints[2] - joints[1], ankle - knee, leg[1]))
                continue;
            leg[2].Identity();
            leg[2].Translate(ankle - joints[2]);
            if (sample.normals[foot].SquaredMag() > 0.0f) {
                const auto& view = model.m_scene->m_view;
                const C3Vector viewOrigin = C3Vector(0, 0, 0) * view;
                const C3Vector up = C3Vector(0, 0, 1) * view - viewOrigin;
                const C3Vector normal = sample.normals[foot] * view - viewOrigin;
                C44Matrix tilt;
                if (JointTransform(joints[2], ankle, up, normal, tilt))
                    leg[2] = tilt;
            }
            solved[foot] = true;
        }
        if ((!solved[0] && !solved[1]) || (lowerPelvis && (!solved[0] || !solved[1])))
            return;
        C44Matrix translation;
        translation.Identity();
        translation.Translate(bodyShift);
        for (uint32_t bone = 0; bone < data.m_bones.m_count; ++bone) {
            uint32_t parent = bone;
            const C44Matrix* correction = nullptr;
            bool pelvisChild = false;
            while (parent < data.m_bones.m_count) {
                if (lowerPelvis && parent == sample.pelvis)
                    pelvisChild = true;
                for (int foot = 0; !correction && foot < 2; ++foot) {
                    if (!solved[foot])
                        continue;
                    const auto& chain = sample.legs[foot];
                    const int joint = parent == chain[2] ? 2 : parent == chain[1] ? 1 : parent == chain[0] ? 0 : -1;
                    if (joint >= 0)
                        correction = &transforms[foot][joint];
                }
                const uint32_t next = data.m_bones[parent].m_parentIndex;
                if (next >= parent)
                    break;
                parent = next;
            }
            if (pelvisChild || (lowerPelvis && correction))
                model.m_boneMatrices[bone] *= translation;
            if (correction)
                model.m_boneMatrices[bone] *= *correction;
        }
    }

    bool& footPlacementEnabled = userSettings.footPlacementEnabled;
    bool& movingFootPlacementEnabled = userSettings.movingFootPlacementEnabled;
    bool& slopeAlignmentEnabled = userSettings.slopeAlignmentEnabled;
    bool& pelvisCompensationEnabled = userSettings.pelvisCompensationEnabled;
    bool& footprintProbesEnabled = userSettings.footprintProbesEnabled;

    C3Vector SampleSlope(const C3Vector& ankle, float groundZ, float scale) {
        const float spacing = std::clamp(0.15f * scale, 0.08f, 0.25f);
        const C3Vector offsets[] = { C3Vector(spacing, 0, 0), C3Vector(0, spacing, 0) };
        float rise[2];
        for (int axis = 0; axis < 2; ++axis) {
            const C3Vector point = ankle + offsets[axis];
            C3Vector hit;
            float fraction = 1.0f;
            if (!World::Intersect(point + C3Vector(0, 0, 0.4f), point - C3Vector(0, 0, 1.0f),
                &hit, &fraction, 0x01000124, nullptr))
                return C3Vector(0, 0, 0);
            rise[axis] = (hit.z - groundZ) / spacing;
        }
        // A step or ledge is not a surface to tilt into.
        if (!(rise[0] * rise[0] + rise[1] * rise[1] <= 0.25f))
            return C3Vector(0, 0, 0);
        C3Vector normal(-rise[0], -rise[1], 1.0f);
        normal.Normalize();
        return normal;
    }

    float SoleCorrection(const CM2Model& model, const RigFingerprint& rig, int foot,
        const C3Vector& ground, const C3Vector& normal, const C3Vector& ankle, float fallback, bool probeFootprint) {
        const auto& data = *model.m_shared->m_data;
        float lift = -std::numeric_limits<float>::infinity();
        C3Vector edges[4];
        for (uint32_t i = 0; i < rig.soleCounts[foot]; ++i) {
            const auto& vertex = rig.soles[foot][i];
            const C3Vector point(vertex.point[0], vertex.point[1], vertex.point[2]);
            C3Vector skinned(0, 0, 0);
            uint32_t weight = 0;
            for (int influence = 0; influence < 4; ++influence) {
                const uint32_t amount = vertex.weights[influence];
                if (!amount)
                    continue;
                const uint32_t bone = vertex.bones[influence];
                if (bone >= data.m_bones.m_count)
                    return fallback;
                skinned += (point * model.m_boneMatrices[bone]) * (amount / 255.0f);
                weight += amount;
            }
            if (weight != 255)
                return fallback;
            const C3Vector world = skinned * model.m_scene->m_viewInv;
            if (!(C3Vector::Distance(world, ankle) < 1.5f))
                return fallback;
            if (probeFootprint) {
                if (!i || world.x < edges[0].x) edges[0] = world;
                if (!i || world.x > edges[1].x) edges[1] = world;
                if (!i || world.y < edges[2].y) edges[2] = world;
                if (!i || world.y > edges[3].y) edges[3] = world;
            }
            float plane = ground.z;
            if (normal.z > 0.8f)
                plane -= (normal.x * (world.x - ground.x) + normal.y * (world.y - ground.y)) / normal.z;
            lift = std::max(lift, plane - world.z);
        }
        if (probeFootprint && rig.soleCounts[foot]) {
            float sampledLift = -std::numeric_limits<float>::infinity();
            for (const auto& edge : edges) {
                C3Vector hit;
                float fraction = 1.0f;
                if (World::Intersect(edge + C3Vector(0, 0, 0.4f), edge - C3Vector(0, 0, 0.6f),
                    &hit, &fraction, 0x01000124, nullptr) && std::isfinite(hit.z) &&
                    std::fabs(hit.z - ground.z) <= 0.25f) {
                    sampledLift = std::max(sampledLift, hit.z - edge.z);
                }
            }
            // Retain the plane estimate for the unprobed parts of the sole.
            lift = std::max(lift, sampledLift);
        }
        return std::isfinite(lift) ? lift : fallback;
    }

    class FootPlacementScript : public ModelScript {
    public:
        FootPlacementScript() : ModelScript("ActionCameraFootPlacement") {}
        void OnModelAnimated(CM2Model& model) override {
            model.m_applyFootPlacement = nullptr;
            if (!footPlacementEnabled || !CGWorldFrame::GetActiveCamera())
                return;
            auto* player = ClntObjMgrGetActivePlayerObj();
            constexpr uint32_t movement = MOVEMENTFLAG_FORWARD | MOVEMENTFLAG_BACKWARD |
                MOVEMENTFLAG_STRAFE_LEFT | MOVEMENTFLAG_STRAFE_RIGHT | MOVEMENTFLAG_TURN_LEFT |
                MOVEMENTFLAG_TURN_RIGHT;
            constexpr uint32_t excluded = MOVEMENTFLAG_ONTRANSPORT | MOVEMENTFLAG_FALLING |
                MOVEMENTFLAG_FALLING_FAR | MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_FLYING | MOVEMENTFLAG_HOVER |
                MOVEMENTFLAG_SPLINE_ENABLED | MOVEMENTFLAG_ASCENDING | MOVEMENTFLAG_DESCENDING;
            if (!player || player->GetObjectModel() != &model || !player->m_movement ||
                player->IsMounted() || player->IsDeadOrGhost() || (player->m_movement->m_flags & excluded) ||
                !model.m_scene || !model.m_boneMatrices || !model.m_shared || !model.m_shared->m_data ||
                model.m_frameStamp != model.m_scene->m_frameStamp)
                return;
            const bool moving = (player->m_movement->m_flags & movement) != 0;
            if (moving && !movingFootPlacementEnabled)
                return;
            const auto& data = *model.m_shared->m_data;
            const auto* rig = ResolveRig(data);
            if (!rig)
                return;
            auto& sample = model.m_footPlacement;
            sample.frame = model.m_scene->m_frameStamp + 1;
            sample.moving = moving;
            sample.pelvis = pelvisCompensationEnabled && !moving ? rig->pelvis : UINT32_MAX;
            for (int foot = 0; foot < 2; ++foot) {
                sample.valid[foot] = false;
                const auto& chain = rig->legs[foot];
                if (chain[0] >= data.m_bones.m_count || chain[1] >= data.m_bones.m_count ||
                    chain[2] >= data.m_bones.m_count)
                    continue;
                const uint32_t ankle = chain[2];
                const C3Vector position =
                    data.m_bones[ankle].m_pivot * model.m_boneMatrices[ankle] * model.m_scene->m_viewInv;
                if (!(C3Vector::Distance(position, player->GetPosition()) < 5.0f))
                    continue;
                C3Vector hit;
                float fraction = 1.0f;
                if (!World::Intersect(position + C3Vector(0, 0, 0.4f), position - C3Vector(0, 0, 1.0f),
                    &hit, &fraction, 0x01000124, nullptr))
                    continue;
                const float scale = player->GetScale();
                if (!(scale > 0.0f))
                    continue;
                const C3Vector normal = slopeAlignmentEnabled && !moving
                    ? SampleSlope(position, hit.z, scale) : C3Vector(0, 0, 0);
                const float fallback = hit.z + rig->soleClearance * scale - position.z;
                float correction = SoleCorrection(model, *rig, foot, hit, normal, position, fallback,
                    footprintProbesEnabled && !moving);
                if (!(std::fabs(correction) <= 0.25f))
                    continue;
                if (moving) {
                    // Fade out the correction as the animated foot rises into its swing.
                    const float weight = std::clamp(1.0f + correction / (0.18f * scale), 0.0f, 1.0f);
                    if (weight <= 0.0f)
                        continue;
                    correction = std::clamp(correction * weight * weight * (3.0f - 2.0f * weight), -0.18f, 0.18f);
                }
                for (int joint = 0; joint < 3; ++joint)
                    sample.legs[foot][joint] = chain[joint];
                sample.targets[foot] = C3Vector(position.x, position.y, position.z + correction);
                sample.normals[foot] = normal;
                sample.valid[foot] = true;
            }
            if (sample.valid[0] || sample.valid[1])
                model.m_applyFootPlacement = ApplyFootPlacement;
        }
    };
}

void RegisterFootPlacement() { new FootPlacementScript(); }
void EnableFootPlacement(bool enabled) { footPlacementEnabled = enabled; }
void EnableMovingFootPlacement(bool enabled) { movingFootPlacementEnabled = enabled; }
void EnableFootSlopeAlignment(bool enabled) { slopeAlignmentEnabled = enabled; }
void EnableFootPelvisCompensation(bool enabled) { pelvisCompensationEnabled = enabled; }
void EnableFootprintProbes(bool enabled) { footprintProbesEnabled = enabled; }

const char* LocalFootIKRig() {
    static thread_local char diagnostic[128];
    auto* player = CGWorldFrame::GetActiveCamera() ? ClntObjMgrGetActivePlayerObj() : nullptr;
    auto* model = player ? player->GetObjectModel() : nullptr;
    if (!model || !model->m_shared || !(model->m_shared->m_flags & 1) || !model->m_shared->m_data)
        return "Foot IK rig: waiting for player model";
    const auto& data = *model->m_shared->m_data;
    if (const auto* rig = ResolveRig(data))
        return rig->name;
    const RigFingerprint* closest = nullptr;
    double closestScore = std::numeric_limits<double>::infinity();
    for (const auto& candidate : kRigs) {
        const double boneDelta = std::fabs(static_cast<double>(data.m_bones.m_count) - candidate.bones) /
            std::max<uint32_t>(candidate.bones, 1);
        const double vertexDelta = std::fabs(static_cast<double>(data.m_vertices.m_count) - candidate.vertices) /
            std::max<uint32_t>(candidate.vertices, 1);
        const double score = boneDelta + vertexDelta;
        if (score < closestScore) {
            closest = &candidate;
            closestScore = score;
        }
    }
    uint32_t boneIndex = 0;
    const char* mismatch = closest ? RigMismatch(data, *closest, boneIndex) : "no candidates";
    if (closest && (std::strcmp(mismatch, "bone count") == 0 || std::strcmp(mismatch, "vertex count") == 0)) {
        std::snprintf(diagnostic, sizeof(diagnostic), "Unsupported %uB/%uV; closest %s expects %uB/%uV",
            data.m_bones.m_count, data.m_vertices.m_count, closest->name, closest->bones, closest->vertices);
    } else {
        std::snprintf(diagnostic, sizeof(diagnostic), "Unsupported %uB/%uV; closest %s: %s at bone %u",
            data.m_bones.m_count, data.m_vertices.m_count, closest ? closest->name : "none", mismatch, boneIndex);
    }
    return diagnostic;
}

const char* ProbeLocalFootGround() {
    auto* player = CGWorldFrame::GetActiveCamera() ? ClntObjMgrGetActivePlayerObj() : nullptr;
    auto* model = player ? player->GetObjectModel() : nullptr;
    constexpr uint32_t excluded = MOVEMENTFLAG_ONTRANSPORT | MOVEMENTFLAG_FALLING |
        MOVEMENTFLAG_FALLING_FAR | MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_FLYING;
    if (!player || !player->m_movement || player->IsMounted() || player->IsDeadOrGhost() ||
        (player->m_movement->m_flags & excluded))
        return "Stand on solid ground to probe.";
    if (!model || !model->m_scene || !model->m_boneMatrices ||
        model->m_frameStamp != model->m_scene->m_frameStamp || !model->m_shared ||
        !(model->m_shared->m_flags & 1) || !model->m_shared->m_data)
        return "No current player bone palette.";
    const auto& data = *model->m_shared->m_data;
    const auto* rig = ResolveRig(data);
    if (!rig)
        return "No supported Foot IK rig.";

    char sample[2][56]{};
    const C3Vector playerPosition = player->GetPosition();
    for (int foot = 0; foot < 2; ++foot) {
        C3Vector joints[3];
        for (int joint = 0; joint < 3; ++joint) {
            const uint32_t index = rig->legs[foot][joint];
            if (index >= data.m_bones.m_count)
                return "Invalid leg bone index.";
            joints[joint] = data.m_bones[index].m_pivot * model->m_boneMatrices[index] * model->m_scene->m_viewInv;
            if (!(C3Vector::Distance(joints[joint], playerPosition) < 5.0f))
                return "Leg pose is outside the probe envelope.";
        }
        const auto& position = joints[2];
        const C3Vector start(position.x, position.y, position.z + 0.4f);
        const C3Vector end(position.x, position.y, position.z - 1.0f);
        C3Vector hit;
        float fraction = 1.0f;
        if (World::Intersect(start, end, &hit, &fraction, 0x01000124, nullptr) && std::isfinite(hit.z)) {
            const float groundDelta = hit.z - position.z;
            C3Vector target = position;
            // Preview only. This bounded target does not yet account for the boot sole.
            target.z += std::clamp(groundDelta, -0.25f, 0.25f);
            C3Vector solvedKnee, solvedAnkle;
            if (PreviewLegSolve(joints[0], joints[1], position, target, solvedKnee, solvedAnkle)) {
                std::snprintf(sample[foot], sizeof(sample[foot]), "ground %+.3f, ankle %+.3f, knee %+.3f",
                    static_cast<double>(groundDelta), static_cast<double>(solvedAnkle.z - position.z),
                    static_cast<double>(solvedKnee.z - joints[1].z));
            } else {
                std::snprintf(sample[foot], sizeof(sample[foot]), "ground %+.3f, no solve",
                    static_cast<double>(groundDelta));
            }
        } else {
            std::snprintf(sample[foot], sizeof(sample[foot]), "no hit");
        }
    }
    static char result[160];
    std::snprintf(result, sizeof(result), "Left: %s / Right: %s", sample[0], sample[1]);
    return result;
}
}
