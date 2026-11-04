#pragma once
#include "LvFinish.h"
#include "LvAmbiance.h"
#include "LvFog.h"
#include "LvFogConstants.h"
namespace HD::Lumen {
struct FogSnapshot {Texture image;bool transmittance=false;std::array<float,4> uv{0,0,1,1};};
struct SurfacePolicy {uint32_t catalogueIdentity=0,alreadyAppliedClasses=0;float rgb[3]{};bool nativeColour=false;};
struct NativeFrame {
 Source::FrameInputs environment{};
 using PolicyReader=bool(*)(void*,uint64_t,NativeFrame&);PolicyReader refreshSurfacePolicy=nullptr;void* policyUser=nullptr;Source::AuthoredFog authored{};bool hasAuthored=false;
 Source::HeightSource height{};Source::Config numericBase{};
 // True only after the bridge has accounted for the reference renderer's local lights. Never guessed by the renderer.
 bool surfacePolicyComplete=false,nativeFogEnabled=false;
 // Read-only snapshot of the existing liquid owner: never create or modify a water pass.
 bool liquidPlaneValid=false;float liquidPlaneHeight=0;
 std::array<SurfacePolicy,1024> policies{};uint32_t policyCount=0;
 // HD 2026-11-02 lot L2: classes already lit for every lamp WITHOUT its own policy (same bits as alreadyAppliedClasses).
 uint32_t defaultAppliedClasses=0;bool effectsAllClasses=false;float spellGain=1.0f;bool spellPure=false; /* HD L10 */const std::vector<Source::EffectLamp>*gameSpells=nullptr;int spellSource=0; /* HD L13: 0 image detector, 1 game projectiles, 2 both */ // HD L7: spell lights and lantern ignore the class mask
 using FogReader=bool(*)(void*,uint64_t,FogSnapshot&);FogReader readReferenceFog=nullptr;void* fogUser=nullptr;
 float dayLightWeight=-1; // native continuous day/night blend, -1 means not supplied
 bool smoothAuthoredWeights=false;float shadowWeights[3]{};
 char journalPlace[240]{}; // HD L17: zone and lights around the camera, for Logs\\lumen-brume.txt only
 NativeFrame(){environment.mapId=-1;}
};
struct Availability {bool backend=false,lamps=false,fog=false,halos=false,ambiance=false,spells=false,shafts=false,memory=false,terrain=false,blur=false;};
bool SceneInputs(const WXL_VkStageContext&,VkRect2D world,const Source::FrameInputs& environment,Source::FrameInputs& out);
class Suite {
public:
 bool Init(const WXL_VkRenderApi*,const char* catalogue,const char* regionsText="");
 // Call on the world/scene thread BEFORE its draw. Allocations/resizes and terrain probes happen here, never in Record.
 bool Prepare(const NativeFrame&,Controls,VkExtent2D renderExtent,VkRect2D world,uint64_t completed);
 bool Record(const WXL_VkStageContext&);
 bool AfterReferenceFog(const WXL_VkStageContext&);
 bool Shutdown(uint64_t completed);
 bool NeedsAfterSpells()const{return prepared_&&controls_.spells;}
 bool NeedsBeforeSpells()const;bool ComposedFog(uint64_t serial)const{return composedFog_==serial&&serial!=0;}bool ComposedShafts(uint64_t serial)const{return ComposedFog(serial)&&controls_.shafts;}
 const Availability& Available()const{return available_;}const char* Status()const{return status_;}const std::vector<Source::EffectLamp>& MergedSpells(){mergedSpells_.clear();if(native_.spellSource!=1)mergedSpells_=spells_.Lights();if(native_.spellSource!=0&&native_.gameSpells)mergedSpells_.insert(mergedSpells_.begin(),native_.gameSpells->begin(),native_.gameSpells->end());if(mergedSpells_.size()>size_t(Source::kMaxEffectLights))mergedSpells_.resize(size_t(Source::kMaxEffectLights));return mergedSpells_;}size_t GameSpellCount()const{return native_.gameSpells?native_.gameSpells->size():0;}const Source::EffectClusterStats& SpellStats()const{return spells_.Stats();}size_t SpellLightCount()const{return spells_.Lights().size();}const std::vector<Source::EffectLamp>& SpellLamps()const{return spells_.Lights();}unsigned SpellMeasurements()const{return measuredCount_;}unsigned SpellSkipped()const{return captures_.Skipped();}uint64_t LastUsed()const{return gpu_.LastUsed();}
 Source::LampCatalog& Catalogue(){return lamps_.Catalog();}
 // HD L17: read-only views of what the fog used this frame (journal only).
 const NativeFrame& NativeUsed()const{return native_;}const Source::FrameInputs& FrameUsed()const{return frame_;}const Source::FogParams& FogUsed()const{return fog_;}const Source::Config& ConfigUsed()const{return config_;}
private:
 const WXL_VkRenderApi*api_=nullptr;Gpu gpu_;Lamps lamps_;Ambiance ambiance_;Fog fogCpu_;SpellLights spells_;unsigned measuredCount_=0;std::vector<Source::EffectLamp> mergedSpells_;LampStage lampGpu_;FogStage fogGpu_;SpellCapture captures_;Composite composite_;NoiseAssets noise_;OccluderMemory memory_;
 NativeFrame native_;Controls controls_{};Source::Config config_{};Source::FogParams fog_{};Source::FrameInputs frame_{};Source::LampTable table_{};
 Availability available_{};VkExtent2D extent_{};VkRect2D world_{};uint32_t scale_=2;bool sized_=false,memoryMade_=false,prepared_=false,opaque_=false,fogWanted_=false,tableUploaded_=false,terrainReady_=false;
 Target neutralFog_,terrain_,floors_,edges_,opaqueFallback_;Texture opaqueDepth_{};unsigned terrainVersion_=~0u,floorVersion_=~0u;double phaseCoarse_[3]{},phaseFine_[3]{},phaseCloud_[2]{},lastSeconds_=-1,ceilingSeconds_=-1;float ceilingGate_=0;float previousVP_[16]{};bool previousValid_=false;uint64_t serial_=0,composedFog_=0,measuredSerial_=0,beforeSerial_=0,finalSerial_=0;const char*status_="off";
 Uniforms common_{},march_{},temporal_{};
 void BuildMarch();bool BeforeSpells(const WXL_VkStageContext&);bool Opaque(const WXL_VkStageContext&);bool Fail(const char*);
};
}
