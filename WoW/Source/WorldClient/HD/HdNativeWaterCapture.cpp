// SPDX-License-Identifier: GPL-3.0-or-later
#include "WoW/Source/WorldClient/HD/HdNativeWaterCapture.h"
#include "WoW/Source/WorldClient/HD/HdForeverWater.h"
#include "WoW/Source/WorldClient/HD/HdNativeScene.h"
#include "WoW/Source/WorldClient/HD/HdCataWater.h"
#include "WoW/Source/WorldClient/HD/Forever/HdForeverFieldOwner.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/WorldScene.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Gx/GxSeam.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
namespace HD::NativeWaterCapture {
namespace {
#include "Engine/Source/Gx/CGxDeviceVulkan/hd/shaders/CaptureScene.spv.h"
namespace frame=gxvk::vk::frame;
namespace texture=gxvk::vk::texture;
namespace hdscene=gxvk::vk::hdscene;
namespace api=gxvk::vk::hdeffect;
CataWater::SceneCapture capture;texture::Image scratch{};
uint64_t scratchLast=0;bool initialized=false;
CVar *diagnostic=nullptr,*info=nullptr;char lastInfo[256]{};
void Info(const char* value){if(info && std::strcmp(lastInfo,value)){std::snprintf(lastInfo,sizeof lastInfo,"%s",value);info->Set(lastInfo,true,false,false,false);}}
CVar* Var(const char* name,const char* help,const char* value,uint32_t flags=1){if(auto* old=CVar::LookupRegistered(name))return old;return CVar::Register(name,help,flags,value,nullptr,CATEGORY_GRAPHICS,false,nullptr,false);}
struct Pending {api::StageContext stage;};
void Why(const char* what){static unsigned n=0;if((n++%30)==0&&n<1800)HD::ForeverWater::Trace("capture: %s",what);}
bool WithDepth(VkCommandBuffer cmd,VkImageView view,VkFormat format,VkImageLayout layout,void* user){
    auto& pending=*static_cast<Pending*>(user);if(cmd!=pending.stage.cmd)return false;
    pending.stage.depthView=view;pending.stage.depthFormat=format;pending.stage.depthLayout=layout;
    capture.Record(pending.stage);
    const bool captured=capture.Get(pending.stage.serial,pending.stage.extent)!=nullptr;
    if(!captured)Why("WithDepth: capture.Get null after Record");
    if(captured && HD::ForeverWater::Requested()) {
        const auto& eye=CWorldScene::s_activeWorldView;
        HD::ForeverNative::FieldOwner::PreparePrevious(pending.stage,World::QueryMapId(),
            g_gxCataPlane.m_valid!=0,g_gxCataPlane.m_height,eye.x,eye.y);
    } else HD::ForeverNative::FieldOwner::DiscardPrevious(pending.stage.serial);
    return captured;
}
bool Record(const hdscene::Context& c,void*) {
    scratchLast=c.serial;NativeScene::Snapshot snapshot;
    if(!NativeScene::Get(c.serial,snapshot)){Why("Record: NativeScene snapshot missing for serial");return false;}
    Pending pending{api::Stage(c,CataWater::CapturePoint)};
    pending.stage.scene=&snapshot.constants;pending.stage.viewport=snapshot.viewport;pending.stage.sceneValid=1;
    const bool ran=frame::WithSceneDepth(WithDepth,&pending);if(!ran)Why("Record: WithSceneDepth returned false");return ran;
}
bool Init(){
    if(initialized)return true;
    if(!hdscene::Supported()){Info("water capture: compute/storage unsupported");return false;}
    const std::vector<uint32_t> code(kCaptureSceneSpv,kCaptureSceneSpv+sizeof kCaptureSceneSpv/sizeof kCaptureSceneSpv[0]);
    if(!capture.Initialize(api::Get(),code)){Info(capture.Error().c_str());return false;}
    initialized=true;return true;
}
}
void BeforeLiquids(){
    if(!g_theGxDevicePtr || g_theGxDevicePtr->DeviceApi()!=GxApi_Vulkan)return;
    if(!diagnostic){
        diagnostic=Var("hdWaterCapture","HD: prepare scene/depth/blur capture only; no water material replacement","0");
        info=Var("hdWaterCaptureInfo","HD: native water capture status","off",0); if (info) info->m_flags &= ~0x1u; /* status only: not archived to Config.wtf */
    }
    const auto* water=CVar::LookupRegistered("hdCataWater");
    const bool wanted=(diagnostic&&diagnostic->m_intValue)||(water&&water->m_intValue)||HD::ForeverWater::Requested();
    capture.Enable(wanted);
    if(!wanted){HD::ForeverNative::FieldOwner::DiscardPrevious(frame::Serial());Info("off");return;}
    if(frame::Target() || frame::QueryActive() || !Init()){Why("BeforeLiquids: target/query/init");HD::ForeverNative::FieldOwner::DiscardPrevious(frame::Serial());return;}
    const auto* image=frame::Scene();if(!image || !image->extent.width || !image->extent.height){Why("BeforeLiquids: no scene image");HD::ForeverNative::FieldOwner::DiscardPrevious(frame::Serial());return;}
    if(!capture.Prepare(frame::Slot(),frame::Completed(),image->extent)){Why("BeforeLiquids: capture.Prepare failed");HD::ForeverNative::FieldOwner::DiscardPrevious(frame::Serial());Info(capture.Error().c_str());return;}
    if(!scratch.handle || scratch.extent.width!=image->extent.width || scratch.extent.height!=image->extent.height){
        frame::RetireImage(scratch,scratchLast);scratchLast=0;
        if(!texture::CreateStorage(image->extent.width,image->extent.height,scratch)){HD::ForeverNative::FieldOwner::DiscardPrevious(frame::Serial());Info("water capture: scratch allocation failed");return;}
    }
    // Capture consumes a colour snapshot but never writes it back over the world.
    if(!hdscene::Apply(scratch,Record,nullptr,false)){Why("BeforeLiquids: hdscene::Apply false");HD::ForeverNative::FieldOwner::DiscardPrevious(frame::Serial());Info("water capture skipped: current scene/depth unavailable");return;}
    if(!capture.MakeSampled(frame::Outside(),frame::Serial(),image->extent)){HD::ForeverNative::FieldOwner::DiscardFrame(frame::Serial());Info("water capture: sampling transition unavailable");return;}
    Why("BeforeLiquids: reached sampled-ready");Info("sampled scene/depth/blur ready; water service integration pending");
}
const CataWater::SceneCapture::Snapshot* Get(uint64_t serial,VkExtent2D extent){return initialized?capture.Get(serial,extent):nullptr;}
const texture::Image* SampledImage(uint32_t kind){
    const auto* scene=frame::Scene();
    return initialized && scene?capture.SampledImage(frame::Serial(),scene->extent,kind):nullptr;
}
bool Shutdown(){
    if(!initialized && !scratch.handle)return true;
    const auto last=std::max(scratchLast,capture.LastUsed());
    if(last>frame::Completed()){frame::Flush();if(!frame::WaitSerial(last))return false;}
    if(!capture.StopAndDestroy(frame::Completed()))return false;
    frame::RetireImage(scratch,scratchLast);scratchLast=0;initialized=false;Info("off");return true;
}
}
