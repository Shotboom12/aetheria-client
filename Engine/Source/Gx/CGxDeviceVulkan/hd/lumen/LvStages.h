#pragma once
#include "LvGpu.h"
#include "LvLamps.h"
namespace HD::Lumen {
// A shared Gpu instance is owned by the native integration. Resize is called outside Record.
class LampStage {
public:bool Init(Gpu&g){gpu_=&g;return true;}bool Resize(uint32_t width,uint32_t height,uint32_t scale=2);
 bool Record(const Uniforms& common,const Source::LampTable&,const Source::LampTiles&,Texture depth,Texture fogOpacity,Texture occluderDepth,float visibilityBlend,const std::array<float,4>&sun,const Source::CarrierBody&,bool surfaces=true,bool halos=true,bool useTiles=true);
 bool UploadTable(const Source::LampTable&,const Source::LampTiles&);
 bool RecordSurfaces(const Uniforms&,Texture depth,Texture occluder,const std::array<float,4>&sun,const Source::CarrierBody&,bool blur=true,bool useTiles=true,Texture material={},Texture normals={});
 bool RecordHalos(const Uniforms&,Texture depth,Texture fog,float visibilityBlend,bool fogIsTransmittance=false,bool useTiles=true,std::array<float,4> fogUV={0,0,1,1});
 void Shutdown();Target& Surfaces(){return surfaces_;}Target& Halos(){return halos_;}void Reset(){valid_=false;}
private:Gpu*gpu_=nullptr;Target table_,tiles_,visibility_[2],surfaces_,halos_,blur_;int surfaceSlots_=0,slots_=0;unsigned ping_=0;bool valid_=false;uint32_t width_=0,height_=0,scale_=2;
};
class FogStage {
public:bool Init(Gpu&g){gpu_=&g;return true;}bool Resize(uint32_t width,uint32_t height,uint32_t scale=2);
 // march constants include c12..35 source FogLayer[4] and c38..66 helper constants; see binding contract.
 bool Record(const Uniforms& march,const Uniforms& temporal,const std::array<Texture,16>& inputs,unsigned quality,bool memoryReady,bool reset);
 void Shutdown();Target& Output(){return history_[ping_];}
private:Gpu*gpu_=nullptr;Target current_,history_[2];unsigned ping_=0;bool valid_=false;
};
class SpellStage {
public:bool Init(Gpu&g){gpu_=&g;return true;}bool Resize(uint32_t width,uint32_t height);
 bool Record(const Uniforms& common,Texture sceneBefore,Texture sceneNow,Texture depthBefore,Texture depthNow,bool swapRB=false);
 void Shutdown();Target& Measurements(){return reduced_;}uint32_t Columns()const{return columns_;}uint32_t Rows()const{return rows_;}
private:Gpu*gpu_=nullptr;Target gatherLight_,gatherPlace_,reduced_;uint32_t width_=0,height_=0,columns_=0,rows_=0;
};
}
