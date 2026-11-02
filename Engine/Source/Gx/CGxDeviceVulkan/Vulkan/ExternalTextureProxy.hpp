#pragma once

#include <cstdint>

class CGxTex;
namespace gxvk::vk::texture { struct Image; }

namespace gxvk::vk::external_proxy
{
    // Register and resolve on the render thread. Owners must unregister before destruction.
    using Resolver = const texture::Image* (*)(void* owner, uint64_t frameSerial);
    bool Register(CGxTex* proxy, Resolver resolver, void* owner);
    void Unregister(CGxTex* proxy, void* owner);
}
