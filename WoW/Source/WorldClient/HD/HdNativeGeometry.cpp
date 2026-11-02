// SPDX-License-Identifier: GPL-3.0-or-later
#include "WoW/Source/WorldClient/HD/HdNativeGeometry.h"
#include "Engine/Source/Gx/CGxDeviceVulkan/Vulkan/HdGeometryPass.hpp"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
namespace HD::NativeGeometry {
namespace {
CVar* diagnostic=nullptr;
bool On(const char* name){auto* v=CVar::LookupRegistered(name);return v && v->m_intValue;}
}
bool Wanted(){
 if(!diagnostic)diagnostic=CVar::Register("hdGeometryCapture","Diagnostic: private tagged opaque geometry replay",0,"0",nullptr,CATEGORY_GRAPHICS,false,nullptr,false);
 return (diagnostic && diagnostic->m_intValue) || On("hdSunShadows") || On("hdContactShadow") || (On("hdAO") && On("hdAONormals"));
}
void World(){if(g_theGxDevicePtr && g_theGxDevicePtr->DeviceApi()==GxApi_Vulkan && Wanted())gxvk::vk::hdgeometry::Render();}
bool Shutdown(){return gxvk::vk::hdgeometry::Shutdown();}
}
