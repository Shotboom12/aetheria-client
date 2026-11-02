// Review candidate: numeric field encoding, no artwork modification or GPU submission.
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdForeverWaterImages.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <utility>

namespace HD::ForeverNative {
inline uint16_t FieldHalf(float value) {
    uint32_t bits; std::memcpy(&bits, &value, sizeof bits);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int exponent = int((bits >> 23) & 255u) - 127 + 15;
    // Matches the existing inferred-wave adapter's finite/truncated half policy.
    if (exponent <= 0) return uint16_t(sign);
    if (exponent >= 31) return uint16_t(sign | 0x7bffu);
    return uint16_t(sign | (uint32_t(exponent) << 10) | ((bits & 0x7fffffu) >> 13));
}
inline bool EncodeField(uint32_t width, uint32_t height, const std::vector<float>& rgba,
    bool mipChain, std::vector<FloatMip>& output) {
    output.clear();
    if (!width || !height || width > 512 || height > 512 || rgba.size() != size_t(width) * height * 4) return false;
    for (float value : rgba) if (!std::isfinite(value) || std::fabs(value) > 65504.0f) return false;
    std::vector<float> current = rgba;
    size_t levelIndex = 0;
    for (;;) {
        if (levelIndex == output.size()) output.emplace_back();
        auto& level = output[levelIndex++];
        level.width = width; level.height = height; level.rgba.resize(current.size());
        for (size_t i = 0; i < current.size(); ++i) level.rgba[i] = FieldHalf(current[i]);
        if (!mipChain || (width == 1 && height == 1)) break;
        const auto w = std::max(1u, width / 2), h = std::max(1u, height / 2);
        std::vector<float> next(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) for (uint32_t c = 0; c < 4; ++c) {
            float sum = 0;
            for (uint32_t dy = 0; dy < 2; ++dy) for (uint32_t dx = 0; dx < 2; ++dx)
                sum += current[(size_t(std::min(height-1, y*2+dy)) * width + std::min(width-1, x*2+dx)) * 4 + c];
            next[(size_t(y) * w + x) * 4 + c] = sum * 0.25f;
        }
        current.swap(next); width = w; height = h;
    }
    output.resize(levelIndex);
    return true;
}
}
