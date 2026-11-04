// Adapted pure renderer formulas from Lumenveil GPL-2.0. No device hooks, colour grade or water pass.
#pragma once
#include "source/fog_model.h"
#include "source/terrain_shadow.h"
#include "source/drift_noise.h"
namespace HD::Lumen::Source {
struct Float4 {float x,y,z,w;};
struct BankConstants {Float4 floor,shape,grounds[kBankGrounds],light,misc,noise;};
struct CloudLookup {Float4 map,shape;};
struct DriftConstants {Float4 coarse,fine,scale,mist,detail;};
static_assert(sizeof(BankConstants)==8*16&&sizeof(DriftConstants)==5*16,"GPU register layout");
BankConstants BuildBankConstants(const FrameInputs&,const FogParams&,const TerrainShadowConstants&,bool);
CloudLookup CloudConstants(const FrameInputs&,const FogParams&,const double*,bool);
Float4 ShaftFadeConstants(float,float,float);
DriftConstants BuildDriftConstants(const FrameInputs&,const FogParams&,const Config&,const double*,const double*,bool);
}
