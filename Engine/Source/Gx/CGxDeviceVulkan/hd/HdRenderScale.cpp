// SPDX-License-Identifier: GPL-3.0-or-later
#include "HdRenderScale.h"
#include "HdNativeFsr.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/Frame.hpp"
#include "Engine/Source/Gx/Types.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include <cmath>
namespace HD::RenderScale {
namespace {
CVar* scaleVar = nullptr;
CVar* infoVar = nullptr;
bool active = false;
}
void Begin() {
    active = false;
    if (Visuals::Current() != Visuals::AzerothRemastered || !g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi() != GxApi_Vulkan) return;
    if (!scaleVar) {
        scaleVar = OptionCVars::Register("hdRenderScale", "HD: world render scale 0.25..1 (1 = off): the 3D world is drawn smaller and stretched, the interface stays sharp", "1");
        infoVar = CVar::Register("hdRenderScaleInfo", "HD: render scale status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    }
    float scale = OptionCVars::Float(scaleVar, 0.25f, 1.0f, 1.0f);
    const float fsrScale = HD::NativeFsr::BeginWorld(); // hdFsr on: FSR picks the render scale (and registers its upscaler)
    if (fsrScale > 0.0f) scale = fsrScale;
    if (!(scale < 0.999f)) return;
    // Only a world that fills the window: the stretch replaces the whole scene, so a partial world view would erase what lies around it.
    namespace frame = gxvk::vk::frame;
    auto* device = static_cast<CGxDeviceVulkan*>(g_theGxDevicePtr);
    VkViewport viewport{};VkRect2D scissor{};
    const VkExtent2D extent = frame::Extent();
    if (!device->IPlaceD3d(viewport, scissor) || viewport.x != 0.0f || viewport.y != 0.0f ||
        viewport.width != float(extent.width) || viewport.height != float(extent.height)) return;
    active = frame::BeginWorldScale(scale);
    if (infoVar) infoVar->Set(active ? "scaled" : "refused", true, false, false, false);
}
void End() {
    if (!active) return;
    active = false;
    gxvk::vk::frame::EndWorldScale();
}
}
