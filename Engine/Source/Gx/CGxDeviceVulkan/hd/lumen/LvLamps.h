#pragma once
#include "LvControls.h"
#include "source/lamp_selection.h"
#include "source/lamp_tiles.h"
#include "source/fog_model.h"
namespace HD::Lumen {
class Lamps {
public:
 bool Init(const std::string& catalogPath);bool Resize(const Source::FrameInputs&);
 const Source::LampTable& Record(const Source::FrameInputs&,const Controls&,const Source::Config&,const Source::FogParams&,const std::vector<Source::EffectLamp>* effects=nullptr);
 void Shutdown();void ForgetVisibility(){selection_.ForgetVisibility();}
 Source::LampCatalog& Catalog(){return catalog_;}const Source::LampTiles& Tiles()const{return tiles_;}
 const Source::LampStats& Stats()const{return selection_.Stats();}float VisibilityBlend()const{return selection_.VisibilityBlend();}
 uint32_t Identity(unsigned slot)const{return selection_.Identity(slot);}
 const Source::LanternLamp& Lantern()const{return lantern_.Lamp();}
private:Source::LampCatalog catalog_;Source::LampSelection selection_;Source::LampTiles tiles_;Source::BeltLantern lantern_;
};
}
