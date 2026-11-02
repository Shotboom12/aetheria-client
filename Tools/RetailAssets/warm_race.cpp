#include "Engine/Source/Retail/Appearance.h"
#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include <cstdlib>
#include <cstdio>
#include <set>

namespace app=Retail::Db2::appearance;
int main(int argc,char** argv) {
    if(argc<4 || argc>5) {
        std::fprintf(stderr,"Usage: RetailRaceWarmup <ini> <RetailRaceID> <sex> [all-choices]\n");return 2;
    }
    _putenv_s("WOW_RETAIL_CONFIG",argv[1]);
    _putenv_s("WOW_RETAIL_HEADLESS","1");
    const uint32_t race=uint32_t(std::strtoul(argv[2],nullptr,10)),sex=uint32_t(std::strtoul(argv[3],nullptr,10));
    if(!app::Status().loaded)return 1;
    const auto model=app::ChrModelForRace(race,sex);
    std::vector<app::ChrOption> options;
    std::vector<uint32_t> defaults;
    for(uint32_t i=0;i<app::OptionCount(model);++i) {
        app::ChrOption option{};app::ChrChoice choice{};
        if(!app::OptionAt(model,i,option) || !app::ChoiceAt(option.id,0,choice))return 1;
        options.push_back(option);defaults.push_back(choice.id);
    }
    std::set<uint32_t> models,textures;
    unsigned cases=0;
    const auto collect=[&](const auto& picks) {
        app::Recipe recipe;
        if(!app::BuildForCharacter(race,sex,picks.data(),uint32_t(picks.size()),recipe))return false;
        models.insert(recipe.modelFileDataId);
        for(const auto& part:recipe.attached)if(part.fileDataId)models.insert(part.fileDataId);
        for(const auto& layer:recipe.layers) {
            const auto id=app::MaterialFileDataId(layer.materialResourcesId,0);
            if(id)textures.insert(id);
        }
        ++cases;return true;
    };
    if(!collect(defaults))return 1;
    if(argc==5) for(size_t i=0;i<options.size();++i)for(uint32_t j=0;j<app::ChoiceCount(options[i].id);++j) {
        app::ChrChoice choice{};auto picks=defaults;
        if(!app::ChoiceAt(options[i].id,j,choice))return 1;
        picks[i]=choice.id;if(!collect(picks))return 1;
    }
    std::printf("Plan: %u recipes; %zu models; %zu material textures.\n",cases,models.size(),textures.size());std::fflush(stdout);
    std::string error;
    for(auto id:models) {
        Retail::Source().RegisterCharacterModel(id);
        const auto name=Retail::FileName(id,".m2");
        if(!Retail::Source().WarmModel(name.c_str(),error)) {std::fprintf(stderr,"%s: %s\n",name.c_str(),error.c_str());return 1;}
        std::printf("Model %u and skin/animation/texture dependencies ready.\n",id);std::fflush(stdout);
    }
    std::vector<uint8_t> bytes;
    for(auto id:textures) {
        if(!Retail::Source().Read(Retail::FileName(id,".blp").c_str(),bytes,error)) {std::fprintf(stderr,"Texture %u: %s\n",id,error.c_str());return 1;}
    }
    std::printf("PASS: race %u sex %u; %u recipe cases warmed. Includes each option against defaults; not every multi-option combination.\n",race,sex,cases);
    return 0;
}
