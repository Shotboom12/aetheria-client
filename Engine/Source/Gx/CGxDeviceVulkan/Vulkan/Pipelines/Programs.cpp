// The shader modules: the backend's own (the SPIR-V of Shaders/*.vert and *.frag, embedded by
// Shaders/Compile.py) and the translated ones, fetched from the manifest's files.
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Pipelines/Internal.hpp"

#include "Engine/Source/Gx/CGxDeviceVulkan/Shaders/Spirv.inc"
#include <cstdio>
#include <cstring>

namespace
{
    using namespace gxvk::vk::pipelines::detail;

    bool CreateModule(const uint32_t* words, size_t bytes, VkShaderModule& out, const char* what)
    {
        VkShaderModuleCreateInfo info = {};
        info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = bytes;
        info.pCode    = words;
        const VkResult r = s.dapi->CreateShaderModule(s.device, &info, nullptr, &out);
        if (r != VK_SUCCESS)
        {
            out = VK_NULL_HANDLE;
            Write(Error, "vulkan: pipelines: vkCreateShaderModule (%s) failed: %s", what, ResultName(r));
            return false;
        }
        return true;
    }

    /// A translated record's module, by its manifest path.
    VkShaderModule RecordModule(uint16_t index)
    {
        const manifest::Record* r = manifest::At(index);
        return r ? shaders::Get(r->spv) : VK_NULL_HANDLE;
    }
}

namespace gxvk::vk::pipelines::detail
{
    bool CreatePrograms()
    {
        return CreateModule(kFfVert, sizeof kFfVert, s.vert[static_cast<uint32_t>(VertexProgram::FixedFunction)], "Ff.vert")
            && CreateModule(kUiVert, sizeof kUiVert, s.vert[static_cast<uint32_t>(VertexProgram::Ui)], "Ui.vert")
            && CreateModule(kFfSemVert, sizeof kFfSemVert, s.vert[static_cast<uint32_t>(VertexProgram::FixedFunctionSemantic)], "FfSem.vert")
            && CreateModule(kFfFrag, sizeof kFfFrag, s.frag[static_cast<uint32_t>(FragmentProgram::FixedFunction)], "Ff.frag")
            && CreateModule(kUiFrag, sizeof kUiFrag, s.frag[static_cast<uint32_t>(FragmentProgram::Ui)], "Ui.frag")
            && CreateModule(kDesaturateFrag, sizeof kDesaturateFrag, s.frag[static_cast<uint32_t>(FragmentProgram::Desaturate)], "Desaturate.frag")
            && CreateModule(kFfTranslatedFrag, sizeof kFfTranslatedFrag, s.frag[static_cast<uint32_t>(FragmentProgram::FixedFunctionTranslated)], "FfTranslated.frag")
            && CreateModule(kOutlineEdgeFrag, sizeof kOutlineEdgeFrag, s.frag[static_cast<uint32_t>(FragmentProgram::OutlineEdge)], "OutlineEdge.frag");
    }

    void DestroyPrograms()
    {
        for (VkShaderModule& m : s.vert)
        {
            if (m != VK_NULL_HANDLE) s.dapi->DestroyShaderModule(s.device, m, nullptr);
            m = VK_NULL_HANDLE;
        }
        for (VkShaderModule& m : s.frag)
        {
            if (m != VK_NULL_HANDLE) s.dapi->DestroyShaderModule(s.device, m, nullptr);
            m = VK_NULL_HANDLE;
        }
    }

    // The staged geometry assets live separately from every original/mirror module.
    // Callers select geometry=2 only when BOTH stages have supplied variants.
    VkShaderModule GeometryModule(uint16_t index)
    {
        const auto* record=manifest::At(index);
        if(!record) return VK_NULL_HANDLE;
        const size_t len=std::strlen(record->spv);
        if(len<4 || std::strcmp(record->spv+len-4,".spv")) return VK_NULL_HANDLE;
        char path[96];
        const int n=std::snprintf(path,sizeof path,"gbuffer/%.*s.gbuf.spv",static_cast<int>(len-4),record->spv);
        return n>0 && n<static_cast<int>(sizeof path)?shaders::Get(path):VK_NULL_HANDLE;
    }

    /// The vertex module of a key: the backend's own for its program, the record's for Translated.
    VkShaderModule VertexModule(const Key& k)
    {
        if (k.geometry==2) return k.vertex==static_cast<uint32_t>(VertexProgram::Translated)?GeometryModule(k.vertexRecord):VK_NULL_HANDLE;
        if (k.mirror) {
            if (k.vertex != static_cast<uint32_t>(VertexProgram::Translated)) return VK_NULL_HANDLE;
            const auto* record = manifest::At(k.vertexRecord);
            if (!record) return VK_NULL_HANDLE;
            char path[96];
            const int n = std::snprintf(path, sizeof path, "mirror/%s", record->spv);
            return n > 0 && n < static_cast<int>(sizeof path) ? shaders::Get(path) : VK_NULL_HANDLE;
        }
        if (k.vertex == static_cast<uint32_t>(VertexProgram::Translated)) return RecordModule(k.vertexRecord);
        return k.vertex < kVertexPrograms ? s.vert[k.vertex] : VK_NULL_HANDLE;
    }

    VkShaderModule FragmentModule(const Key& k)
    {
        if (k.geometry==2) return k.fragment==static_cast<uint32_t>(FragmentProgram::Translated)?GeometryModule(k.fragmentRecord):VK_NULL_HANDLE;
        if (k.fragment == static_cast<uint32_t>(FragmentProgram::Translated)) return RecordModule(k.fragmentRecord);
        return k.fragment < kFragmentPrograms ? s.frag[k.fragment] : VK_NULL_HANDLE;
    }
}
