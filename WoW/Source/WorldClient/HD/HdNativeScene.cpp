#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/CGxDeviceVulkan.h"
#include "Engine/Source/Gx/Types.h"
#include <cstring>
namespace HD::NativeScene {
namespace { Snapshot snapshot; bool valid=false; }
void Capture() {
    valid=false;
    namespace frame=gxvk::vk::frame;
    if(!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi()!=GxApi_Vulkan || frame::Target())return;
    auto* device=static_cast<CGxDeviceVulkan*>(g_theGxDevicePtr);
    VkRect2D scissor{};
    if(!device->IPlaceD3d(snapshot.viewport,scissor))return;
    static_assert(sizeof(C44Matrix)==sizeof snapshot.constants.projection,"verified native matrix rows");
    std::memcpy(snapshot.constants.projection,&device->m_projNative,sizeof snapshot.constants.projection);
    const auto& view=device->m_xforms[GxXform_View];
    std::memcpy(snapshot.constants.view,&view.m_mtx[view.m_level],sizeof snapshot.constants.view);
    constexpr float centreShift=.5f-1.0f/128.0f; // Same as native IPlaceD3d caller in CGxDeviceVulkanDraw.
    snapshot.viewport.x+=centreShift;snapshot.viewport.y+=centreShift;
    snapshot.serial=frame::Serial();valid=true;
}
bool Get(uint64_t serial,Snapshot& out) {
    if(!valid || snapshot.serial!=serial)return false;
    out=snapshot;return true;
}
}
