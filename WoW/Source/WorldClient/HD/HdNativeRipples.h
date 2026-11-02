#pragma once
#include <cstdint>
namespace gxvk::vk::texture {struct Image;}
namespace HD::NativeRipples {
void BeginWorld(bool enabled);
void BeforeLiquids();
void Reset();
bool Shutdown();
const gxvk::vk::texture::Image* SampledImage(uint32_t map);
}
