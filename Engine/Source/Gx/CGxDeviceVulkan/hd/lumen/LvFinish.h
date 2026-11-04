#pragma once
#include "LvStages.h"
#include "LvSpellLights.h"
#include "source/shaft_memory.h"
#include "source/drift_noise.h"
#include "source/cloud_noise.h"
namespace HD::Lumen {
class NoiseAssets {
public:bool Init(Gpu&);bool RecordUploads();bool Ready()const;void Shutdown();Target& Volume(){return volume_;}Target& Cloud(){return cloud_;}
private:Gpu*g_=nullptr;Target volume_,cloud_;std::vector<std::vector<uint8_t>>volumeBytes_;std::vector<Source::CloudNoiseLevel>cloudBytes_;unsigned nextVolume_=0,nextCloud_=0;
};
class OccluderMemory {
public:bool Init(Gpu&g,uint32_t side=2048);bool Record(const Uniforms&common,const Source::FrameInputs&,Texture depth,const Source::CarrierBody&,bool reset=false);void Shutdown();void Lookup(const Source::FrameInputs&,Uniforms&march)const;Target& Output(){return map_;}Target& Before(){return before_;}bool Valid()const{return valid_;}unsigned Realignments()const{return realignments_;}
private:Gpu*g_=nullptr;Target map_,kept_,atomic_,realigned_,before_;uint32_t side_=2048,frame_=0,realignments_=0;bool valid_=false,fading_=false;Source::ShaftMemoryFrame basis_{},beforeBasis_{};Source::ShaftMemoryCamera camera_{},beforeCamera_{};double fadeStart_=0,fadeSeconds_=1,lastRealign_=0,lastSeconds_=0;int mapId_=-1;
};
class SpellCapture {
public:bool Init(Gpu&g){g_=&g;return stage_.Init(g);}bool Resize(uint32_t w,uint32_t h,uint64_t completed,VkRect2D viewport={});
 bool Before(const WXL_VkStageContext&,const Uniforms&,const Source::FrameInputs&);
 bool After(const WXL_VkStageContext&,const Uniforms&);
 unsigned Poll(uint64_t completed,SpellLights&,const Source::LampCatalog&);
 // Invalidate measurements without reading data or destroying in-flight buffers.
 void Cancel(uint64_t completed);
 bool Shutdown(uint64_t completed);unsigned Skipped()const{return skipped_;}
private:Gpu*g_=nullptr;SpellStage stage_;Target scene_,now_,depth_;VkRect2D viewport_{};Readback ring_[3];bool discard_[3]{};Source::EffectFrame frames_[3]{},baselineFrame_{};uint64_t baselineSerial_=0;uint32_t w_=0,h_=0;unsigned skipped_=0;
};
struct CompositeSettings {bool surfaces=false,fog=false,halos=false;float lampGain=1;};
class Composite {
public:bool Init(Gpu&g){g_=&g;return true;}bool Resize(uint32_t w,uint32_t h);
 bool Record(const WXL_VkStageContext&,const Uniforms&,const WXL_VkGBuffer*,Texture lamps,Texture fog,Texture halos,VkExtent2D lowExtent,CompositeSettings);
 void Shutdown();
private:Gpu*g_=nullptr;Target scene_;
};
// Builds the shared source c0..8 registers from a validated native D3D projection/viewport.
bool ValidNativeGBuffer(const WXL_VkStageContext&,const WXL_VkGBuffer&);
bool CommonConstants(const Source::FrameInputs&,VkExtent2D depthSize,uint32_t scale,Uniforms&);
}
