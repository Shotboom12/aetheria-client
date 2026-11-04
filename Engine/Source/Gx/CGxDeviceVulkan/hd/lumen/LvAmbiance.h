#pragma once
#include "LvControls.h"
#include "source/region_settings.h"
namespace HD::Lumen {
class Ambiance {
public:bool Init(const std::string&text);Source::Config Record(const Source::FrameInputs&,const Controls&,const Source::Config& numericBase);void Shutdown();
 const std::vector<std::string>&Problems()const{return problems_;}
private:Source::RegionBook book_;Source::RegionBlender blender_;std::vector<std::string>problems_;
};
}
