// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdEffectApi.hpp"
namespace HD::VulkanGrade {
// Planned after the fog kit's public point 5 / private point 4. See INTEGRATION.md.
constexpr uint32_t Point=6,PrivatePoint=5;
constexpr uint32_t LutWidth=1024,LutHeight=32;
constexpr size_t LutBytes=size_t(LutWidth)*LutHeight*4;
using Texels=std::array<uint8_t,LutBytes>; // BGRA bytes, matching existing BLP files
struct alignas(16) Parameters {
    float blend=0,strength=1,debug=0,reserved=0;
    uint32_t swapRB=0,pad[3]{};
};
static_assert(sizeof(Parameters)==32,"GLSL push constant layout");
// Exact reference weights: 16 IDs, strongest two, stable tie order.
struct LutWeights {
    int32_t id[16]{};float weight[16]{};int32_t count=0;
    void Add(int32_t lut,float w);
    void Scale(float factor);
};
struct Selection {int32_t a=0,b=0;float blend=0;bool hasGrade=false;};
Selection Select(const LutWeights& weights);
void Identity(Texels& pixels);
bool DecodeLut(const uint8_t* bytes,size_t size,Texels& pixels);
std::array<float,3> Sample(const Texels& pixels,std::array<float,3> rgb);
class Effect {
public:
    Effect()=default;Effect(const Effect&)=delete;Effect& operator=(const Effect&)=delete;
    bool Init(const gxvk::vk::hdeffect::Api* api);
    bool Resize(uint32_t width,uint32_t height);
    // New ID only, immutable after addition. CPU allocations/GPU creation happen HERE, never in Record.
    bool AddLut(int32_t id,const Texels& pixels);
    bool HasLut(int32_t id) const;
    bool Record(const gxvk::vk::hdeffect::StageContext& ctx,int32_t a,int32_t b,const Parameters& parameters);
    bool Shutdown(uint64_t completedSerial);
    uint64_t LastUsed() const;
    const char* Error() const;
private:
    struct Impl;Impl* state_=nullptr;
};
}
