// Reflection/oblique-clip math adapted from Azeroth Remastered CataMirror.cpp.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdMirrorMath.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace gxvk::vk::hdmirror {
namespace {
constexpr double kClipBias=0.05;
bool Invert(const double m[16], double out[16]) {
    double a[4][8];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            a[r][c] = m[r * 4 + c];
            a[r][c + 4] = r == c ? 1.0 : 0.0;
        }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int r = col + 1; r < 4; ++r)
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
        if (std::fabs(a[pivot][col]) < 1e-30) return false;
        for (int c = 0; c < 8; ++c) std::swap(a[col][c], a[pivot][c]);
        const double inv = 1.0 / a[col][col];
        for (int c = 0; c < 8; ++c) a[col][c] *= inv;
        for (int r = 0; r < 4; ++r) {
            if (r == col) continue;
            const double f = a[r][col];
            for (int c = 0; c < 8; ++c) a[r][c] -= f * a[col][c];
        }
    }
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[r * 4 + c] = a[r][c + 4];
    return true;
}

void Mul(const double a[16], const double b[16], double out[16]) {
    double t[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            double s = 0.0;
            for (int k = 0; k < 4; ++k) s += a[r * 4 + k] * b[k * 4 + c];
            t[r * 4 + c] = s;
        }
    std::memcpy(out, t, sizeof t);
}
}
bool ClipTransform(const float view[16], const float projection[16], float height, float eyeZ, float out[16]) {
    if (!view || !projection || !out || !std::isfinite(height) || !std::isfinite(eyeZ)) return false;
    float result[16];
    double V[16], P[16];
    for (int i = 0; i < 16; ++i) {
        V[i] = view[i];
        P[i] = projection[i];
        if (!std::isfinite(V[i]) || !std::isfinite(P[i])) return false;
    }
    // The world's up in view space: row 2 of the view's rotation (e_z times the matrix).
    double n[3] = {V[8], V[9], V[10]};
    const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (!(len > 1e-6)) return false;
    for (double& x : n) x /= len;
    const double dh = static_cast<double>(eyeZ) - height;   // the eye above the water
    if (!(dh > kClipBias * 2.0)) return false;
    // p' = p - 2 (p.n + dh) n
    double R[16] = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) R[i * 4 + j] = (i == j ? 1.0 : 0.0) - 2.0 * n[i] * n[j];
    for (int j = 0; j < 3; ++j) R[12 + j] = -2.0 * dh * n[j];
    R[15] = 1.0;
    // Keep the mirrored points at or under the plane (the world above the water): -p'.n - dh + bias >= 0.
    const double C[4] = {-n[0], -n[1], -n[2], kClipBias - dh};
    double iP[16];
    if (!Invert(P, iP)) return false;
    double Cc[4];   // the plane in clip space: inverse(P) C
    for (int r = 0; r < 4; ++r) Cc[r] = iP[r * 4 + 0] * C[0] + iP[r * 4 + 1] * C[1] + iP[r * 4 + 2] * C[2] + iP[r * 4 + 3] * C[3];
    const double qc[4] = {Cc[0] >= 0.0 ? 1.0 : -1.0, Cc[1] >= 0.0 ? 1.0 : -1.0, 1.0, 1.0};
    double q[4];    // the far corner opposite the plane, in view space: qc inverse(P)
    for (int c = 0; c < 4; ++c) q[c] = qc[0] * iP[0 * 4 + c] + qc[1] * iP[1 * 4 + c] + qc[2] * iP[2 * 4 + c] + qc[3] * iP[3 * 4 + c];
    const double qw = q[0] * P[0 * 4 + 3] + q[1] * P[1 * 4 + 3] + q[2] * P[2 * 4 + 3] + q[3] * P[3 * 4 + 3];
    const double qC = q[0] * C[0] + q[1] * C[1] + q[2] * C[2] + q[3] * C[3];
    if (!(std::fabs(qC) > 1e-12)) return false;
    double O[16];
    std::memcpy(O, P, sizeof O);
    for (int r = 0; r < 4; ++r) O[r * 4 + 2] = C[r] * (qw / qC);
    double M[16];
    Mul(iP, R, M);
    Mul(M, O, M);
    for (int i = 0; i < 16; ++i) {
        result[i] = static_cast<float>(M[i]);
        if (!std::isfinite(result[i])) return false;
    }
    std::memcpy(out, result, sizeof result);
    return true;
}
}
