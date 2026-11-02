// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Pipelines.hpp"
namespace HD::VulkanShadows::Bridge {
inline uint32_t ids[5]{};
inline uint64_t serial=0;
inline uint32_t Mask(const gxvk::vk::pipelines::Draw& draw,uint64_t frame,bool interiorOnly=false) {
 if(serial!=frame)return 0;
 uint32_t mask=0;
 for(uint32_t slot=0;slot<16;++slot)if(draw.textureMask&(1u<<slot))
  for(uint32_t i=interiorOnly?4:0;i<5;++i)if(ids[i] && draw.textures[slot].imageId==ids[i]){mask|=1u<<slot;break;}
 return mask;
}
}
