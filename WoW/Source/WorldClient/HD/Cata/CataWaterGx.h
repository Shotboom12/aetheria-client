// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CataWater.h"
class CGxShader;
namespace HD::CataWater::Gx {
// Render thread only, with a live Vulkan device. Engine hash-table ownership, not raw new/delete.
// The caller keeps the returned reference and releases it before the device is destroyed.
CGxShader* Acquire(ShaderKind kind, const char* family, uint32_t permutation,
                   const std::vector<uint8_t>& bytecode, std::string& error);
void Release(CGxShader*& shader);
// Candidate bridge, compiled against the actual client headers. Does not bind textures or submit a draw.
// The caller must finish geometry, lighting, fog, screen/depth constants, descriptors and all per-instance
// transforms BEFORE this call. Pixel high constants are uploaded LAST to avoid bone/VS overwrites.
bool BindAndUpload(CGxShader* vertex, CGxShader* pixel, const MaterialConstants& constants,
                   const PreparedInputs& inputs, const FrameKey& current, std::string& error);
}
