#pragma once
#include <cstdio>
#include <cstring>
#include <vector>
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/ShaderEffect/ShaderEffect.h"
#include "Engine/Source/ShaderEffect/ShaderEffectManager.h"

// The valid DX9 COMMENT identifies the paired offline Vulkan adaptation. Original
// asset bytes and stock shader modules are unchanged. See build_void_shaders.py.
inline CShaderEffect* M2RetailDissolveEffect(CShaderEffect* base) {
    if (!base || !g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi() != GxApi_Vulkan) return base;
    char name[96]; std::snprintf(name, sizeof(name), "Retail_Dissolve2450_%p", static_cast<void*>(base));
    if (auto* existing = CShaderEffectManager::GetEffect(name)) return existing;
    for (auto* pixel : base->m_pixelShaders) {
        if (!pixel || pixel->m_code.m_count < 8) return base;
        uint32_t end; std::memcpy(&end, pixel->m_code.m_data + pixel->m_code.m_count - 4, 4);
        if (end != 0xffff) return base;
    }
    auto* result = CShaderEffectManager::CreateEffect(name);
    result->m_fixedFuncPassCount = base->m_fixedFuncPassCount;
    std::memcpy(result->m_colorOps, base->m_colorOps, sizeof(base->m_colorOps));
    std::memcpy(result->m_alphaOps, base->m_alphaOps, sizeof(base->m_alphaOps));
    for (unsigned i = 0; i < 90; ++i) {
        result->m_vertexShaders[i] = base->m_vertexShaders[i];
        if (result->m_vertexShaders[i]) ++result->m_vertexShaders[i]->m_refCount;
    }
    static constexpr char tag[] = "ESTERIA_NATIVE_DISSOLVE_2450_V1";
    constexpr uint32_t words = (sizeof(tag) + 3) / 4;
    for (unsigned i = 0; i < 16; ++i) {
        auto* original = base->m_pixelShaders[i];
        const size_t count = original->m_code.m_count - 4;
        std::vector<uint8_t> code(count + 4 + words * 4 + 4, 0);
        std::memcpy(code.data(), original->m_code.m_data, count);
        const uint32_t comment = (words << 16) | 0xfffe, end = 0xffff;
        std::memcpy(code.data() + count, &comment, 4);
        std::memcpy(code.data() + count + 4, tag, sizeof(tag));
        std::memcpy(code.data() + code.size() - 4, &end, 4);
        char shaderName[128]; std::snprintf(shaderName, sizeof(shaderName), "%s:%u", name, i);
        auto* shader = g_theGxDevicePtr->m_shaderList[GxSh_Pixel].New(shaderName, 0, 0);
        shader->m_refCount = 1; shader->m_target = GxSh_Pixel;
        shader->m_code.SetCount(static_cast<uint32_t>(code.size()));
        std::memcpy(shader->m_code.m_data, code.data(), code.size());
        result->m_pixelShaders[i] = shader;
    }
    return result;
}
