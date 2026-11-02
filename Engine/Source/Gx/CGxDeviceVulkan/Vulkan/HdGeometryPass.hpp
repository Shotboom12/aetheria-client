// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/HdShadowInputs.h"
namespace gxvk::vk::hdgeometry {
bool Available();
// Private opaque replay: no main colour/depth attachment is written.
bool Render();
bool Get(uint64_t serial,HD::VulkanShadows::GeometryInputs& out);
bool Shutdown();
}
