#ifndef WOW_SOURCE_UIUTIL_MOUNTCAMERAPOLICY_H
#define WOW_SOURCE_UIUTIL_MOUNTCAMERAPOLICY_H

#include <algorithm>
#include <cmath>

namespace MountCameraPolicy {
    struct Profile {
        float horizontal;
        float vertical;
        float fovDegrees;
        float followMs;
        float settleMs;
        float maximumDrift;
        float forwardDrift;
        float verticalDrift;
        float movingFovDegrees;
        float movingFovEaseMs;
        float turnDeadzone;
        float turnIntensity;
        float turnMaximum;
        float turnEaseMs;
        float turnReleaseMs;
    };

    // Values are copied from the 1.0.4.2 per-race profile set. The profiles
    // are identical across the supported races; this keeps them as tuning
    // references while the behavior runs in Aetheria's native x64 code.
    inline constexpr Profile Ground{
        -1.01f, 0.35f, 70.0f, 1195.0f, 1053.0f, 8.0f, 6.0f, 0.0f,
        6.0f, 5000.0f, 15.0f, 15.0f, 22.0f, 260.0f, 450.0f
    };
    inline constexpr Profile Flying{
        0.0f, -0.05f, 65.0f, 2500.0f, 3000.0f, 12.0f, 12.0f, 12.0f,
        20.0f, 1622.0f, 11.0f, 19.0f, 40.0f, 392.0f, 905.0f
    };
    inline constexpr float MountTransitionMs = 881.0f;
    inline constexpr float FlightBankMaximum = 40.0f;
    inline constexpr float FlightBankEaseMs = 303.0f;
    inline constexpr float FlightBankReleaseMs = 1075.0f;

    inline float Clamp(float value, float low, float high) {
        return std::max(low, std::min(value, high));
    }

    inline float Blend(float current, float target, float dt, float durationMs) {
        if (!std::isfinite(dt) || dt <= 0.0f) return current;
        const float weight = durationMs > 0.0f
            ? 1.0f - std::exp(-dt * 1000.0f / durationMs)
            : 1.0f;
        return current + (target - current) * weight;
    }

    struct TurningLean {
        bool ready = false;
        float previousYaw = 0.0f;
        float rate = 0.0f;
        float value = 0.0f;

        void Reset() { *this = TurningLean{}; }

        float Step(float yaw, bool eligible, float dt, const Profile& profile) {
            if (!std::isfinite(yaw) || !std::isfinite(dt) || dt <= 0.0f) return value;
            if (!ready) {
                ready = true;
                previousYaw = yaw;
                return value;
            }

            const float change = std::remainder(yaw - previousYaw, 6.28318530718f);
            previousYaw = yaw;
            const bool discontinuity = dt > 0.1f || std::fabs(change) > 0.8f;
            dt = Clamp(dt, 0.0f, 0.1f);
            if (!eligible || discontinuity) {
                rate = 0.0f;
            } else {
                const float measured = Clamp(change / dt * 57.2957795131f, -720.0f, 720.0f);
                rate += (measured - rate) * (1.0f - std::exp(-dt / 0.08f));
            }

            const float magnitude = std::max(0.0f, std::fabs(rate) - profile.turnDeadzone) *
                profile.turnIntensity / 100.0f;
            const float goal = eligible && !discontinuity
                ? Clamp(rate > 0.0f ? -magnitude : magnitude, -profile.turnMaximum, profile.turnMaximum)
                : 0.0f;
            value = Blend(value, goal, dt, goal == 0.0f ? profile.turnReleaseMs : profile.turnEaseMs);
            if (std::fabs(value) < 0.0001f && goal == 0.0f) value = 0.0f;
            return value;
        }
    };

    struct FlightBank {
        float value = 0.0f;
        float velocity = 0.0f;

        void Reset() { value = velocity = 0.0f; }

        float Step(int side, bool enabled, float dt) {
            if (!std::isfinite(dt) || dt <= 0.0f) return value;
            dt = Clamp(dt, 0.0f, 0.1f);
            const float goal = enabled ? (side > 0 ? FlightBankMaximum : side < 0 ? -FlightBankMaximum : 0.0f) : 0.0f;
            const float duration = goal == 0.0f ? FlightBankReleaseMs : FlightBankEaseMs;
            const float omega = 4744.0f / duration;
            const float x = value - goal;
            const float c = velocity + omega * x;
            const float e = std::exp(-omega * dt);
            value = goal + (x + c * dt) * e;
            velocity = (velocity - omega * c * dt) * e;
            value = Clamp(value, -FlightBankMaximum, FlightBankMaximum);
            if (std::fabs(value - goal) < 0.00001f && std::fabs(velocity) < 0.0001f) {
                value = goal;
                velocity = 0.0f;
            }
            return value;
        }
    };

    // Applies roll to a fresh native mount transform. Translation and the
    // forward axis are preserved, so the result never accumulates frame over
    // frame and never mutates shared M2 data.
    inline void ApplyBank(float* matrix, float degrees) {
        if (!matrix || !std::isfinite(degrees) || std::fabs(degrees) < 0.001f) return;
        const float angle = degrees * 0.0174532925199f;
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        float original[16];
        std::copy(matrix, matrix + 16, original);
        for (int axis = 0; axis < 3; ++axis) {
            matrix[4 + axis] = cosine * original[4 + axis] + sine * original[8 + axis];
            matrix[8 + axis] = -sine * original[4 + axis] + cosine * original[8 + axis];
        }
    }
}

#endif
