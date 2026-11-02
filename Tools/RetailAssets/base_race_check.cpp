#include "Engine/Source/Retail/Appearance.h"
#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/Compose.h"
#include "Engine/Source/Retail/RaceGeometry.h"
#include "Engine/Source/Retail/RetailModel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace app=Retail::Db2::appearance;
static bool Fetch(uint32_t id,std::vector<uint8_t>& bytes,void*) {
    std::string error;
    if(Retail::Source().Read(Retail::FileName(id,".blp").c_str(),bytes,error))return true;
    std::fprintf(stderr,"Texture %u: %s\n",id,error.c_str());return false;
}
static uint32_t Word(const std::vector<uint8_t>& data,size_t at) {
    uint32_t result=0;if(at+4<=data.size())std::memcpy(&result,data.data()+at,4);return result;
}
static bool Check(uint32_t race,uint32_t sex) {
    const auto model=app::ChrModelForRace(race,sex);
    if(!model)return false;
    std::vector<app::ChrOption> options;
    std::vector<uint32_t> defaults;
    for(uint32_t i=0;i<app::OptionCount(model);++i) {
        app::ChrOption option{};app::ChrChoice choice{};
        if(!app::OptionAt(model,i,option)||!app::ChoiceAt(option.id,0,choice))return false;
        options.push_back(option);defaults.push_back(choice.id);
    }
    unsigned cases=0,failures=0,unresolved=0;
    std::set<uint32_t> models;
    app::Recipe initial;
    const auto inspect=[&](const auto& picks, bool first) {
        app::Recipe recipe;
        if(!app::BuildForCharacter(race,sex,picks.data(),uint32_t(picks.size()),recipe)||!recipe.modelFileDataId||!recipe.textureLayoutId) {
            ++failures;return;
        }
        if(first)initial=recipe;
        models.insert(recipe.modelFileDataId);
        for(const auto& layer:recipe.layers)if(!app::MaterialFileDataId(layer.materialResourcesId,0))++unresolved;
        ++cases;
    };
    inspect(defaults,true);
    for(size_t i=0;i<options.size();++i)for(uint32_t j=0;j<app::ChoiceCount(options[i].id);++j) {
        auto picks=defaults;app::ChrChoice choice{};
        if(!app::ChoiceAt(options[i].id,j,choice)){++failures;continue;}
        picks[i]=choice.id;inspect(picks,false);
    }
    std::printf("DATA race=%u sex=%u options=%zu cases=%u failures=%u unresolved=%u bodyModels=%zu\n",
        race,sex,options.size(),cases,failures,unresolved,models.size());std::fflush(stdout);
    std::string error;
    for(uint32_t id:models) {
        Retail::Source().RegisterCharacterModel(id);
        const auto name=Retail::FileName(id,".m2"),base=name.substr(0,name.size()-3);
        std::vector<uint8_t> bytes;
        if(!Retail::Source().Read(name.c_str(),bytes,error)) {
            std::fprintf(stderr,"MODEL race=%u sex=%u id=%u: %s\n",race,sex,id,error.c_str());return false;
        }
        Retail::ModelFiles graph;
        if(!Retail::Source().DescribeLoadedModel(name.c_str(),graph))return false;
        const auto sequenceCount=Word(bytes,0x1c), sequenceOffset=Word(bytes,0x20);
        if(sequenceOffset>bytes.size() || sequenceCount>(bytes.size()-sequenceOffset)/64)return false;
        for(uint32_t first=0;first<sequenceCount;++first) {
            uint32_t link=first;
            for(uint32_t hops=0;;++hops) {
                if(link>=sequenceCount || hops>=sequenceCount)return false;
                uint16_t next=0;std::memcpy(&next,bytes.data()+sequenceOffset+link*64+62,2);
                link=next;
                if(link==first)break;
            }
        }
        std::printf("LINKS race=%u sex=%u model=%u sequences=%u invalid=0\n",race,sex,id,sequenceCount);
        for(uint16_t action : {uint16_t(4), uint16_t(5)}) {
            bool found = false;
            const auto count = Word(bytes, 0x1c), offset = Word(bytes, 0x20);
            if(offset > bytes.size() || count > (bytes.size() - offset) / 64)return false;
            for(uint32_t i=0; i<count; ++i) {
                uint16_t sequenceId=0;std::memcpy(&sequenceId, bytes.data()+offset+i*64, 2);
                if(sequenceId!=action)continue;
                std::printf("MOVEMENT race=%u sex=%u model=%u action=%u duration=%u flags=%u\n",
                    race,sex,id,action,Word(bytes,offset+i*64+4),Word(bytes,offset+i*64+12));
                found=true;break;
            }
            if(!found){std::fprintf(stderr,"Missing movement race=%u sex=%u model=%u action=%u\n",race,sex,id,action);return false;}
        }
        unsigned idle=0;
        for(const auto& animation:graph.animations)if(animation.fileId && animation.id==0) {
            char suffix[48];std::snprintf(suffix,sizeof(suffix),"%04u-%02u.anim",animation.id,animation.variation);
            if(!Retail::Source().Read((base+suffix).c_str(),bytes,error)) {
                std::fprintf(stderr,"IDLE race=%u sex=%u: %s\n",race,sex,error.c_str());return false;
            }
            ++idle;
        }
        if(!Retail::Source().Read((base+"00.skin").c_str(),bytes,error))return false;
        const auto count=Word(bytes,28),offset=Word(bytes,32);
        if(offset>bytes.size()||count>(bytes.size()-offset)/48)return false;
        std::set<uint16_t> ids;
        for(uint32_t i=0;i<count;++i){uint16_t part=0;std::memcpy(&part,bytes.data()+offset+48*i,2);ids.insert(part);}
        std::printf("MODEL race=%u sex=%u id=%u skeleton=%u skins=%zu idleFiles=%u geosets=",race,sex,id,graph.skeleton,graph.skins.size(),idle);
        for(auto part:ids)std::printf("%u,",part);
        std::printf("\n");std::fflush(stdout);
        Retail::Source().ReleaseModel(name.c_str());
    }
    // Validate default visible collections too; optional choice collections remain on-demand.
    for(const auto& part:initial.attached)if(part.fileDataId&&part.geoset%100) {
        Retail::Source().RegisterCharacterModel(part.fileDataId);
        const auto name=Retail::FileName(part.fileDataId,".m2");std::vector<uint8_t> bytes;
        if(!Retail::Source().Read(name.c_str(),bytes,error)){std::fprintf(stderr,"COLLECTION %u: %s\n",part.fileDataId,error.c_str());return false;}
        Retail::Source().ReleaseModel(name.c_str());
    }
    std::vector<Retail::ComposedTexture> textures;
    std::string warnings;
    const bool composed=Retail::ComposeTextures(initial,Fetch,nullptr,textures,warnings);
    if(!warnings.empty())std::fprintf(stderr,"COMPOSE race=%u sex=%u: %s\n",race,sex,warnings.c_str());
    bool body=false;
    for(const auto& texture:textures) {
        std::printf("TEXTURE race=%u sex=%u type=%u size=%ux%u\n",race,sex,texture.textureType,texture.image.width,texture.image.height);
        if(texture.textureType==1 && texture.image.width && texture.image.height && !texture.image.rgba.empty())body=true;
    }
    const bool ok=!failures&&!unresolved&&composed&&body;
    std::printf("%s race=%u sex=%u\n",ok?"PASS":"FAIL",race,sex);std::fflush(stdout);
    return ok;
}
int main(int argc,char** argv) {
    if(argc!=2 && argc!=3 && argc!=4)return 2;
    _putenv_s("WOW_RETAIL_CONFIG",argv[1]);_putenv_s("WOW_RETAIL_HEADLESS","1");
    if(!app::Status().loaded){std::fprintf(stderr,"%s\n",app::Status().error.c_str());return 1;}
    std::vector<uint32_t> races={1,2,3,4,5,6,7,8,9,10,11,22,25,26};
    if(argc==3) {
        const std::vector<uint32_t> allied={27,28,29,30,31,32,34,35,36,37,52,70,84,85,86,91};
        if(!std::strcmp(argv[2],"--allied"))races=allied;
        else if(!std::strcmp(argv[2],"--all"))races.insert(races.end(),allied.begin(),allied.end());
        else if(!std::strcmp(argv[2],"--world")) {
            races.insert(races.end(),allied.begin(),allied.end());
            for(uint32_t race : {75u,76u,95u,96u})races.push_back(race);
        }
        else return 2;
    }
    if(argc==4)races={uint32_t(std::atoi(argv[2]))};
    unsigned failures=0,total=0;
    for(uint32_t race:races)for(uint32_t sex=0;sex<2;++sex) {
        if(argc==4 && (race!=uint32_t(std::atoi(argv[2]))||sex!=uint32_t(std::atoi(argv[3]))))continue;
        ++total;if(!Check(race,sex))++failures;
    }
    std::printf("SUMMARY profiles=%u failures=%u; default model/skin/idle/material checks plus single-choice metadata; other assets stream on demand.\n",total,failures);
    return total && !failures?0:1;
}
