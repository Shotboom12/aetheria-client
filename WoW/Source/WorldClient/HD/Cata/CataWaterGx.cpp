// SPDX-License-Identifier: GPL-3.0-or-later
// HD: candidate glue for the 3.3.5a x64 stage 2.5 source. Not installed or enabled by this kit.
#include "CataWaterGx.h"
#include <cstdio>
#include <cstring>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/Types.h"
#include "WoW/Source/WorldClient/HD/HdGxShader.h"

namespace HD::CataWater::Gx {
CGxShader* Acquire(ShaderKind kind, const char* family, uint32_t permutation,
                   const std::vector<uint8_t>& code, std::string& error) {
    if (!HD::VulkanDevice()) { error = "Cata bridge needs the live Vulkan device"; return nullptr; }
    if (!family || std::strncmp(family, "Cata", 4) || std::strlen(family) > 40 || permutation > 255) {
        error = "expected a bounded Cata shader family/permutation"; return nullptr;
    }
    for (const char* c = family; *c; ++c) {
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9'))) {
            error = "invalid Cata shader family"; return nullptr;
        }
    }
    if (!ValidateBytecode(code, kind, error)) return nullptr;
    if (!HD::ShaderKnown(code.data(), code.size())) {
        error = "Cata bytecode has no translated manifest entry"; return nullptr;
    }
    const EGxShTarget target = kind == ShaderKind::Vertex ? GxSh_Vertex : GxSh_Pixel;
    char name[64]; std::snprintf(name, sizeof name, "%s:%u", family, permutation);
    auto* device = g_theGxDevicePtr;
    auto* shader = device->m_shaderList[target].Ptr(name);
    if (shader) {
        if (shader->m_code.Count() != code.size() || std::memcmp(shader->m_code.m_data, code.data(), code.size())) {
            error = "shader name already exists with different bytecode"; return nullptr;
        }
        if (!shader->Valid()) { error = "existing Cata shader is invalid"; return nullptr; }
        ++shader->m_refCount; error.clear(); return shader;
    }
    // New sets the hash key used by wxl's reader/translated registry. An unnamed CGxShader is unusable here.
    shader = device->m_shaderList[target].New(name, 0, 0);
    if (!shader) { error = "shader allocation failed"; return nullptr; }
    shader->m_refCount = 1; shader->m_target = target;
    shader->unk_003C = 0; shader->unk_0040 = 0; shader->unk_0044 = 0; shader->unk_0046 = 0;
    shader->m_code.SetCount(static_cast<uint32_t>(code.size()));
    std::memcpy(shader->m_code.m_data, code.data(), code.size());
    if (!shader->Valid()) {
        device->ShaderDestroy(shader); error = "device rejected translated Cata shader"; return nullptr;
    }
    error.clear(); return shader;
}
void Release(CGxShader*& shader) {
    if (shader && g_theGxDevicePtr) g_theGxDevicePtr->ShaderDestroy(shader);
}
bool BindAndUpload(CGxShader* vertex, CGxShader* pixel, const MaterialConstants& constants,
                   const PreparedInputs& inputs, const FrameKey& now, std::string& error) {
    if (!HD::VulkanDevice()) { error = "Cata bridge needs Vulkan"; return false; }
    if (!CanDraw(inputs, now, error)) return false;
    if (!vertex || !pixel || vertex->m_target != GxSh_Vertex || pixel->m_target != GxSh_Pixel ||
        !vertex->Valid() || !pixel->Valid()) { error = "Cata shader pair invalid"; return false; }
    static_assert(sizeof(Vec4) == 4 * sizeof(float), "shader constants must be packed rows");
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, vertex);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, pixel);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(constants.vertex.data()), 53);
    HD::PixelConstantsSet(0, reinterpret_cast<const float*>(constants.pixel.data()), 32);
    error.clear(); return true;
}
}
