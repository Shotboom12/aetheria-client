#pragma once
#include "LvControls.h"
#include "source/fog_model.h"
#include "source/terrain_shadow.h"
namespace HD::Lumen {
class Fog {
public:void ResetHistory(){guard_.Reset();reset_=First;}void Init(){ResetHistory();terrain_.Forget();}void Shutdown(){Init();}
 Source::FogParams Record(const Source::FrameInputs&,const Controls&,const Source::Config&,const Source::AuthoredFog* nativeFog,const Source::HeightSource* terrain=nullptr);
 uint32_t ResetBits()const{return reset_;}const Source::TerrainShadowMap& Terrain()const{return terrain_;}
private:HistoryGuard guard_;Source::TerrainShadowMap terrain_;uint32_t reset_=First;
};
}
