#include "Engine/Source/Retail/BodyShape.h"
#include "Engine/Source/Retail/EyeEffects.h"
#include "Engine/Source/Retail/RetailLookWire.h"
// ESTERIA_SELECTOR_RETRY_BIND
#include "WoW/Source/DB/Rec/CharSectionsRec.h"
// ESTERIA_SELECTOR_FOLLOWUP
// ESTERIA_SELECTOR_CONTROLS
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
// ESTERIA_SELECTOR_BATCH
// ESTERIA_CREATOR_REMAINING
// Native AMD64 appearance integration. Asset codecs are shared with the
// accepted Esteria helper; runtime access uses source types, never x86 offsets.
#include "EsteriaAppearance.h"
#include "WoW/Common/ComponentCore/RetailRace.h"
#include "CharacterComponent.h"
#include "WoW/Common/TextureCache.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/CImVector.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Data.h"
#include "WoW/Source/Glue/CharacterCreation.h"
#include "WoW/Source/Glue/CharacterSelection.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "WoW/Source/WowServices/RealmConnection.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <random>
#include "EsteriaCodecs/HighmountainAppearance.h"
#include "EsteriaCodecs/EarthenAppearance.h"
#include "EsteriaCodecs/HaranirAppearance.h"
#include "EsteriaCodecs/VulperaAppearance.h"
#include "EsteriaCodecs/CreatureAppearance.h"
#include "EsteriaCodecs/DemonHunterAppearance.h"
#include "EsteriaCodecs/NormalElfAppearance.h"
#include "EsteriaCodecs/OrcAppearanceV2.h"
#include "EsteriaCodecs/TaurenAppearance.h"
#include "EsteriaCodecs/HordeBatchAppearance.h"
#include "EsteriaPixelSource.h"

namespace {
template<class T> T& Field(void* data, unsigned offset) {
    return *reinterpret_cast<T*>(static_cast<unsigned char*>(data)+offset);
}
template<class T> T const& Field(void const* data, unsigned offset) {
    return *reinterpret_cast<T const*>(static_cast<unsigned char const*>(data)+offset);
}
void TraceSelector(char const* format, ...) {
    static std::mutex mutex;
    static FILE* file=[](){
        wchar_t module[MAX_PATH]{};if(!GetModuleFileNameW(nullptr,module,MAX_PATH))return static_cast<FILE*>(nullptr);
        auto slash=std::wcsrchr(module,L'\\');if(!slash)return static_cast<FILE*>(nullptr);
        std::wcscpy(slash+1,L"Logs\\EsteriaSelectorMaterials.log");FILE* out=nullptr;
        return _wfopen_s(&out,module,L"a")?static_cast<FILE*>(nullptr):out;
    }();
    if(!file)return;std::lock_guard<std::mutex> lock(mutex);SYSTEMTIME now{};GetLocalTime(&now);
    std::fprintf(file,"%02u:%02u:%02u.%03u ",now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);
    va_list args;va_start(args,format);std::vfprintf(file,format,args);va_end(args);std::fputc('\n',file);std::fflush(file);
}
void TraceDHTex(char const*, ...) {}
// Not in the original binary: the bin catalogs live in Data\Retail beside the executable and are named Aetheria*.bin. Each
// request tries Data\Retail\<Aetheria name>, Data\Retail\<name>, then the same two beside the executable, so a client
// folder in either layout (and with the old Esteria* names) keeps working.
FILE* CatalogFile(wchar_t const* name) {
    wchar_t base[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr,base,MAX_PATH)) return nullptr;
    auto slash=std::wcsrchr(base,L'\\');
    if (!slash) return nullptr;
    slash[1]=0;
    std::wstring requested(name);
    auto split=requested.find_last_of(L"\\/");
    std::wstring folder=split==std::wstring::npos ? L"" : requested.substr(0,split+1);
    std::wstring file=split==std::wstring::npos ? requested : requested.substr(split+1);
    std::wstring renamed=file.compare(0,7,L"Esteria")==0 ? L"Aetheria"+file.substr(7) : file;
    std::wstring folders[2]={folder,L""};
    if (folder.empty()) { folders[0]=L"Data\\Retail\\"; }
    for (auto const& dir : folders) {
        for (auto const* leaf : {&renamed,&file}) {
            std::wstring path=base+dir+*leaf;
            if (path.size()>=MAX_PATH) continue;
            FILE* opened=nullptr;
            if (!_wfopen_s(&opened,path.c_str(),L"rb") && opened) return opened;
        }
    }
    return nullptr;
}bool BloodElfProfile(unsigned race) { return race==10 || race==17 || race==24; }
bool NightElfProfile(unsigned race) { return race==4 || race==21; }
using Fields=std::array<std::uint8_t,13>;
Fields HighmountainFields(CCharacterComponent* c) {
    auto const& p=c->m_data.m_preferences;
    Fields fields{std::uint8_t(p.m_skinID),std::uint8_t(p.m_faceId),
        std::uint8_t(p.m_hairStyleId),std::uint8_t(p.m_hairColorId),std::uint8_t(p.m_facialHairId)};
    // Installed packed catalogs use all 64 bits, including their profile marker.
    // Retail eye effects have their own renderer state; do not strip legacy data.
    auto const appearance=p.m_esteriaExtra;
    for(unsigned i=0;i<8;++i) fields[i+5]=std::uint8_t(appearance>>(8*i));
    if(NormalElfAppearance::Uses(p.m_raceID)) {
        fields=(p.m_esteriaExtra & NormalElfAppearance::ModernMarker)
            ? NormalElfAppearance::Decode(p.m_raceID,p.m_sexID,fields)
            : NormalElfAppearance::Legacy(p.m_raceID,p.m_sexID,fields);
        fields=NormalElfAppearance::ApplyEyePalette(p.m_raceID,p.m_sexID,fields,p.m_classId);
    }
    if(p.m_raceID==60) fields=CreatureAppearance::DecodeKyrian(p.m_sexID,fields);
    return fields;
}
void SetHighmountainFields(CCharacterComponent* c,Fields const& input) {
    auto& p=c->m_data.m_preferences;
    Fields fields=p.m_raceID==60 ? CreatureAppearance::EncodeKyrian(p.m_sexID,input) : input;
    p.m_skinID=fields[0]; p.m_faceId=fields[1]; p.m_hairStyleId=fields[2];
    p.m_hairColorId=fields[3]; p.m_facialHairId=fields[4];
    p.m_esteriaExtra=0;
    for(unsigned i=0;i<8;++i) p.m_esteriaExtra|=std::uint64_t(fields[i+5])<<(8*i);
    if(NormalElfAppearance::Uses(p.m_raceID))
        p.m_esteriaExtra=NormalElfAppearance::Extra(p.m_raceID,p.m_sexID,fields);
    else if(OrcAppearanceV2::Uses(p.m_raceID) && OrcAppearanceV2::IsModern(fields))
        p.m_esteriaExtra=OrcAppearanceV2::Extra(fields);
    else if(p.m_raceID==18 && HordeBatchAppearance::IsModern(18,fields))
        p.m_esteriaExtra=VulperaBatchAppearance::Extra(fields);
}
bool OrcModern(CCharacterComponent* c) {
    return OrcAppearanceV2::Uses(c->m_data.m_preferences.m_raceID)
        && OrcAppearanceV2::IsModern(HighmountainFields(c));
}
bool TaurenModern(CCharacterComponent* c) {
    return c->m_data.m_preferences.m_raceID==6 &&
        (c->m_data.m_preferences.m_esteriaExtra>>56)==0xC6;
}
bool BatchModern(CCharacterComponent* c) {
    auto const& p=c->m_data.m_preferences;
    return HordeBatchAppearance::IsModern(p.m_raceID,p.m_esteriaExtra);
}
struct HaranirImages {
    Fields fields{};
    unsigned race=0,gender=2;
    std::uint64_t coverageFingerprint=0;
    std::array<std::string,13> paths;
    std::array<std::shared_ptr<void>,13> textures;
    std::string lower,upper;
};
// Engine resources are released by Forget on component destruction. Keeping
// the registry itself alive avoids static destruction after graphics shutdown.
auto& haranirImages=*new std::unordered_map<CCharacterComponent*,HaranirImages>;
void Refresh(CCharacterComponent* c);

    bool ExtendedRace(unsigned race)
    {
        return HordeBatchAppearance::Uses(race) || race == 6 || OrcAppearanceV2::Uses(race) || BloodElfProfile(race) || NightElfProfile(race) || race == 20 || race == 46 || race == 48 || race == 49 || race == 50 || race == 51
            || CreatureAppearance::Uses(race);
    }

    bool ValidateExtended(unsigned race, unsigned gender, std::array<std::uint8_t, 13> const& fields)
    {
        if (race == 30 && gender == 1) return true;
        if (HordeBatchAppearance::IsModern(race, fields))
            return HordeBatchAppearance::Validate(race, gender, fields);
        if (race == 6) return TaurenAppearance::Validate(gender, fields);
        if (OrcAppearanceV2::Uses(race)) return OrcAppearanceV2::Validate(race, gender, fields);
        if (NormalElfAppearance::Uses(race))
            return NormalElfAppearance::Validate(race, gender, fields);
        if (BloodElfProfile(race) || NightElfProfile(race))
            return DemonHunterAppearance::Validate(race, gender, fields);
        if (CreatureAppearance::Uses(race) && !(race == 30 && gender == 1))
        {
            std::array<std::uint8_t, 5> appearance{};
            std::copy_n(fields.begin(), appearance.size(), appearance.begin());
            // Renderer fields are logical; validate the corresponding wire layout.
            if (race == 60)
                appearance = CreatureAppearance::EncodeKyrian(gender, appearance);
            return std::all_of(fields.begin() + 5, fields.end(), [](std::uint8_t value) { return value == 0; })
                && CreatureAppearance::Validate(race, gender, appearance);
        }
        if (race == 20)
        {
            std::array<std::uint8_t, 5> vulpera{};
            std::copy_n(fields.begin(), vulpera.size(), vulpera.begin());
            return std::all_of(fields.begin() + 5, fields.end(), [](std::uint8_t value) { return value == 0; })
                && VulperaAppearance::Validate(gender, vulpera);
        }
        if (race == 50 || race == 51)
            return HaranirAppearance::Validate(gender, fields);
        std::array<std::uint8_t, 6> legacy{};
        std::copy_n(fields.begin(), legacy.size(), legacy.begin());
        return std::all_of(fields.begin() + 6, fields.end(), [](std::uint8_t value) { return value == 0; })
            && (race == 46 ? HighmountainAppearance::Validate(gender, legacy)
                : EarthenAppearance::Validate(gender, legacy));
    }


#include "EsteriaBatchControls.inl"
    struct Option
    {
        std::uint32_t offset;
        std::uint32_t count;
        std::uint32_t factor;
        std::uint32_t geometryOffset;
        std::uint32_t geometryChoices[32];
    };

    struct Profile
    {
        std::uint32_t race;
        std::uint32_t gender;
        std::uint32_t optionCount;
        Option options[16];
    };

    std::vector<Profile> const& Profiles()
    {
        static std::vector<Profile> const profiles = []
        {
            std::vector<Profile> result;
            for (auto name : {L"EsteriaAppearance.bin", L"EsteriaAddedAppearance.bin"}) {
            FILE* file = CatalogFile(name);
            if (!file) continue;
            std::uint32_t header[3] = {};
            if (std::fread(header, sizeof(header), 1, file) != 1 || header[0] != 0x50504145
                || header[1] != 2 || header[2] > 64)
            {
                std::fclose(file);
                return result;
            }
            for (std::uint32_t i = 0; i < header[2]; ++i)
            {
                Profile profile{};
                if (std::fread(&profile, sizeof(profile), 1, file) != 1)
                    break;
                bool valid = profile.race < 256 && profile.gender < 2
                    && profile.optionCount > 0 && profile.optionCount <= 16;
                for (std::uint32_t j = 0; valid && j < profile.optionCount; ++j)
                {
                    auto const& option = profile.options[j];
                    valid = (option.offset == 0x24 || option.offset == 0x28 || option.offset == 0x2C
                        || option.offset == 0x30 || option.offset == 0x34)
                        && option.count && option.factor && option.count <= 256
                        && option.factor <= 256 / option.count
                        && (!option.geometryOffset || (option.count <= 32
                            && option.geometryOffset >= 0x144 && option.geometryOffset < 0x190
                            && option.geometryOffset % 4 == 0));
                }
                if (valid)
                    result.push_back(profile);
            }
            std::fclose(file);
            }
            return result;
        }();
        return profiles;
    }

    struct Geometry
    {
        char model[128];
        std::vector<unsigned char> skin;
    };

    struct MaterialSelector
    {
        unsigned offset;
        unsigned factor;
        unsigned count;
        unsigned value;
    };

    struct AppearanceMaterial
    {
        unsigned race;
        unsigned gender;
        unsigned slot;
        MaterialSelector selectors[3];
        char path[128];
    };

    std::vector<AppearanceMaterial> const& AppearanceMaterials()
    {
        static std::vector<AppearanceMaterial> const materials = []
        {
            std::vector<AppearanceMaterial> result;
            FILE* file = CatalogFile(L"EsteriaAppearanceMaterials.bin");
            if (!file)
                return result;
            unsigned header[3] = {};
            if (std::fread(header, sizeof(header), 1, file) == 1 && header[0] == 0x544D4145
                && header[1] == 1 && header[2] <= 4096)
                for (unsigned i = 0; i < header[2]; ++i)
                {
                    AppearanceMaterial material{};
                    if (std::fread(&material, sizeof(material), 1, file) != 1)
                        break;
                    bool valid = material.race == 47 && material.gender < 2
                        && material.slot == 5 && material.path[0]
                        && std::memchr(material.path, 0, sizeof(material.path));
                    for (auto const& selector : material.selectors)
                        valid = valid && (selector.offset == 0x24 || selector.offset == 0x28
                            || selector.offset == 0x30) && selector.factor && selector.count
                            && selector.count <= 256 && selector.factor <= 256 / selector.count
                            && selector.value < selector.count;
                    if (valid)
                        result.push_back(material);
                }
            std::fclose(file);
            return result;
        }();
        return materials;
    }

    bool ValidGeometry(std::vector<unsigned char>& skin)
    {
        if (skin.size() < 48 || Field<unsigned>(skin.data(), 0) != 0x4E494B53)
            return false;
        unsigned const strides[] = {2, 2, 4, 48, 24};
        for (unsigned i = 0; i < 5; ++i)
        {
            unsigned count = Field<unsigned>(skin.data(), 4 + i * 8);
            unsigned offset = Field<unsigned>(skin.data(), 8 + i * 8);
            if (offset > skin.size() || count > (skin.size() - offset) / strides[i])
                return false;
        }
        unsigned vertices = Field<unsigned>(skin.data(), 4);
        unsigned indices = Field<unsigned>(skin.data(), 12);
        unsigned meshes = Field<unsigned>(skin.data(), 28);
        if (!vertices || vertices > 65535 || !indices || !meshes || meshes > 4096)
            return false;
        auto* sections = skin.data() + Field<unsigned>(skin.data(), 32);
        for (unsigned i = 0; i < meshes; ++i)
        {
            auto* section = sections + i * 48;
            unsigned start = Field<std::uint16_t>(section, 8)
                | (Field<std::uint16_t>(section, 2) << 16);
            if (start > indices || Field<std::uint16_t>(section, 10) > indices - start
                || unsigned(Field<std::uint16_t>(section, 4)) + Field<std::uint16_t>(section, 6) > vertices
                || Field<std::uint16_t>(section, 12) > 75)
                return false;
        }
        return true;
    }

    std::vector<Geometry>& Geometries()
    {
        static std::vector<Geometry> geometries = []
        {
            std::vector<Geometry> result;
            FILE* file = CatalogFile(L"EsteriaAppearanceGeometry.bin");
            if (!file)
                return result;
            unsigned header[3] = {};
            if (std::fread(header, sizeof(header), 1, file) == 1 && header[0] == 0x4D474145
                && header[1] == 1 && header[2] <= 64)
                for (unsigned i = 0; i < header[2]; ++i)
                {
                    Geometry geometry{};
                    unsigned length = 0;
                    if (std::fread(geometry.model, sizeof(geometry.model), 1, file) != 1
                        || !std::memchr(geometry.model, 0, sizeof(geometry.model))
                        || std::fread(&length, sizeof(length), 1, file) != 1 || length > 16 * 1024 * 1024)
                        break;
                    geometry.skin.resize(length);
                    if (std::fread(geometry.skin.data(), length, 1, file) != 1 || !ValidGeometry(geometry.skin))
                        break;
                    result.push_back(std::move(geometry));
                }
            std::fclose(file);
            return result;
        }();
        return geometries;
    }

    struct HighmountainSelection
    {
        unsigned gender;
        unsigned kind; // 0: geometry group, 1: replaceable material
        unsigned target;
        unsigned value;
        unsigned choices[3]; // {option, value} packed as two uint16s; 0xffffffff means unconditional
        char path[128];
    };

    std::vector<HighmountainSelection> ReadSelections(wchar_t const* name)
    {
        return [name]
        {
            std::vector<HighmountainSelection> result;
            FILE* file = CatalogFile(name);
            if (!file)
                return result;
            unsigned header[3] = {};
            if (std::fread(header, sizeof(header), 1, file) == 1 && header[0] == 0x314D4845
                && header[1] == 1 && header[2] <= 16384)
                for (unsigned i = 0; i < header[2]; ++i)
                {
                    HighmountainSelection record{};
                    if (std::fread(&record, sizeof(record), 1, file) != 1)
                        break;
                    bool valid = record.gender < 2 && record.kind <= 4
                        && (record.kind == 4 ? record.target && record.value > 0 && record.value < 52
                            : record.kind == 3 ? record.target < 13 && record.value >= 16
                            && std::memchr(record.path, 0, sizeof(record.path))
                            : record.kind == 2 ? record.target < 5200 && record.value >= 10000
                            && record.value < 30000 : record.kind ? record.target < 32 && record.path[0]
                            && std::memchr(record.path, 0, sizeof(record.path)) : record.target < 52
                            && record.value >= record.target * 100 && record.value < record.target * 100 + 100);
                    for (unsigned selector : record.choices)
                        valid = valid && (selector == 0xffffffff || (selector & 0xffff) < 32
                            && (selector >> 16) < 256);
                    if (valid)
                        result.push_back(record);
                }
            std::fclose(file);
            return result;
        }();
    }

    std::vector<HighmountainSelection> const& HighmountainSelections()
    {
        static auto const records = ReadSelections(L"EsteriaHighmountain.bin");
        return records;
    }

    std::vector<HighmountainSelection> const& EarthenSelections()
    {
        static auto const records = ReadSelections(L"EsteriaEarthen.bin");
        return records;
    }

    std::vector<HighmountainSelection> const& HaranirSelections()
    {
        static auto const records = ReadSelections(L"EsteriaHaranir.bin");
        return records;
    }

    std::vector<HighmountainSelection> const& VulperaSelections()
    {
        static auto const records = ReadSelections(L"EsteriaVulpera.bin");
        return records;
    }

    std::vector<HighmountainSelection> const& RetailSelections(unsigned race, unsigned gender = 0)
    {
        static auto const UndeadBatch = ReadSelections(L"EsteriaUndeadBatch.bin");
        static auto const TrollBatch = ReadSelections(L"EsteriaTrollBatch.bin");
        static auto const GoblinBatch = ReadSelections(L"EsteriaGoblinBatch.bin");
        static auto const PandarenBatch = ReadSelections(L"EsteriaPandarenBatch.bin");
        static auto const VulperaBatch = ReadSelections(L"EsteriaVulperaBatch.bin");
        static auto const ZandalariBatch = ReadSelections(L"EsteriaZandalariBatch.bin");
        static auto const NightborneBatch = ReadSelections(L"EsteriaNightborneBatch.bin");
        static auto const ManariBatch = ReadSelections(L"EsteriaManariBatch.bin");
        static auto const EarthenBatch = ReadSelections(L"EsteriaEarthenBatch.bin");
        static auto const HumanBatch = ReadSelections(L"EsteriaHumanBatch.bin");
        static auto const DwarfBatch = ReadSelections(L"EsteriaDwarfBatch.bin");
        static auto const GnomeBatch = ReadSelections(L"EsteriaGnomeBatch.bin");
        static auto const DraeneiBatch = ReadSelections(L"EsteriaDraeneiBatch.bin");
        static auto const WorgenBatch = ReadSelections(L"EsteriaWorgenBatch.bin");
        static auto const VoidelfBatch = ReadSelections(L"EsteriaVoidelfBatch.bin");
        static auto const DarkironBatch = ReadSelections(L"EsteriaDarkironBatch.bin");
        static auto const KultiranBatch = ReadSelections(L"EsteriaKultiranBatch.bin");
        static auto const LightforgedBatch = ReadSelections(L"EsteriaLightforgedBatch.bin");
        static auto const Tauren = ReadSelections(L"EsteriaTauren.bin");
        static auto const OrcMale = ReadSelections(L"EsteriaOrcMaleSelections.bin");
        static auto const OrcFemale = ReadSelections(L"EsteriaOrcFemaleSelections.bin");
        static auto const MagharMale = ReadSelections(L"EsteriaMagharMaleSelections.bin");
        static auto const MagharFemale = ReadSelections(L"EsteriaMagharFemaleSelections.bin");
        static auto const normalBloodElfMale = ReadSelections(L"EsteriaNormalBloodElfMaleSelections.bin");
        static auto const normalBloodElfFemale = ReadSelections(L"EsteriaNormalBloodElfFemaleSelections.bin");
        static auto const normalNightElfMale = ReadSelections(L"EsteriaNormalNightElfMaleSelections.bin");
        static auto const normalNightElfFemale = ReadSelections(L"EsteriaNormalNightElfFemaleSelections.bin");
        static auto const demonHunter = ReadSelections(L"EsteriaDHBloodElfMaleSelections.bin");
        static auto const demonHunterFemale = ReadSelections(L"EsteriaDHBloodElfFemaleSelections.bin");
        static auto const nightElfDemonHunter = ReadSelections(L"EsteriaDHNightElfMaleSelections.bin");
        static auto const nightElfDemonHunterFemale = ReadSelections(L"EsteriaDHNightElfFemaleSelections.bin");
        static auto const sethrak = ReadSelections(L"EsteriaSethrak.bin");
        static auto const gnoll = ReadSelections(L"EsteriaGnoll.bin");
        static auto const drakonid = ReadSelections(L"EsteriaDrakonid.bin");
        static auto const tarasek = ReadSelections(L"EsteriaTarasek.bin");
        static auto const saberon = ReadSelections(L"EsteriaSaberon.bin");
        static auto const kyrian = ReadSelections(L"Data\\Retail\\EsteriaKyrian.bin");
        static auto const venthyr = ReadSelections(L"Data\\Retail\\EsteriaVenthyr.bin");
        static auto const naga = ReadSelections(L"EsteriaNaga.bin");
        static auto const tuskarr = ReadSelections(L"EsteriaTuskarr.bin");
        static auto const vrykul = ReadSelections(L"EsteriaVrykul.bin");
        static auto const human = ReadSelections(L"EsteriaThinHuman.bin");
        switch (race)
        {
            case 5: return UndeadBatch;
            case 8: return TrollBatch;
            case 9: return GoblinBatch;
            case 18: return VulperaBatch;
            case 22: return ZandalariBatch;
            case 26: return NightborneBatch;
            case 28: return ManariBatch;
            case 29:
            case 31: return EarthenBatch;
            case 1: return HumanBatch;
            case 3: return DwarfBatch;
            case 7: return GnomeBatch;
            case 11: return DraeneiBatch;
            case 12: return WorgenBatch;
            case 13: return VoidelfBatch;
            case 16: return DarkironBatch;
            case 25: return KultiranBatch;
            case 27: return LightforgedBatch;
            case 6: return Tauren;
            case 2: return gender ? OrcFemale : OrcMale;
            case 14: return gender ? MagharFemale : MagharMale;
            case 24:
            case 10: return gender ? normalBloodElfFemale : normalBloodElfMale;
            case 4: return gender ? normalNightElfFemale : normalNightElfMale;
            case 17: return gender ? demonHunterFemale : demonHunter;
            case 21: return gender == 0 ? nightElfDemonHunter : nightElfDemonHunterFemale;
            case 62: return sethrak;
            case 63: return gnoll;
            case 69:
            case 70: return drakonid;
            case 72: return tarasek;
            case 60: return kyrian;
            case 61: return venthyr;
            case 64: return saberon;
            case 54: return naga;
            case 55: return tuskarr;
            case 56:
            case 57: return vrykul;
            case 30:
            case 58:
            case 59: return human;
            case 19:
            case 20: return PandarenBatch;
            default: return HaranirSelections();
        }
    }

    wchar_t const* RetailTextureBank(unsigned race, unsigned gender = 0)
    {
        switch (race)
        {
            case 5: return L"EsteriaUndeadBatchTextures.bin";
            case 8: return L"EsteriaTrollBatchTextures.bin";
            case 9: return L"EsteriaGoblinBatchTextures.bin";
            case 18: return L"EsteriaVulperaBatchTextures.bin";
            case 22: return L"EsteriaZandalariBatchTextures.bin";
            case 26: return L"EsteriaNightborneBatchTextures.bin";
            case 28: return L"EsteriaManariBatchTextures.bin";
            case 29:
            case 31: return L"EsteriaEarthenBatchTextures.bin";
            case 1: return L"EsteriaHumanBatchTextures.bin";
            case 3: return L"EsteriaDwarfBatchTextures.bin";
            case 7: return L"EsteriaGnomeBatchTextures.bin";
            case 11: return L"EsteriaDraeneiBatchTextures.bin";
            case 12: return L"EsteriaWorgenBatchTextures.bin";
            case 13: return L"EsteriaVoidelfBatchTextures.bin";
            case 16: return L"EsteriaDarkironBatchTextures.bin";
            case 25: return L"EsteriaKultiranBatchTextures.bin";
            case 27: return L"EsteriaLightforgedBatchTextures.bin";
            case 6: return L"EsteriaTaurenTextures.bin";
            case 2: return gender ? L"EsteriaOrcFemaleTextures.bin" : L"EsteriaOrcMaleTextures.bin";
            case 14: return gender ? L"EsteriaMagharFemaleTextures.bin" : L"EsteriaMagharMaleTextures.bin";
            case 24: return gender ? L"EsteriaHighElfFemaleTextures.bin" : L"EsteriaHighElfMaleTextures.bin";
            case 10: return gender ? L"EsteriaNormalBloodElfFemaleTextures.bin" : L"EsteriaNormalBloodElfMaleTextures.bin";
            case 4: return gender ? L"EsteriaNormalNightElfFemaleTextures.bin" : L"EsteriaNormalNightElfMaleTextures.bin";
            case 17: return gender ? L"EsteriaDHBloodElfFemaleTextures.bin" : L"EsteriaDHBloodElfMaleTextures.bin";
            case 21: return gender == 0 ? L"EsteriaDHNightElfMaleTextures.bin" : L"EsteriaDHNightElfFemaleTextures.bin";
            case 62: return L"EsteriaSethrakTextures.bin";
            case 63: return L"EsteriaGnollTextures.bin";
            case 69:
            case 70: return L"EsteriaDrakonidTextures.bin";
            case 72: return L"EsteriaTarasekTextures.bin";
            case 60: return L"Data\\Retail\\EsteriaKyrianTextures.bin";
            case 61: return L"Data\\Retail\\EsteriaVenthyrTextures.bin";
            case 64: return L"EsteriaSaberonTextures.bin";
            case 54: return L"EsteriaNagaTextures.bin";
            case 55: return L"EsteriaTuskarrTextures.bin";
            case 56:
            case 57: return L"EsteriaVrykulTextures.bin";
            case 30:
            case 58:
            case 59: return L"EsteriaThinHumanTextures.bin";
            case 19:
            case 20: return L"EsteriaPandarenBatchTextures.bin";
            default: return L"EsteriaHaranirTextures.bin";
        }
    }

    char const* RetailCacheFamily(unsigned race)
    {
        switch (race)
        {
            case 5: return "UndeadBatch";
            case 8: return "TrollBatch";
            case 9: return "GoblinBatch";
            case 18: return "VulperaBatch";
            case 22: return "ZandalariBatch";
            case 26: return "NightborneBatch";
            case 28: return "ManariBatch";
            case 29:
            case 31: return "EarthenBatch";
            case 1: return "HumanBatch";
            case 3: return "DwarfBatch";
            case 7: return "GnomeBatch";
            case 11: return "DraeneiBatch";
            case 12: return "WorgenBatch";
            case 13: return "VoidelfBatch";
            case 16: return "DarkironBatch";
            case 25: return "KultiranBatch";
            case 27: return "LightforgedBatch";
            case 6: return "Tauren";
            case 2: return "Orc";
            case 14: return "Maghar";
            case 24: return "HighElf";
            case 10: return "NormalBloodElf";
            case 4: return "NormalNightElf";
            case 17: return "DemonHunterBloodElf";
            case 21: return "DemonHunterNightElf";
            case 62: return "Sethrak";
            case 63: return "Gnoll";
            case 69:
            case 70: return "Drakonid";
            case 72: return "Tarasek";
            case 60: return "Kyrian";
            case 61: return "Venthyr";
            case 64: return "Saberon";
            case 54: return "Naga";
            case 55: return "Tuskarr";
            case 56:
            case 57: return "Vrykul";
            case 30:
            case 58:
            case 59: return "ThinHuman";
            case 19:
            case 20: return "PandarenBatch";
            default: return "Haranir";
        }
    }

// Native clothing rows keep seven ItemDisplayInfo IDs at row+28 after
// releasing their temporary texture pointers. Cache decoded coverage by that
// persistent identity, region and layer; native unequip clears the ID.
struct DHClothingAlpha
{
    unsigned width = 0, height = 0;
    std::vector<unsigned char> alpha;
    std::uint64_t fingerprint = 0;
};
std::unordered_map<std::uint64_t, DHClothingAlpha> dhClothingAlpha;
std::uint64_t DHClothingKey(unsigned id, unsigned region, unsigned layer, unsigned gender)
{
    return (std::uint64_t(id) << 32) | (gender << 24) | (region << 8) | layer;
}

bool DHLayerAlpha(void* layer, std::vector<unsigned char>& alpha, unsigned& width, unsigned& height)
{
    if (!layer) return false;
    auto* bytes = reinterpret_cast<unsigned char*>(static_cast<CACHEENTRY*>(layer)->m_data);
    unsigned size = static_cast<CACHEENTRY*>(layer)->m_size;
    if (!bytes || size < 148 || Field<unsigned>(bytes, 0) != 0x32504C42) return false;
    width = Field<unsigned>(bytes, 12); height = Field<unsigned>(bytes, 16);
    if (!width || !height || width > 2048 || height > 2048) return false;
    unsigned offset = Field<unsigned>(bytes, 20), length = Field<unsigned>(bytes, 84);
    if (offset > size || length > size - offset) return false;
    unsigned count = width * height;
    alpha.assign(count, 255);
    auto* data = bytes + offset;
    if (bytes[9] == 0) return true;
    if (bytes[8] == 1)
    {
        unsigned bits = bytes[9];
        if ((bits != 1 && bits != 4 && bits != 8) || length < count + (count * bits + 7) / 8) return false;
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned value = (data[count + (i * bits) / 8] >> ((i * bits) % 8)) & ((1u << bits) - 1);
            alpha[i] = static_cast<unsigned char>(value * 255 / ((1u << bits) - 1));
        }
        return true;
    }
    if (bytes[8] == 3)
    {
        if (length < count * 4) return false;
        for (unsigned i = 0; i < count; ++i) alpha[i] = data[i * 4 + 3];
        return true;
    }
    if (bytes[8] != 2) return false;
    unsigned blockBytes = bytes[10] == 0 ? 8 : 16;
    unsigned across = (width + 3) / 4, down = (height + 3) / 4;
    if (length < across * down * blockBytes || (bytes[10] != 0 && bytes[10] != 1 && bytes[10] != 7)) return false;
    for (unsigned by = 0; by < down; ++by)
        for (unsigned bx = 0; bx < across; ++bx)
        {
            auto* block = data + (by * across + bx) * blockBytes;
            unsigned values[8]{}; std::uint64_t selectors = 0;
            if (bytes[10] == 7)
            {
                values[0] = block[0]; values[1] = block[1];
                if (values[0] > values[1])
                    for (unsigned j = 2; j < 8; ++j) values[j] = ((8-j)*values[0] + (j-1)*values[1]) / 7;
                else
                {
                    for (unsigned j = 2; j < 6; ++j) values[j] = ((6-j)*values[0] + (j-1)*values[1]) / 5;
                    values[6] = 0; values[7] = 255;
                }
                for (unsigned j = 0; j < 6; ++j) selectors |= std::uint64_t(block[j + 2]) << (8 * j);
            }
            for (unsigned pixel = 0; pixel < 16; ++pixel)
            {
                unsigned x = bx * 4 + pixel % 4, y = by * 4 + pixel / 4;
                if (x >= width || y >= height) continue;
                unsigned value = 255;
                if (bytes[10] == 7) value = values[(selectors >> (pixel * 3)) & 7];
                else if (bytes[10] == 1) value = ((block[pixel / 2] >> (4 * (pixel % 2))) & 15) * 17;
                else if (Field<std::uint16_t>(block, 0) <= Field<std::uint16_t>(block, 2)
                    && ((Field<unsigned>(block, 4) >> (pixel * 2)) & 3) == 3) value = 0;
                alpha[y * width + x] = static_cast<unsigned char>(value);
            }
        }
    return true;
}

std::uint64_t DHCoverageFingerprint(CCharacterComponent* character)
{
    std::uint64_t hash = 14695981039346656037ull;
    auto mix = [&](std::uint64_t value) { hash = (hash ^ value) * 1099511628211ull; };
    mix(EsteriaComponentTextureSize());
    for (unsigned region = 0; region < 9; ++region)
    {
        auto& row = character->m_section[region];
        auto* rect = reinterpret_cast<unsigned*>(&CCharacterComponent::s_sectionInfoRaw[region]);
        for (unsigned k = 0; k < 4; ++k) mix(rect[k]);
        for (unsigned layerIndex = 0; layerIndex < 7; ++layerIndex)
        {
            unsigned id = row.m_layerItemDisplayId[layerIndex];
            mix(id);
            if (!id) continue;
            auto key = DHClothingKey(id, region, layerIndex, character->m_data.m_preferences.m_sexID);
            auto found = dhClothingAlpha.find(key);
            void* layer = row.m_layerTex[layerIndex];
            if (layer && found == dhClothingAlpha.end())
            {
                DHClothingAlpha coverage;
                if (DHLayerAlpha(layer, coverage.alpha, coverage.width, coverage.height))
                {
                    coverage.fingerprint = 14695981039346656037ull;
                    for (auto alpha : coverage.alpha)
                        coverage.fingerprint = (coverage.fingerprint ^ alpha) * 1099511628211ull;
                    // Bound retained texture data; cleared entries fall back to
                    // covered regions until the native loader supplies them again.
                    if (dhClothingAlpha.size() >= 128) dhClothingAlpha.clear();
                    found = dhClothingAlpha.emplace(key, std::move(coverage)).first;
                    TraceDHTex("DH retained clothing alpha id=%u region=%u layer=%u size=%ux%u",
                        id, region, layerIndex, found->second.width, found->second.height);
                }
            }
            mix(found == dhClothingAlpha.end() ? 0 : found->second.fingerprint);
        }
    }
    return hash;
}

void DHMaskClothing(CCharacterComponent* character, std::vector<unsigned char>& pixels, unsigned width, unsigned height)
{
    unsigned nativeSide = 512;
    if (!nativeSide || nativeSide > 2048) { std::fill(pixels.begin(), pixels.end(), 0); return; }
    unsigned layers = 0, fallback = 0, retained = 0;
    for (unsigned region = 0; region < 9; ++region)
    {
        auto& row = character->m_section[region];
        auto* rect = reinterpret_cast<unsigned*>(&CCharacterComponent::s_sectionInfoRaw[region]);
        unsigned x0 = rect[0] * width / nativeSide, y0 = rect[1] * height / nativeSide;
        unsigned rw = rect[2] * width / nativeSide, rh = rect[3] * height / nativeSide;
        if (!rw || !rh || x0 > width || rw > width - x0 || y0 > height || rh > height - y0) continue;
        for (unsigned layerIndex = 0; layerIndex < 7; ++layerIndex)
        {
            unsigned id = row.m_layerItemDisplayId[layerIndex];
            if (!id) continue;
            auto found = dhClothingAlpha.find(DHClothingKey(id, region, layerIndex, character->m_data.m_preferences.m_sexID));
            bool decoded = found != dhClothingAlpha.end();
            ++layers; fallback += !decoded;
            retained += decoded && !row.m_layerTex[layerIndex];
            // An equipped layer without retained alpha blocks the region.
            // Pointer release after compositing never means clothing removal.
            for (unsigned y = 0; y < rh; ++y)
                for (unsigned x = 0; x < rw; ++x)
                {
                    unsigned a = 255;
                    if (decoded)
                    {
                        auto const& coverage = found->second;
                        a = coverage.alpha[(y * coverage.height / rh) * coverage.width
                            + x * coverage.width / rw];
                    }
                    unsigned at = ((y0 + y) * width + x0 + x) * 4;
                    for (unsigned channel = 0; channel < 3; ++channel)
                        pixels[at + channel] = static_cast<unsigned char>((pixels[at + channel] * (255 - a) + 127) / 255);
                }
        }
    }
    TraceDHTex("DH tattoo clothing coverage layers=%u pending-or-unsupported=%u retained-after-release=%u", layers, fallback, retained);
}

bool WriteHaranirBlp(std::string const& relative, std::vector<unsigned char> pixels,
    unsigned width, unsigned height, bool indexed, bool indexedAlpha = false)
{
    char executable[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, executable, MAX_PATH))
        return false;
    char* slash = std::strrchr(executable, '\\');
    if (!slash)
        return false;
    slash[1] = 0;
    std::string absolute = executable + relative;
    FILE* existing = nullptr;
    if (!fopen_s(&existing, absolute.c_str(), "rb"))
    {
        unsigned char header[24]{};
        bool cached = std::fread(header, sizeof(header), 1, existing) == 1
            && !std::memcmp(header, "BLP2", 4) && header[8] == (indexed ? 1 : 3)
            && header[9] == (indexedAlpha ? 8 : indexed ? 0 : 8) && header[10] == 8
            && Field<unsigned>(header, 20) == 1172 && header[11] == 1
            && Field<unsigned>(header, 12) == width && Field<unsigned>(header, 16) == height;
        std::fclose(existing);
        if (cached)
            return true;
    }
    using Microsoft::WRL::ComPtr;
    struct Apartment
    {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment()
        {
            if (SUCCEEDED(result))
                CoUninitialize();
        }
    } apartment;
    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE)
        return false;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICPalette> palette;
    if (indexed && (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory))) || FAILED(factory->CreatePalette(&palette))))
        return false;
    std::vector<unsigned char> data(1172);
    std::memcpy(data.data(), "BLP2", 4);
    Field<unsigned>(data.data(), 4) = 1;
    data[8] = indexed ? 1 : 3;
    data[9] = indexedAlpha ? 8 : indexed ? 0 : 8;
    data[10] = 8;
    data[11] = 1;
    Field<unsigned>(data.data(), 12) = width;
    Field<unsigned>(data.data(), 16) = height;
    for (unsigned mip = 0; mip < 16; ++mip)
    {
        std::vector<unsigned char> level;
        if (indexed)
        {
            ComPtr<IWICBitmap> bitmap;
            ComPtr<IWICFormatConverter> converter;
            if (FAILED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppRGBA,
                width * 4, static_cast<UINT>(pixels.size()), pixels.data(), &bitmap)))
                return false;
            if (!mip)
            {
                if (FAILED(palette->InitializeFromBitmap(bitmap.Get(), 256, FALSE)))
                    return false;
                WICColor colors[256]{};
                UINT count = 0;
                if (FAILED(palette->GetColors(256, colors, &count)))
                    return false;
                for (unsigned i = 0; i < count; ++i)
                    Field<unsigned>(data.data(), 148 + i * 4) = colors[i] & 0xffffff;
            }
            level.resize(width * height);
            if (FAILED(factory->CreateFormatConverter(&converter))
                || FAILED(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat8bppIndexed,
                    WICBitmapDitherTypeNone, palette.Get(), 0, WICBitmapPaletteTypeCustom))
                || FAILED(converter->CopyPixels(nullptr, width, static_cast<UINT>(level.size()), level.data())))
                return false;
            if (indexedAlpha)
                for (std::size_t i = 3; i < pixels.size(); i += 4)
                    level.push_back(pixels[i]);
        }
        else
        {
            level = pixels;
            for (unsigned i = 0; i < level.size(); i += 4)
                std::swap(level[i], level[i + 2]);
        }
        Field<unsigned>(data.data(), 20 + mip * 4) = static_cast<unsigned>(data.size());
        Field<unsigned>(data.data(), 84 + mip * 4) = static_cast<unsigned>(level.size());
        data.insert(data.end(), level.begin(), level.end());
        if (width == 1 || height == 1)
            break;
        unsigned nextWidth = (std::max)(1u, width / 2);
        unsigned nextHeight = (std::max)(1u, height / 2);
        std::vector<unsigned char> smaller(nextWidth * nextHeight * 4);
        for (unsigned y = 0; y < nextHeight; ++y)
            for (unsigned x = 0; x < nextWidth; ++x)
                for (unsigned channel = 0; channel < 4; ++channel)
                {
                    unsigned sum = 0;
                    for (unsigned dy = 0; dy < 2; ++dy)
                        for (unsigned dx = 0; dx < 2; ++dx)
                            sum += pixels[((y * 2 + dy) * width + x * 2 + dx) * 4 + channel];
                    smaller[(y * nextWidth + x) * 4 + channel] = static_cast<unsigned char>(sum / 4);
                }
        pixels = std::move(smaller);
        width = nextWidth;
        height = nextHeight;
    }
    for (std::size_t i = std::strlen(executable); i < absolute.size(); ++i)
        if (absolute[i] == '\\')
            CreateDirectoryA(absolute.substr(0, i).c_str(), nullptr);
    std::string temporary = absolute + ".next";
    FILE* file = nullptr;
    if (fopen_s(&file, temporary.c_str(), "wb"))
        return false;
    bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    written = std::fclose(file) == 0 && written;
    if (!written || !MoveFileExA(temporary.c_str(), absolute.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileA(temporary.c_str());
        return false;
    }
    return true;
}

template<class Options>
bool ComposeHaranir(CCharacterComponent* character, Options const& options, HaranirImages& images)
{
    auto fields = HighmountainFields(character);
    unsigned gender = character->m_data.m_preferences.m_sexID;
    unsigned race = character->m_data.m_preferences.m_raceID;
    if (!ValidateExtended(race, gender, fields))
    {
        if (BloodElfProfile(race))
            TraceDHTex("Compose rejected choices character=%p gender=%u fields=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                character, gender, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5],
                fields[6], fields[7], fields[8], fields[9], fields[10], fields[11], fields[12]);
        return false;
    }
    if (OrcModern(character))
    {
        static unsigned traces = 0;
        if (traces++ < 200)
            TraceDHTex("Orc compose begin character=%p gender=%u fields=%u,%u,%u,%u,%u,%u,%u,%u",
                character, gender, fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7]);
    }
    std::uint64_t const coverageFingerprint = (BloodElfProfile(race) || NightElfProfile(race) || OrcModern(character) || BatchModern(character))
        ? DHCoverageFingerprint(character) : 0;
    if (images.race == race && images.gender == gender && images.fields == fields
        && images.coverageFingerprint == coverageFingerprint)
    {
        if (BloodElfProfile(race))
            TraceDHTex("Compose cache hit character=%p fields=%u,%u,%u,%u,%u", character,
                fields[0], fields[1], fields[2], fields[3], fields[4]);
        return true;
    }
    FILE* input = CatalogFile(RetailTextureBank(race, gender));
    if (!input)
    {
        if (BloodElfProfile(race))
            TraceDHTex("Compose cannot open texture bank character=%p", character);
        return false;
    }
    unsigned header[4]{};
    bool valid = std::fread(header, sizeof(header), 1, input) == 1 && header[0] == 0x31544845
        && header[1] == 1 && header[2] <= 4096;
    if (BloodElfProfile(race))
        TraceDHTex("Compose begin character=%p gender=%u fields=%u,%u,%u,%u,%u header=%08x/%u/%u valid=%u",
            character, gender, fields[0], fields[1], fields[2], fields[3], fields[4],
            header[0], header[1], header[2], valid ? 1 : 0);
    HaranirImages next;
    next.fields = fields;
    next.race = race;
    next.gender = gender;
    next.coverageFingerprint = coverageFingerprint;
    auto decompress = reinterpret_cast<LONG(WINAPI*)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG)>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlDecompressBuffer"));
    for (unsigned group = 0; valid && group < next.paths.size(); ++group)
    {
        std::vector<unsigned char> pixels;
        unsigned width = 0;
        unsigned height = 0;
        std::uint64_t seen = 0;
        unsigned matchedRows = 0;
        bool const orc = OrcModern(character);
        bool const tauren = TaurenModern(character);
        bool const batch = BatchModern(character);
        // Bump the cache generation whenever the compositor changes so the client
        // cannot keep using BLPs baked by an older blend/layout implementation.
        std::uint64_t hash = 14695981039346656037ull ^ header[3] ^ 5;
        // The BE male DH texture now preserves Retail's original U mapping.
        // Use a new name so the client cannot retain the old horizontal remap.
        if (BloodElfProfile(race) && group == 2)
            hash = (hash ^ 0x4448505245535855ull) * 1099511628211ull;
        if (BloodElfProfile(race) && group == 3)
            hash = (hash ^ 0x4448455945535131ull) * 1099511628211ull;
        // Hair target 12 carries transparency. Its earlier raw-BGRA cache loaded
        // successfully but rendered white in the Wrath client. Give the
        // indexed-alpha version a distinct filename so old cache files cannot win.
        if (BloodElfProfile(race) && group == 4)
            hash = (hash ^ 0x4448414c50484138ull) * 1099511628211ull;
        if (BloodElfProfile(race) && group == 4)
            hash = (hash ^ 0x44485354594c4532ull ^ fields[3]) * 1099511628211ull;
        for (auto const& record : RetailSelections(race, gender))
        {
            if (record.kind != 3 || record.gender != gender || record.target != group)
                continue;
            bool selected = true;
            for (unsigned selector : record.choices)
                if (selector != 0xffffffff)
                {
                    unsigned index = selector & 65535;
                    if (index >= options.size())
                    {
                        selected = false;
                        break;
                    }
                    auto const& option = options[index];
                    selected = selected && fields[option.field] / option.factor % option.count == selector >> 16;
                }
            unsigned target = 0;
            unsigned blend = 0;
            unsigned classMask = 0xffffffff;
            int const parts = sscanf_s(record.path, "%u:%u:%u", &target, &blend, &classMask);
            unsigned const characterClass = character->m_data.m_preferences.m_classId;
            if (batch && (parts != 3 || characterClass >= 32 || !(classMask & (1u << characterClass))))
                continue;
            if (!selected || parts < 2 || target >= 64
                || (seen & (std::uint64_t{1} << target)))
                continue;
            // Tattoo target 16 uses UsageType 0 albedo, with authored alpha.
            // UsageType 2 is its emissive map and cannot substitute for albedo.
            // Retail blend 9 is straight-alpha source-over, not inferred coverage.
            bool const demonHunterTattoo = BloodElfProfile(race) && group == 0
                && target == 16 && blend == 9;
            bool const nightElfTattooColor = NightElfProfile(race) && group == 0
                && target == 16 && blend == 9;
            bool const nightElfTattooPattern = NightElfProfile(race) && group == 0
                && target == 18 && blend == 4;
            bool const nightElfMarkings = NightElfProfile(race) && (group == 0 || group == 1)
                && target == 17 && blend == 4;
            bool const nightElfOverlay = NightElfProfile(race) && (group == 0 || group == 1)
                && (target == 2 || target == 36) && blend == 6;
            bool const dhIris = (BloodElfProfile(race) || NightElfProfile(race) || orc || tauren || batch || race == 50 || race == 51) && group == 3;
            bool const dhGlow = ((BloodElfProfile(race) || NightElfProfile(race)) && group == 4 && target == 16) || (orc && (group == 4 || group == 6)) || (batch && (group == 7 || group == 8));
            if (blend != 1 && blend != 15 && !demonHunterTattoo
                && !nightElfTattooColor && !nightElfTattooPattern && !nightElfMarkings && !nightElfOverlay
                && !(dhIris && target == 44 && blend == 9) && !dhGlow
                && !(orc && (blend == 4 || blend == 6 || blend == 9)) && !(tauren && blend == 9) && !(batch && (blend == 6 || blend == 7 || blend == 9)))
                continue;
            ++matchedRows;
            seen |= std::uint64_t{1} << target;
            unsigned item[3]{};
            if (!decompress || std::fseek(input, record.value, SEEK_SET)
                || std::fread(item, sizeof(item), 1, input) != 1 || !item[0] || !item[1]
                || item[0] > 512 || item[1] > 512 || item[2] > 2 * 1024 * 1024)
            {
                if (BloodElfProfile(race))
                    TraceDHTex("Compose invalid layer header group=%u target=%u offset=%u dims=%ux%u packed=%u decompress=%u",
                        group, target, record.value, item[0], item[1], item[2], decompress != nullptr);
                valid = false;
                break;
            }
            std::vector<unsigned char> packed(item[2]);
            std::vector<unsigned char> layer(item[0] * item[1] * 4);
            ULONG size = 0;
            if (std::fread(packed.data(), 1, packed.size(), input) != packed.size()
                || decompress(2, layer.data(), static_cast<ULONG>(layer.size()), packed.data(), item[2], &size)
                || size != layer.size())
            {
                if (BloodElfProfile(race))
                    TraceDHTex("Compose decompression failed group=%u target=%u offset=%u size=%u expected=%u",
                        group, target, record.value, size, layer.size());
                valid = false;
                break;
            }
            hash = (hash ^ record.value) * 1099511628211ull;
            // Bank offsets can be reused after replacing glow maps with albedo.
            // Give the corrected tattoo composition its own disk-cache identity.
            if (demonHunterTattoo)
                hash = (hash ^ 0x544154414c424544ull) * 1099511628211ull;
            if (pixels.empty())
            {
                pixels.resize(layer.size());
                width = item[0];
                height = item[1];
            }
            if (width != item[0] || height != item[1])
            {
                if (BloodElfProfile(race))
                    TraceDHTex("Compose layer dimension mismatch group=%u target=%u got=%ux%u expected=%ux%u",
                        group, target, item[0], item[1], width, height);
                valid = false;
                break;
            }
            for (unsigned i = 0; i < layer.size(); i += 4)
            {
                if (dhGlow || (dhIris && target == 25 && blend == 1) || (race == 26 && group == 2 && target == 10))
                {
                    // Eye albedo alpha is a material channel, not its coverage.
                    // Iris coverage comes from the authored mesh. Emission is RGB over black.
                    std::copy_n(layer.data() + i, 3, pixels.data() + i);
                    pixels[i + 3] = 255;
                    continue;
                }
                if (nightElfTattooPattern || nightElfMarkings || nightElfOverlay || (orc && (blend == 4 || blend == 6)) || (batch && blend == 6))
                {
                    // Retail layout 109 uses mode 4 (Multiply) for the opaque-RGB
                    // tattoo pattern atlas and mode 6 (Overlay) for the type-1 eye
                    // layer. Match the documented WoW Model Viewer compositor formulas.
                    float const sourceAlpha = layer[i + 3] / 255.0f;
                    float const destinationAlpha = pixels[i + 3] / 255.0f;
                    for (unsigned channel = 0; channel < 3; ++channel)
                    {
                        float const source = layer[i + channel] / 255.0f;
                        float const destination = pixels[i + channel] / 255.0f;
                        float blended = 0.0f;
                        if (nightElfTattooPattern || nightElfMarkings || (orc && blend == 4))
                            blended = source * destination;
                        else if (destination < 0.5f)
                            blended = 2.0f * destination * source;
                        else
                            blended = 1.0f - 2.0f * (1.0f - destination) * (1.0f - source);
                        pixels[i + channel] = static_cast<unsigned char>(std::clamp(
                            (blended * sourceAlpha + destination * (1.0f - sourceAlpha)) * 255.0f,
                            0.0f, 255.0f));
                    }
                    pixels[i + 3] = static_cast<unsigned char>(std::clamp(
                        (sourceAlpha + destinationAlpha * (1.0f - sourceAlpha)) * 255.0f,
                        0.0f, 255.0f));
                    continue;
                }
                unsigned alpha = layer[i + 3];
                unsigned priorAlpha = pixels[i + 3];
                unsigned outputAlpha = alpha * 255 + priorAlpha * (255 - alpha);
                for (unsigned channel = 0; channel < 3; ++channel)
                {
                    unsigned source = layer[i + channel];
                    pixels[i + channel] = static_cast<unsigned char>(
                        outputAlpha ? (source * alpha * 255
                            + pixels[i + channel] * priorAlpha * (255 - alpha) + outputAlpha / 2) / outputAlpha : 0);
                }
                pixels[i + 3] = static_cast<unsigned char>((outputAlpha + 127) / 255);
            }
        }
        if (batch && (group == 7 || group == 8) && pixels.empty())
        {
            if (race != 22 && race != 26 && race != 27 && race != 13) continue;
            width = height = 512;
            pixels.assign(width * height * 4, 0);
        }
        if (batch && group == 7)
        {
            DHMaskClothing(character, pixels, width, height);
            hash = (hash ^ coverageFingerprint ^ 0x4241544348474c31ull) * 1099511628211ull;
        }
        if (pixels.empty())
        {
            if (BloodElfProfile(race))
                TraceDHTex("Compose group empty group=%u matchedRows=%u", group, matchedRows);
            continue;
        }
        if ((BloodElfProfile(race) || NightElfProfile(race) || orc) && group == 4)
        {
            // Wrath's additive pass lacks Retail's emissive exposure/bloom.
            // Apply a modest exposure gain to the authored emission only;
            // black remains black and clothing coverage is applied afterwards.
            for (unsigned i = 0; i < pixels.size(); i += 4)
                for (unsigned channel = 0; channel < 3; ++channel)
                    pixels[i + channel] = static_cast<unsigned char>(
                        (std::min)(255u, unsigned(pixels[i + channel]) * (orc ? 1u : 2u)));
            DHMaskClothing(character, pixels, width, height);
            hash = (hash ^ coverageFingerprint ^ 0x4448434f56455235ull) * 1099511628211ull;
        }
        char name[160]{};
        if (batch)
            sprintf_s(name, "DHCache\\HB\\v1-%u-%u-%u-%016llx.blp", race, gender, group, static_cast<unsigned long long>(hash));
        else if (tauren)
            sprintf_s(name, "DHCache\\TR\\v1-%u-%u-%016llx.blp", gender, group, static_cast<unsigned long long>(hash));
        else if (orc)
            sprintf_s(name, "DHCache\\OR\\v2-%u-%u-%u-%016llx.blp", race, gender, group, static_cast<unsigned long long>(hash));
        else if (BloodElfProfile(race))
            sprintf_s(name, "DHCache\\BE\\%u-%u-%016llx.blp", gender, group,
                static_cast<unsigned long long>(hash));
        else if (NightElfProfile(race))
            sprintf_s(name, "DHCache\\NE\\v4-%u-%u-%016llx.blp", gender, group,
                static_cast<unsigned long long>(hash));
        else if (race == 30)
            sprintf_s(name, "DHCache\\TH\\v2-%u-%u-%016llx.blp", gender, group, static_cast<unsigned long long>(hash));
        else if (race == 50 || race == 51 || race == 54 || race == 55 || race == 56)
            sprintf_s(name, "DHCache\\NR\\v4-%u-%u-%u-%016llx.blp", race, gender, group,
                static_cast<unsigned long long>(hash));
        else if (race == 60 || race == 61)
            sprintf_s(name, "Data\\Retail\\Cache\\Appearance\\%s\\%u-%u-%016llx.blp",
                RetailCacheFamily(race), gender, group, static_cast<unsigned long long>(hash));
        else
            sprintf_s(name, "Interface\\AddOns\\EsteriaAppearanceCache\\%s\\%u-%u-%016llx.blp",
                RetailCacheFamily(race), gender, group, static_cast<unsigned long long>(hash));
        next.paths[group] = name;
        if (group != 1 || orc || tauren || batch)
        {
            // The legacy client reads the raw BGRA DH hair cache as white even
            // with its ARGB header. Its indexed body cache already renders; use
            // that same format for the fully opaque hair and beard atlas.
            bool const indexed = race == 50 || race == 51 || CreatureAppearance::Uses(race) || batch || orc || tauren || (NightElfProfile(race) && group == 8) || group == 0 || ((BloodElfProfile(race) || NightElfProfile(race))
                && (group == 2 || group == 3 || group == 4 || group == 6));
            bool const indexedAlpha = ((race == 50 || race == 51) && group >= 2)
                || (CreatureAppearance::Uses(race) && (group == 2 || group == 4))
                || (NightElfProfile(race) && group == 8)
                || (tauren && (group == 2 || group == 4 || group == 5))
                || (batch && (group == 2 || group == 4 || group == 5 || group == 6 || group >= 9));
            if ((BloodElfProfile(race) || NightElfProfile(race)) && group == 3 && width == 256 && height == 128)
            {
                // The native decoder rejected the rectangular iris cache.
                // Preserve normalized UVs on the accepted 512-square indexed format.
                std::vector<unsigned char> square(512 * 512 * 4);
                for (unsigned y = 0; y < 512; ++y)
                    for (unsigned x = 0; x < 512; ++x)
                        std::copy_n(pixels.data() + ((y / 4) * width + x / 2) * 4, 4,
                            square.data() + (y * 512 + x) * 4);
                pixels = std::move(square);
                width = height = 512;
            }
            if ((BloodElfProfile(race) || NightElfProfile(race)) && group == 2 && width == 512 && height == 256)
            {
                // The active hairstyle UV audit shows that these triangles
                // sample Retail target 10's original left and center hair strips.
                // Preserve U; duplicate rows only to retain the indexed 512-square
                // BLP accepted by the Wrath decoder while preserving normalized V.
                std::vector<unsigned char> square(width * 512 * 4);
                for (unsigned y = 0; y < 512; ++y)
                    std::copy_n(pixels.data() + (y / 2) * width * 4, width * 4,
                        square.data() + y * width * 4);
                pixels = std::move(square);
                height = 512;
            }
            if ((race == 50 || race == 51 || race == 54 || race == 55 || race == 56)
                && group >= 2 && width != height)
            {
                // Preserve normalized UV coordinates on the square indexed atlas accepted by Wrath.
                unsigned const size = (std::max)(width, height);
                std::vector<unsigned char> square(size * size * 4);
                for (unsigned y = 0; y < size; ++y)
                    for (unsigned x = 0; x < size; ++x)
                        std::copy_n(pixels.data() + ((y * height / size) * width + x * width / size) * 4,
                            4, square.data() + (y * size + x) * 4);
                pixels = std::move(square);
                width = height = size;
            }
            valid = WriteHaranirBlp(next.paths[group], pixels, width, height, indexed, indexedAlpha);
            if (BloodElfProfile(race))
                TraceDHTex("Compose BLP write group=%u result=%u size=%ux%u path=%s",
                    group, valid ? 1 : 0, width, height, next.paths[group].c_str());
            if (orc && group == 1)
            {
                static unsigned traces = 0;
                if (traces++ < 200)
                    TraceDHTex("Orc face BLP write result=%u rows=%u size=%ux%u indexed=%u path=%s",
                        valid ? 1 : 0, matchedRows, width, height, indexed ? 1 : 0, next.paths[group].c_str());
            }
        }
        else
        {
            // The catalogs project source sections into Wrath's 256x192 face destination first.
            if (height != 192 || width != 256)
            {
                valid = false;
                if (BloodElfProfile(race))
                    TraceDHTex("Compose bad face atlas dimensions group=%u size=%ux%u", group, width, height);
                break;
            }
            // The BE male DH model samples a full face atlas at type-2 record 3 in
            // addition to the client's lower/upper face layer callbacks. Keep one
            // cache-owned full atlas for that explicit model material route.
            if (BloodElfProfile(race) || NightElfProfile(race) || race == 50 || race == 51 || CreatureAppearance::Uses(race))
            {
                // The live WotLK texture decoder rejects the 256x192 Retail face atlas.
                // Stretch its V axis to a power-of-two 256x256 canvas so the native type-2
                // sampler can load it while preserving the original normalized UV mapping.
                std::vector<unsigned char> faceAtlas(width * 256 * 4);
                for (unsigned y = 0; y < 256; ++y)
                {
                    unsigned sourceY = (std::min)(height - 1, y * height / 256);
                    std::memcpy(faceAtlas.data() + y * width * 4,
                        pixels.data() + sourceY * width * 4, width * 4);
                }
                valid = WriteHaranirBlp(next.paths[group], faceAtlas, width, 256, false);
                TraceDHTex("Face atlas converted from %ux%u to %ux256 result=%u",
                    width, height, width, valid ? 1 : 0);
            }
            next.lower = next.paths[group] + "-lower.blp";
            next.upper = next.paths[group] + "-upper.blp";
            std::vector<unsigned char> upper(pixels.begin(), pixels.begin() + width * 64 * 4);
            std::vector<unsigned char> lower(pixels.begin() + width * 64 * 4, pixels.end());
            bool const upperWritten = WriteHaranirBlp(next.upper, upper, width, 64, true);
            bool const lowerWritten = WriteHaranirBlp(next.lower, lower, width, height - 64, true);
            valid = valid && upperWritten && lowerWritten;
            if (BloodElfProfile(race))
                TraceDHTex("Compose face BLP writes group=1 upper=%u lower=%u", upperWritten, lowerWritten);
        }
    }
    std::fclose(input);
    // Names remain inside our client cache. The native filesystem resolver rejected both relative and
    // absolute paths during live Glue tests; validated buffers are passed to the native decoders below.
    if (valid)
    {
        char executable[MAX_PATH]{};
        if (!GetModuleFileNameA(nullptr, executable, MAX_PATH))
            return false;
        char* slash = std::strrchr(executable, '\\');
        if (!slash)
            return false;
        slash[1] = 0;
        for (auto& path : next.paths)
            if (!path.empty())
                path = executable + path;
        if (!next.lower.empty())
            next.lower = executable + next.lower;
        if (!next.upper.empty())
            next.upper = executable + next.upper;
        if (next.lower.size() >= 128 || next.upper.size() >= 128)
        {
            if (BloodElfProfile(race))
                TraceDHTex("Compose rejected absolute path lengths lower=%u upper=%u",
                    static_cast<unsigned>(next.lower.size()), static_cast<unsigned>(next.upper.size()));
            return false;
        }
    }
    if (valid)
    {
        // Reuse unchanged native textures across single-control updates.
        for (unsigned group = 0; group < next.paths.size(); ++group)
            if (next.paths[group] == images.paths[group]) next.textures[group] = images.textures[group];
        images = std::move(next);
    }
    else
    {
        if (BloodElfProfile(race))
            TraceDHTex("Compose failed character=%p", character);
        OutputDebugStringA("Esteria: Retail material composition failed\n");
    }
    if (valid && BloodElfProfile(race))
        TraceDHTex("Compose success character=%p groups=%u,%u,%u paths0=%s path1=%s path2=%s",
            character, !images.paths[0].empty(), !images.paths[1].empty(), !images.paths[2].empty(),
            images.paths[0].c_str(), images.paths[1].c_str(), images.paths[2].c_str());
    return valid;
}

char const* HaranirLayerPath(CCharacterComponent* character, unsigned kind, unsigned slot)
{
    unsigned race = character->m_data.m_preferences.m_raceID;
    unsigned gender = character->m_data.m_preferences.m_sexID;
    if (race == 72 && gender == 1) return nullptr;
    if (!BloodElfProfile(race) && !NightElfProfile(race) && race != 20 && race != 50 && race != 51 && !CreatureAppearance::Uses(race) && !OrcModern(character) && !TaurenModern(character) && !BatchModern(character))
        return nullptr;
    auto& images = haranirImages[character];
    bool valid;
    if (race == 30 && gender == 1) { haranirImages.erase(character); return nullptr; }
    if (BatchModern(character)) valid = ComposeHaranir(character, BatchControlProfile(race, gender).options, images);
    else if (race == 6 && gender == 0) valid = ComposeHaranir(character, TaurenAppearance::MaleOptions, images);
    else if (race == 6 && gender == 1) valid = ComposeHaranir(character, TaurenAppearance::FemaleOptions, images);
    else if (race == 2 && gender == 0) valid = ComposeHaranir(character, OrcAppearanceV2::OrcMaleOptions, images);
    else if (race == 2 && gender == 1) valid = ComposeHaranir(character, OrcAppearanceV2::OrcFemaleOptions, images);
    else if (race == 14 && gender == 0) valid = ComposeHaranir(character, OrcAppearanceV2::MagharMaleOptions, images);
    else if (race == 14 && gender == 1) valid = ComposeHaranir(character, OrcAppearanceV2::MagharFemaleOptions, images);
    else if (race == 69 || race == 70) valid = ComposeHaranir(character, CreatureAppearance::DrakonidControls, images);
    else if (race == 63) valid = ComposeHaranir(character, CreatureAppearance::GnollControls, images);
    else if (CreatureAppearance::Uses(race))
        valid = ComposeHaranir(character, CreatureAppearance::Options(race, character->m_data.m_preferences.m_sexID), images);
    else if (BloodElfProfile(race))
        valid = gender == 0
            ? ComposeHaranir(character, DemonHunterAppearance::BloodElfMaleOptions, images)
            : ComposeHaranir(character, DemonHunterAppearance::BloodElfFemaleOptions, images);
    else if (NightElfProfile(race))
        valid = gender == 0
            ? ComposeHaranir(character, DemonHunterAppearance::NightElfMaleOptions, images)
            : ComposeHaranir(character, DemonHunterAppearance::NightElfFemaleOptions, images);
    else if (race == 20)
        valid = character->m_data.m_preferences.m_sexID == 0
            ? ComposeHaranir(character, VulperaAppearance::MaleOptions, images)
            : ComposeHaranir(character, VulperaAppearance::FemaleOptions, images);
    else
        valid = character->m_data.m_preferences.m_sexID == 0
            ? ComposeHaranir(character, HaranirAppearance::MaleOptions, images)
            : ComposeHaranir(character, HaranirAppearance::FemaleOptions, images);
    if (!valid)
        return "";
    if (kind == 0 && slot == 0)
        return images.paths[0].c_str();
    if (kind == 0 && slot == 1)
        return images.paths[4].c_str();
    if (kind == 1 && (OrcModern(character) || TaurenModern(character) || BatchModern(character))) return "";
    if (kind == 1 && slot < 2)
        return slot ? images.upper.c_str() : images.lower.c_str();
    if (kind == 3 && slot == 0)
        return images.paths[2].c_str();
    return "";
}


std::vector<unsigned char> HaranirBlpData(char const* path)
{
    // ESTERIA_ARCHIVED_BLP: use the same owned pixel decoder for loose/cache and MPQ BLPs.
    std::vector<unsigned char> bytes;
    FILE* file=nullptr;
    if(!fopen_s(&file,path,"rb")) {
        std::fseek(file,0,SEEK_END);long size=std::ftell(file);std::rewind(file);
        if(size>=148 && size<=8*1024*1024){bytes.resize(size);if(std::fread(bytes.data(),1,bytes.size(),file)!=bytes.size())bytes.clear();}
        std::fclose(file);
    } else {
        SFileImpl* source=nullptr;
        if(SFile::OpenEx(nullptr,path,0,&source)) {
            unsigned size=SFileGetFileSize(source,nullptr),read=0;
            if(size>=148 && size<=8*1024*1024){bytes.resize(size);if(!SFileReadFile(source,bytes.data(),size,&read,nullptr,0) || read!=size)bytes.clear();}
            SFileCloseFile(source);
        }
    }
    if (bytes.empty() || Field<unsigned>(bytes.data(), 0) != 0x32504C42
        || Field<unsigned>(bytes.data(), 4) != 1 || (bytes[8] < 1 || bytes[8] > 3)
        || !Field<unsigned>(bytes.data(), 12) || Field<unsigned>(bytes.data(), 12) > 1024
        || !Field<unsigned>(bytes.data(), 16) || Field<unsigned>(bytes.data(), 16) > 1024)
        return {};
    unsigned const dataStart = bytes[8] == 1 ? 1172 : 148;
    for (unsigned i = 0; i < 16; ++i)
    {
        unsigned offset = Field<unsigned>(bytes.data(), 20 + i * 4);
        unsigned size = Field<unsigned>(bytes.data(), 84 + i * 4);
        if ((offset || size) && (offset < dataStart || offset > bytes.size() || !size || size > bytes.size() - offset))
            return {};
        unsigned width = (std::max)(1u, Field<unsigned>(bytes.data(), 12) >> i);
        unsigned height = (std::max)(1u, Field<unsigned>(bytes.data(), 16) >> i);
        unsigned expected = 0;
        if (bytes[8] == 3)
            expected = width * height * 4;
        else if (bytes[8] == 2)
        {
            unsigned const blockBytes = bytes[10] == 0 ? 8 : 16;
            expected = ((width + 3) / 4) * ((height + 3) / 4) * blockBytes;
        }
        else
        {
            if (bytes[9] != 0 && bytes[9] != 1 && bytes[9] != 4 && bytes[9] != 8)
                return {};
            expected += (width * height * bytes[9] + 7) / 8;
            expected += width * height;
        }
        if (size && size != expected)
            return {};
    }
    return bytes;
}


    template<class Options, class Labels, class Requirements>
    bool HighmountainControl(lua_State* state, char const* command, CCharacterComponent* character, Options const& options,
        Labels const& labels, Requirements const& requirements)
    {
        auto const number = &lua_tonumber;
        auto const pushNumber = &lua_pushnumber;
        auto const pushString = &lua_pushstring;
        double requested = number(state, 2);
        if (!std::isfinite(requested) || requested < 1 || requested > options.size()
            || requested != std::floor(requested))
        {
            if (std::strcmp(command, "EA_GET") == 0)
            {
                pushNumber(state, 0);
                pushNumber(state, 1);
                pushString(state, "");
            }
            else if (std::strcmp(command, "EA_CHOICES") == 0)
                pushString(state, "");
            return true;
        }
        unsigned index = static_cast<unsigned>(requested) - 1;
        unsigned race = character->m_data.m_preferences.m_raceID;
        auto fields = HighmountainFields(character);
        unsigned gender = character->m_data.m_preferences.m_sexID;
        unsigned characterClass = character->m_data.m_preferences.m_classId;
        auto normalize = [&](std::array<std::uint8_t, 13>& values)
        {
            if (HordeBatchAppearance::Uses(race))
            {
                values = HordeBatchAppearance::Normalize(race, gender, values, characterClass);
                return;
            }
            if (race == 6)
            {
                values = TaurenAppearance::Normalize(gender, values, characterClass);
                return;
            }
            if (OrcAppearanceV2::Uses(race))
            {
                // Native race/sex changes can carry bytes from another packed profile.
                // Invalid new-session fields start from valid defaults; saved characters
                // are validated separately and never migrated through this UI path.
                if (!OrcAppearanceV2::Validate(race, gender, values)) values = {};
                values = OrcAppearanceV2::Normalize(race, gender, values, characterClass);
                return;
            }
            if (NormalElfAppearance::Uses(race))
            {
                values = NormalElfAppearance::Normalize(race, gender, values, characterClass);
                return;
            }
            if (race != 20)
                return;
            std::array<std::uint8_t, 5> vulpera{};
            std::copy_n(values.begin(), vulpera.size(), vulpera.begin());
            vulpera = VulperaAppearance::Normalize(gender, characterClass, vulpera);
            std::copy(vulpera.begin(), vulpera.end(), values.begin());
        };
        auto allowed = [&](std::array<std::uint8_t, 13> const& values)
        {
            if (HordeBatchAppearance::Uses(race))
                return HordeBatchAppearance::Validate(race, gender, values, characterClass);
            if (race == 6) return TaurenAppearance::Validate(gender, values, characterClass);
            if (OrcAppearanceV2::Uses(race)) return OrcAppearanceV2::Validate(race, gender, values, characterClass);
            if (!ValidateExtended(race, gender, values))
                return false;
            if (NormalElfAppearance::Uses(race))
                return NormalElfAppearance::Validate(race, gender, values, characterClass);
            if (race != 20)
                return true;
            std::array<std::uint8_t, 5> vulpera{};
            std::copy_n(values.begin(), vulpera.size(), vulpera.begin());
            return VulperaAppearance::ValidateClass(gender, characterClass, vulpera);
        };
        auto synchronizeTattoo = [&](std::array<std::uint8_t, 13>& candidate, unsigned index, unsigned next)
        {
            if (BloodElfProfile(race) && gender == 0 && (index == 8 || index == 9))
            {
                auto const& tattoo = options[8];
                auto const& color = options[9];
                unsigned tattooPrior = candidate[tattoo.field] / tattoo.factor % tattoo.count;
                unsigned colorPrior = candidate[color.field] / color.factor % color.count;
                unsigned tattooValue = tattooPrior;
                unsigned colorValue = colorPrior;
                if (index == 8)
                {
                    tattooValue = next;
                    colorValue = next == 0 ? 0 : (colorPrior == 0 ? 1 : colorPrior);
                }
                else
                {
                    colorValue = next;
                    tattooValue = next == 0 ? 0 : (tattooPrior == 0 ? 1 : tattooPrior);
                }
                candidate[tattoo.field] = static_cast<std::uint8_t>(
                    candidate[tattoo.field] - tattooPrior * tattoo.factor + tattooValue * tattoo.factor);
                candidate[color.field] = static_cast<std::uint8_t>(
                    candidate[color.field] - colorPrior * color.factor + colorValue * color.factor);
                return;
            }
            if (NightElfProfile(race) && gender == 1
                && (index == 11 || index == 12 || index == 18 || index == 19))
            {
                unsigned pair = index == 11 ? 12 : index == 12 ? 11 : index == 18 ? 19 : 18;
                auto const& descriptor = options[pair];
                unsigned prior = candidate[descriptor.field] / descriptor.factor % descriptor.count;
                // Preserve a selected tattoo pattern while cycling its authored color palette.
                if (index == 19 && prior != 0)
                    return;
                for (unsigned step = 0; step < descriptor.count; ++step)
                {
                    unsigned value = (prior + step) % descriptor.count;
                    bool compatible = true;
                    for (auto const& rule : requirements)
                        if ((rule.option == pair && rule.required == index && value == rule.value
                                && !(rule.mask & (std::uint64_t{1} << next)))
                            || (rule.option == index && rule.required == pair && next == rule.value
                                && !(rule.mask & (std::uint64_t{1} << value))))
                            compatible = false;
                    if (!compatible)
                        continue;
                    candidate[descriptor.field] = static_cast<std::uint8_t>(
                        candidate[descriptor.field] - prior * descriptor.factor + value * descriptor.factor);
                    return;
                }
                return;
            }
            if (NightElfProfile(race) && ((gender == 0 && (index == 15 || index == 16))
                || (gender == 1 && (index == 18 || index == 19))))
            {
                unsigned tattooIndex = gender == 0 ? 15 : 18;
                unsigned colorIndex = gender == 0 ? 16 : 19;
                unsigned pairedIndex = index == tattooIndex ? colorIndex : tattooIndex;
                auto const& paired = options[pairedIndex];
                unsigned prior = candidate[paired.field] / paired.factor % paired.count;
                if (index == colorIndex && prior != 0)
                    return;
                unsigned pairedValue = prior;
                if (index == tattooIndex)
                    pairedValue = next == 0 ? 0 : (prior == 0 ? 1 : prior);
                else if (next == 0)
                    pairedValue = 0;
                else if (prior == 0)
                    pairedValue = 1;
                candidate[paired.field] = static_cast<std::uint8_t>(
                    candidate[paired.field] - prior * paired.factor + pairedValue * paired.factor);
                return;
            }
            if (!BloodElfProfile(race) || gender != 1 || (index != 10 && index != 11))
                return;
            unsigned pairedIndex = index == 10 ? 11 : 10;
            auto const& paired = options[pairedIndex];
            unsigned prior = candidate[paired.field] / paired.factor % paired.count;
            unsigned pairedValue = next == 0 ? 0 : (prior == 0 ? 1 : prior);
            candidate[paired.field] = static_cast<std::uint8_t>(
                candidate[paired.field] - prior * paired.factor + pairedValue * paired.factor);
        };
        auto original = fields;
        normalize(fields);
        if (fields != original)
        {
            SetHighmountainFields(character, fields);
            Refresh(character);
            if (OrcAppearanceV2::Uses(race) || race == 6 || HordeBatchAppearance::Uses(race)) character = CCharacterCreation::m_character;
        }
        if (std::strcmp(command, "EA_REAPPLY") == 0)
        {
            Refresh(character);
            return true;
        }
        if (std::strcmp(command, "EA_RANDOM") == 0)
        {
            static std::mt19937 random(GetTickCount());
            fields = {};
            normalize(fields);
            for (unsigned i = 0; i < options.size(); ++i)
            {
                auto const& descriptor = options[i];
                unsigned first = std::uniform_int_distribution<unsigned>(0, descriptor.count - 1)(random);
                for (unsigned attempt = 0; attempt < descriptor.count; ++attempt)
                {
                    auto candidate = fields;
                    unsigned value = (first + attempt) % descriptor.count;
                    unsigned prior = candidate[descriptor.field] / descriptor.factor % descriptor.count;
                    candidate[descriptor.field] = static_cast<std::uint8_t>(
                        candidate[descriptor.field] - prior * descriptor.factor + value * descriptor.factor);
                    synchronizeTattoo(candidate, i, value);
                    if (allowed(candidate))
                    {
                        fields = candidate;
                        break;
                    }
                }
            }
            SetHighmountainFields(character, fields);
            Refresh(character);
            return true;
        }
        auto const& option = options[index];
        unsigned value = (fields[option.field] / option.factor) % option.count;
        if (BloodElfProfile(race) && std::strcmp(command, "EA_CYCLE") == 0)
            TraceDHTex("EA_CYCLE input index=%u label=%s field=%u factor=%u count=%u value=%u raw=%u fields=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                index, labels[index], option.field, option.factor, option.count, value, fields[option.field],
                fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7],
                fields[8], fields[9], fields[10], fields[11], fields[12]);
        if (std::strcmp(command, "EA_GET") == 0)
        {
            unsigned displayedValue = value;
            unsigned count = option.count;
            if (HordeBatchAppearance::Uses(race) || race == 6 || OrcAppearanceV2::Uses(race) || NormalElfAppearance::Uses(race)
                || (NightElfProfile(race) && index == (gender == 0 ? 16u : 19u)))
            {
                unsigned paletteCount = 0;
                for (unsigned color = 0; color < option.count; ++color)
                {
                    auto candidate = fields;
                    candidate[option.field] = static_cast<std::uint8_t>(
                        candidate[option.field] - value * option.factor + color * option.factor);
                    if (NormalElfAppearance::Uses(race))
                        synchronizeTattoo(candidate, index, color);
                    if (!allowed(candidate))
                        continue;
                    if (color == value)
                        displayedValue = paletteCount;
                    ++paletteCount;
                }
                count = paletteCount;
            }

            if (race == 20 && index == 9)
            {
                auto candidate = fields;
                candidate[option.field] = static_cast<std::uint8_t>(candidate[option.field] - value * option.factor
                    + option.factor);
                if (!allowed(candidate))
                    count = 1;
            }
            pushNumber(state, displayedValue);
            pushNumber(state, count);
            pushString(state, labels[index]);
            return true;
        }
        bool choices = std::strcmp(command, "EA_CHOICES") == 0;
        bool direct = std::strcmp(command, "EA_SET") == 0;
        if (!choices && !direct && std::strcmp(command, "EA_CYCLE") != 0)
            return true;
        double direction = choices ? 1 : number(state, 3);
        if (direct ? (!std::isfinite(direction) || direction < 0 || direction >= option.count
            || direction != std::floor(direction)) : (direction != 1 && direction != -1))
            return true;
        std::string available;
        for (unsigned step = 1; step <= (direct ? 1u : option.count); ++step)
        {
            unsigned next = choices ? step - 1 : direct ? static_cast<unsigned>(direction)
                : static_cast<unsigned>((static_cast<int>(value) + static_cast<int>(option.count) * 2
                    + static_cast<int>(step) * static_cast<int>(direction)) % static_cast<int>(option.count));
            auto candidate = fields;
            candidate[option.field] = static_cast<std::uint8_t>(
                candidate[option.field] - value * option.factor + next * option.factor);
            synchronizeTattoo(candidate, index, next);
            // Changing a prerequisite resets incompatible dependent accessories to their authored default.
            for (unsigned pass = 0; pass < options.size(); ++pass)
                for (auto const& rule : requirements)
                {
                    auto const& selected = options[rule.option];
                    auto const& required = options[rule.required];
                    unsigned dependent = (candidate[selected.field] / selected.factor) % selected.count;
                    unsigned prerequisite = (candidate[required.field] / required.factor) % required.count;
                    if (rule.option != index && dependent == rule.value
                        && !(rule.mask & (std::uint64_t{1} << prerequisite)))
                        candidate[selected.field] -= static_cast<std::uint8_t>(dependent * selected.factor);
                }
            if (!allowed(candidate))
                continue;
            if (choices)
            {
                available += std::to_string(next) + ",";
                continue;
            }
            SetHighmountainFields(character, candidate);
            if (BloodElfProfile(race) && std::strcmp(command, "EA_CYCLE") == 0)
                TraceDHTex("EA_CYCLE output index=%u label=%s value=%u->%u raw=%u->%u fields=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
                    index, labels[index], value, next, fields[option.field], candidate[option.field],
                    candidate[0], candidate[1], candidate[2], candidate[3], candidate[4], candidate[5],
                    candidate[6], candidate[7], candidate[8], candidate[9], candidate[10], candidate[11],
                    candidate[12]);
            Refresh(character);
            break;
        }
        if (choices)
            pushString(state, available.c_str());
        return true;
    }

    bool HighmountainCycle(lua_State* state, char const* command)
    {
        CCharacterComponent* character = CCharacterCreation::m_character;
        if (!character || !ExtendedRace(character->m_data.m_preferences.m_raceID)
            || (!HordeBatchAppearance::Uses(character->m_data.m_preferences.m_raceID)
                && CreatureAppearance::Uses(character->m_data.m_preferences.m_raceID)
                && character->m_data.m_preferences.m_raceID != 60 && character->m_data.m_preferences.m_raceID != 61
                && character->m_data.m_preferences.m_raceID != 63 && character->m_data.m_preferences.m_raceID != 72
                && character->m_data.m_preferences.m_raceID != 69 && character->m_data.m_preferences.m_raceID != 70))
            return false;
        unsigned race = character->m_data.m_preferences.m_raceID;
        unsigned gender = character->m_data.m_preferences.m_sexID;
        if (HordeBatchAppearance::Uses(race))
        {
            auto const& profile = BatchControlProfile(race, gender);
            return HighmountainControl(state, command, character, profile.options, profile.labels, profile.requirements);
        }
        if (race == 60) {
            static const std::array<HighmountainAppearance::Requirement, 0> none{};
            if (gender) return HighmountainControl(state, command, character, CreatureAppearance::KyrianFemaleControls, CreatureAppearance::KyrianFemaleLabels, none);
            return HighmountainControl(state, command, character, CreatureAppearance::KyrianMaleControls, CreatureAppearance::KyrianMaleLabels, none);
        }
        if (race == 61) {
            static const std::array<HighmountainAppearance::Requirement, 0> none{};
            if (gender) return HighmountainControl(state, command, character, CreatureAppearance::VenthyrFemaleControls, CreatureAppearance::VenthyrFemaleLabels, none);
            return HighmountainControl(state, command, character, CreatureAppearance::VenthyrMaleControls, CreatureAppearance::VenthyrMaleLabels, none);
        }
        if (race == 63) {
            static const std::array<HighmountainAppearance::Requirement, 0> none{};
            return HighmountainControl(state, command, character, CreatureAppearance::GnollControls, CreatureAppearance::GnollLabels, none);
        }
        if (race == 72) {
            static const std::array<HighmountainAppearance::Requirement, 0> none{};
            static const std::array<char const*,5> maleLabels{"Skin Color","Face","Scale Pattern","Eye Color","Features"};
            static const std::array<char const*,5> femaleLabels{"Skin Color","Face","Hair Style","Eye Color","Features"};
            return HighmountainControl(state, command, character, CreatureAppearance::Options(race,gender), gender?femaleLabels:maleLabels, none);
        }
        if (race == 69 || race == 70) {
            static const std::array<HighmountainAppearance::Requirement, 0> none{};
            return HighmountainControl(state, command, character, CreatureAppearance::DrakonidControls, CreatureAppearance::DrakonidLabels, none);
        }
        if (race == 6 && gender == 0) return HighmountainControl(state, command, character, TaurenAppearance::MaleOptions, TaurenAppearance::MaleLabels, TaurenAppearance::MaleRequirements);
        if (race == 6 && gender == 1) return HighmountainControl(state, command, character, TaurenAppearance::FemaleOptions, TaurenAppearance::FemaleLabels, TaurenAppearance::FemaleRequirements);
        if (race == 2 && gender == 0) return HighmountainControl(state, command, character, OrcAppearanceV2::OrcMaleOptions, OrcAppearanceV2::OrcMaleLabels, OrcAppearanceV2::OrcMaleRequirements);
        if (race == 2 && gender == 1) return HighmountainControl(state, command, character, OrcAppearanceV2::OrcFemaleOptions, OrcAppearanceV2::OrcFemaleLabels, OrcAppearanceV2::OrcFemaleRequirements);
        if (race == 14 && gender == 0) return HighmountainControl(state, command, character, OrcAppearanceV2::MagharMaleOptions, OrcAppearanceV2::MagharMaleLabels, OrcAppearanceV2::MagharMaleRequirements);
        if (race == 14 && gender == 1) return HighmountainControl(state, command, character, OrcAppearanceV2::MagharFemaleOptions, OrcAppearanceV2::MagharFemaleLabels, OrcAppearanceV2::MagharFemaleRequirements);
        if (BloodElfProfile(race) && gender == 0)
            return HighmountainControl(state, command, character, DemonHunterAppearance::BloodElfMaleOptions,
                DemonHunterAppearance::BloodElfMaleLabels, DemonHunterAppearance::BloodElfMaleRequirements);
        if (BloodElfProfile(race) && gender == 1)
            return HighmountainControl(state, command, character, DemonHunterAppearance::BloodElfFemaleOptions,
                DemonHunterAppearance::BloodElfFemaleLabels, DemonHunterAppearance::BloodElfFemaleRequirements);
        if (NightElfProfile(race) && gender == 0)
            return HighmountainControl(state, command, character, DemonHunterAppearance::NightElfMaleOptions,
                DemonHunterAppearance::NightElfMaleLabels, DemonHunterAppearance::NightElfMaleRequirements);
        if (NightElfProfile(race) && gender == 1)
            return HighmountainControl(state, command, character, DemonHunterAppearance::NightElfFemaleOptions,
                DemonHunterAppearance::NightElfFemaleLabels, DemonHunterAppearance::NightElfFemaleRequirements);
        if (character->m_data.m_preferences.m_raceID == 20)
        {
            if (character->m_data.m_preferences.m_sexID == 0)
                return HighmountainControl(state, command, character, VulperaAppearance::MaleOptions,
                    VulperaAppearance::MaleLabels, VulperaAppearance::MaleRequirements);
            return HighmountainControl(state, command, character, VulperaAppearance::FemaleOptions,
                VulperaAppearance::FemaleLabels, VulperaAppearance::FemaleRequirements);
        }
        if (character->m_data.m_preferences.m_raceID == 50 || character->m_data.m_preferences.m_raceID == 51)
        {
            if (character->m_data.m_preferences.m_sexID == 0)
                return HighmountainControl(state, command, character, HaranirAppearance::MaleOptions,
                    HaranirAppearance::MaleLabels, HaranirAppearance::MaleRequirements);
            return HighmountainControl(state, command, character, HaranirAppearance::FemaleOptions,
                HaranirAppearance::FemaleLabels, HaranirAppearance::FemaleRequirements);
        }
        if (character->m_data.m_preferences.m_raceID != 46)
        {
            if (character->m_data.m_preferences.m_sexID == 0)
                return HighmountainControl(state, command, character, EarthenAppearance::MaleOptions,
                    EarthenAppearance::MaleLabels, EarthenAppearance::MaleRequirements);
            return HighmountainControl(state, command, character, EarthenAppearance::FemaleOptions,
                EarthenAppearance::FemaleLabels, EarthenAppearance::FemaleRequirements);
        }
        if (character->m_data.m_preferences.m_sexID == 0)
            return HighmountainControl(state, command, character, HighmountainAppearance::MaleOptions,
                HighmountainAppearance::MaleLabels, HighmountainAppearance::MaleRequirements);
        return HighmountainControl(state, command, character, HighmountainAppearance::FemaleOptions,
            HighmountainAppearance::FemaleLabels, HighmountainAppearance::FemaleRequirements);
    }


auto& pendingModels=*new std::unordered_map<CCharacterComponent*,CM2Model*>;
auto& accessoryTextures=*new std::unordered_map<std::string,std::shared_ptr<void>>;
auto& accessoryTextureOrder=*new std::vector<std::string>;
void PixelUpload(EGxTexCommand command,uint32_t,uint32_t,uint32_t,uint32_t mip,
    void* user,uint32_t& pitch,const void*& pixels) {
    auto* source=static_cast<AccessoryPixelSource*>(user);
    if(command==GxTex_Latch) { void* data=nullptr; source->Mip(mip,pitch,data); pixels=data; }
    else if(command==GxTex_Unlock) source->Finish();
}

std::shared_ptr<void> LoadTexture(char const* path) {
    auto source=std::make_shared<AccessoryPixelSource>();
    source->bytes=HaranirBlpData(path); source->path=path;
    if(!source->Valid()) return {};
    auto* handle=TextureCreate(source->Word(12),source->Word(16),GxTex_Argb8888,
        GxTex_Argb8888,CGxTexFlags(GxTex_Linear),source.get(),&PixelUpload,path,0);
    if(!handle) return {};
    auto* texture=static_cast<CTexture*>(HandleDereference(reinterpret_cast<HOBJECT>(handle)));
    if(!texture) { HandleClose(reinterpret_cast<HOBJECT>(handle)); return {}; }
    texture->m_esteriaPixels=source;
    return std::shared_ptr<void>(handle,[](void* ptr){HandleClose(reinterpret_cast<HOBJECT>(ptr));});
}
std::shared_ptr<void> LoadAssetTexture(char const* path) {
    if(!path || !*path)return {};
    auto found=accessoryTextures.find(path);
    if(found!=accessoryTextures.end())return found->second;
    auto texture=LoadTexture(path);
    if(!texture) {
        // Selector paths may live only inside MPQs. LoadTexture handles the
        // generated loose cache; the native resolver handles archived assets.
        CStatus status;
        auto* handle=TextureCreate(path,CGxTexFlags(),&status,0);
        if(handle)texture=std::shared_ptr<void>(handle,[](void* ptr){HandleClose(reinterpret_cast<HOBJECT>(ptr));});
    }
    if(texture) {
        // Models duplicate texture handles in ReplaceTexture, including queued
        // replacements. Evict only the cache's ownership of old palette choices.
        // Otherwise every appearance visited stays resident until client exit.
        constexpr std::size_t limit=32;
        while(accessoryTextureOrder.size()>=limit) {
            accessoryTextures.erase(accessoryTextureOrder.front());
            accessoryTextureOrder.erase(accessoryTextureOrder.begin());
        }
        accessoryTextures.emplace(path,texture);
        accessoryTextureOrder.emplace_back(path);
    }
    return texture;
}

void* LoadLayerTexture(char const* path) {
    auto bytes=HaranirBlpData(path);
    if(bytes.empty() || bytes.size()>0xfffff) return nullptr;
    auto* entry=static_cast<CACHEENTRY*>(TextureCacheCreateTexture(path));
    if(!entry || entry->m_data || entry->m_asyncObject) return entry;
    auto* memory=static_cast<unsigned char*>(SMemAlloc(bytes.size(),__FILE__,__LINE__,0));
    if(!memory) { TextureCacheDestroyTexture(entry); return nullptr; }
    std::memcpy(memory,bytes.data(),bytes.size());
    entry->m_data=reinterpret_cast<BLPHeader*>(memory); entry->m_size=unsigned(bytes.size());
    entry->m_info.m_width=std::uint16_t(Field<unsigned>(memory,12));
    entry->m_info.m_height=std::uint16_t(Field<unsigned>(memory,16));
    unsigned count=0; while(count<16 && Field<unsigned>(memory,20+count*4)) ++count;
    entry->m_info.m_mipCount=count; entry->m_info.m_alphaSize=memory[9];
    entry->m_info.m_opaque=memory[9]==0; entry->m_loadFailed=0;
    return entry;
}

#include "EsteriaSelectorOutfits.inl"

    unsigned SaberonTextureCoverage(CCharacterComponent* character)
    {
        static auto const masks = []
        {
            std::unordered_map<unsigned, unsigned> result;
            FILE* file = CatalogFile(L"EsteriaSaberonCoverage.bin");
            if (!file)
                return result;
            unsigned header[3]{};
            if (std::fread(header, sizeof(header), 1, file) == 1 && header[0] == 0x56434253
                && header[1] == 1 && header[2] <= 65536)
                for (unsigned i = 0; i < header[2]; ++i)
                {
                    unsigned row[2]{};
                    if (std::fread(row, sizeof(row), 1, file) != 1 || !row[0] || !row[1] || row[1] >= 128)
                    {
                        result.clear();
                        break;
                    }
                    result.emplace(row[0], row[1]);
                }
            std::fclose(file);
            return result;
        }();
        unsigned result = 0;
        for (unsigned slot = 2; slot <= 9; ++slot)
        {
            if (slot == 4)
                continue; // Belts have a separate raised waist surface.
            auto found = masks.find(character->m_itemDisplayID[slot]);
            if (found != masks.end())
                result |= found->second;
        }
        return result;
    }

    unsigned DrakonidTextureCoverage(CCharacterComponent* character)
    {
        static auto const masks = []
        {
            std::unordered_map<unsigned, unsigned> result;
            FILE* file = CatalogFile(L"EsteriaDrakonidCoverage.bin");
            if (!file)
                return result;
            unsigned header[3]{};
            if (std::fread(header, sizeof(header), 1, file) == 1 && header[0] == 0x56434253
                && header[1] == 1 && header[2] <= 65536)
                for (unsigned i = 0; i < header[2]; ++i)
                {
                    unsigned row[2]{};
                    if (std::fread(row, sizeof(row), 1, file) != 1 || !row[0] || !row[1] || row[1] >= 256)
                    {
                        result.clear();
                        break;
                    }
                    result.emplace(row[0], row[1]);
                }
            std::fclose(file);
            return result;
        }();
        unsigned result = 0;
        for (unsigned slot = 2; slot <= 9; ++slot)
        {
            auto found = masks.find(character->m_itemDisplayID[slot]);
            if (found != masks.end())
                result |= found->second;
        }
        return result;
    }


template<class Options> void SelectGeometry(CCharacterComponent* c,Options const& options,
    std::vector<HighmountainSelection> const& records) {
    auto* model=c->m_data.m_model; if(!model || !model->m_scene || !model->m_shared || !model->m_shared->m_skinData) return;
    auto const& p=c->m_data.m_preferences;
    auto fields=HighmountainFields(c);
    std::uint64_t groups=0,hidden=0,materials=0;
    std::array<unsigned,52> selected{};
    model->SetGeometryVisible(10000,29999,0);
    if(BloodElfProfile(p.m_raceID)) model->SetGeometryVisible(100,p.m_sexID?399:299,0);
    for(auto const& record:records) {
        if(record.gender!=p.m_sexID) continue;
        if(record.kind==4 && record.value<52 && record.target==c->m_itemDisplayID[0])
            hidden|=std::uint64_t(1)<<record.value;
    }
    for(auto const& record:records) {
        if(record.gender!=p.m_sexID || record.kind>=3) continue;
        bool matches=true;
        for(auto key:record.choices) {
            if(key==0xffffffff) continue;
            unsigned index=key&65535;
            if(index>=options.size()) {matches=false;break;}
            auto const& option=options[index];
            if(option.field>=fields.size() || !option.factor || !option.count ||
                fields[option.field]/option.factor%option.count != key>>16) {matches=false;break;}
        }
        if(!matches) continue;
        if(record.kind==2) {
            bool equipped=false;
            if(p.m_raceID==62||p.m_raceID==64)
                for(unsigned slot=2;slot<=9;++slot)equipped=equipped||c->m_itemDisplayID[slot]!=0;
            if((p.m_raceID==62||equipped)&&record.value>=21000&&record.value<22000)continue;
            if(p.m_raceID==64&&record.value>=20001&&record.value<=20009) {
                unsigned coverage=SaberonTextureCoverage(c);
                if(record.value>=20008)continue;
                if(!(coverage&(1u<<(record.value-20001))))continue;
            }
            if((p.m_raceID==69||p.m_raceID==70) && record.value>=20001 && record.value<=20028
                && (record.value-20001)%10<8 && !(DrakonidTextureCoverage(c)&(1u<<((record.value-20001)%10))))continue;
            unsigned group=record.target/100;
            unsigned source=group<19?c->m_data.m_geosets[group]:group<52?selected[group]:0;
            if(!record.target || source==record.target) model->SetGeometryVisible(record.value,record.value,1);
        } else if(!record.kind && record.target<52) {
            if(BloodElfProfile(p.m_raceID) && record.target==1) continue;
            auto bit=std::uint64_t(1)<<record.target;
            if(groups&bit) continue; groups|=bit;
            unsigned value=(hidden&bit)?record.target*100:record.value;
            unsigned race=p.m_raceID;
            if(record.target==20 && (BatchModern(c)||race==48||race==49||race==50||race==51||race==56||race==57))
                value=c->m_itemDisplayID[6]?2002:(race==50||race==51?record.value:2001);
            selected[record.target]=value;
            if(record.target<19) {
                c->m_data.m_geosets[record.target]=value;
                if((BloodElfProfile(race)||NightElfProfile(race)||OrcModern(c)||TaurenModern(c)||BatchModern(c)) && record.target==0) {
                    // The base body shares group zero with hair. Preserve section zero.
                    model->SetGeometryVisible(1,99,0);
                    if(value)model->SetGeometryVisible(value,value,1);
                }
                if((NightElfProfile(race)||OrcModern(c)||BatchModern(c)||(BloodElfProfile(race)&&record.target==7)) && record.target!=0) {
                    model->SetGeometryVisible(OrcModern(c)&&record.target==7?702:record.target*100,record.target*100+99,0);
                    model->SetGeometryVisible(value,value,1);
                }
            } else {
                model->SetGeometryVisible(record.target*100,record.target*100+99,0);
                model->SetGeometryVisible(value,value,1);
            }
        } else if(record.kind==1 && record.target<64 && !(materials&(std::uint64_t(1)<<record.target))) {
            auto texture=LoadAssetTexture(record.path);
            if(texture)model->ReplaceTexture(record.target,reinterpret_cast<CTexture*>(texture.get()));
            materials|=std::uint64_t(1)<<record.target;
        }
    }
    // The added player robe has its own atlas. It stays hidden until equipment
    // binding has loaded that atlas, including in creation and naked previews.
    if(p.m_raceID==60||p.m_raceID==61) {
        model->SetGeometryVisible(1302,1302,0);
        model->SetGeometryVisible(20002,20034,0);
        model->SetGeometryVisible(20100,20139,0);
        model->SetGeometryVisible(20200,20200,0);
        auto empty=LoadAssetTexture("custom\\equipment\\native\\empty-garment.blp");
        if(empty)model->ReplaceTexture(15,reinterpret_cast<CTexture*>(empty.get()));
    }
    // Equipped class previews use the same fitted layers as world/roster gear.
    if (p.m_raceID == 60 || p.m_raceID == 61) {
        auto hideGroup = [&](unsigned group) {
            model->SetGeometryVisible(group * 100, group * 100 + 99, 0);
        };
        bool armorEquipped = false;
        for (unsigned slot = ITEMSLOT_HEAD; slot <= ITEMSLOT_CAPE; ++slot)
            armorEquipped = armorEquipped || c->m_itemDisplayID[slot] != 0;
        if (armorEquipped) {
            if (p.m_raceID == 60) {
                hideGroup(32); // Built-in headwear.
                hideGroup(34); // Built-in shoulder armor.
                hideGroup(33); // Built-in outfit; retain hair and wings.
            } else {
                hideGroup(35);
                if (p.m_sexID) hideGroup(32);
                hideGroup(34);
                // Venthyr outfit meshes include the entire body. Use its basic
                // body instead of hiding the body along with the built-in coat.
                hideGroup(31);
                model->SetGeometryVisible(3101, 3101, 1);
            }
        }
    }
    if(OrcModern(c)) {
        model->SetGeometryVisible(2000,2099,0);
        unsigned feet=c->m_itemDisplayID[6]?2002:2001;
        model->SetGeometryVisible(feet,feet,1);
    }
    if(BloodElfProfile(p.m_raceID)||NightElfProfile(p.m_raceID)) {
        auto* skin=model->m_shared?model->m_shared->m_skinData:nullptr;
        for(unsigned i=0;skin && i<skin->m_skinSections.m_count;++i) {
            unsigned id=skin->m_skinSections[i].m_skinSectionId&65535;
            if(id==3301||id/100==7)model->SetGeometryVisible(id,id,id==3301||id==selected[7]);
        }
    }
    for(unsigned group=1;group<52;++group) if(hidden&(std::uint64_t(1)<<group)) {
        if(group<19)c->m_data.m_geosets[group]=group*100;
        model->SetGeometryVisible(group*100,group*100+99,0);
    }
    if(OrcModern(c) || (BatchModern(c) && (p.m_raceID==1 || p.m_raceID==5 || p.m_raceID==18))) {
        auto* skin=model->m_shared?model->m_shared->m_skinData:nullptr;
        for(unsigned group:{22u,32u}) {
            if(selected[group] || (group==32 && p.m_raceID==1)) continue;
            unsigned preferred=group==22?2201:3202;
            for(unsigned i=0;skin && i<skin->m_skinSections.m_count;++i)
                if((skin->m_skinSections[i].m_skinSectionId&65535)==preferred) {
                    model->SetGeometryVisible(group*100,group*100+99,0);
                    model->SetGeometryVisible(preferred,preferred,1); break;
                }
        }
    }
    // Geometry refreshes also run independently of material binding. Restore
    // equipped native skirts and cuff meshes after the customization pass.
    if(p.m_raceID==60||p.m_raceID==61)BindSelectorOutfit(c);
}

template<class F> bool OptionsFor(CCharacterComponent* c,F&& callback) {
    auto const& p=c->m_data.m_preferences;
    unsigned r=p.m_raceID,g=p.m_sexID;
    if(BatchModern(c)) {callback(BatchControlProfile(r,g).options);return true;}
    if(OrcModern(c)) {
        if(r==2) {if(g) callback(OrcAppearanceV2::OrcFemaleOptions);else callback(OrcAppearanceV2::OrcMaleOptions);}
        else {if(g) callback(OrcAppearanceV2::MagharFemaleOptions);else callback(OrcAppearanceV2::MagharMaleOptions);}return true;
    }
    if(r==60){callback(g?CreatureAppearance::KyrianFemaleControls:CreatureAppearance::KyrianMaleControls);return true;}
    if(r==61){callback(g?CreatureAppearance::VenthyrFemaleControls:CreatureAppearance::VenthyrMaleControls);return true;}
    if(r==63){callback(CreatureAppearance::GnollControls);return true;}
    if(r==69||r==70){callback(CreatureAppearance::DrakonidControls);return true;}
    if(BloodElfProfile(r)) {if(g)callback(DemonHunterAppearance::BloodElfFemaleOptions);else callback(DemonHunterAppearance::BloodElfMaleOptions);return true;}
    if(NightElfProfile(r)) {if(g)callback(DemonHunterAppearance::NightElfFemaleOptions);else callback(DemonHunterAppearance::NightElfMaleOptions);return true;}
    if(r==6 && TaurenModern(c)){if(g)callback(TaurenAppearance::FemaleOptions);else callback(TaurenAppearance::MaleOptions);return true;}
    if(CreatureAppearance::Uses(r)){callback(CreatureAppearance::Options(r,g));return true;}
    if(r==46){if(g)callback(HighmountainAppearance::FemaleOptions);else callback(HighmountainAppearance::MaleOptions);return true;}
    if(r==48||r==49){if(g)callback(EarthenAppearance::FemaleOptions);else callback(EarthenAppearance::MaleOptions);return true;}
    if(r==50||r==51){if(g)callback(HaranirAppearance::FemaleOptions);else callback(HaranirAppearance::MaleOptions);return true;}
    return false;
}

std::unordered_map<std::uint64_t,std::uint64_t> roster;
std::unordered_map<std::uint64_t,std::string> retailRoster;
struct PreviewGear {std::array<unsigned,12> displays{};bool hidden=false;};
auto& previewGear=*new std::unordered_map<CCharacterComponent*,PreviewGear>;
auto& genericTextures=*new std::unordered_map<CCharacterComponent*,std::array<std::shared_ptr<void>,32>>;
unsigned& LegacyField(CCharacterComponent* c,unsigned offset) {
    auto& p=c->m_data.m_preferences;
    switch(offset) {
        case 0x24:return p.m_hairColorId;case 0x28:return p.m_skinID;
        case 0x2c:return p.m_faceId;case 0x30:return p.m_facialHairId;
        default:return p.m_hairStyleId;
    }
}
bool GenericGeometry(CCharacterComponent* c) {
    auto& p=c->m_data.m_preferences;auto* model=c->m_data.m_model;
    if(!model || !model->m_scene || !model->m_shared || !model->m_shared->m_skinData)return false;
    for(auto const& profile:Profiles()) {
        if(profile.race!=p.m_raceID||profile.gender!=p.m_sexID)continue;
        for(unsigned i=0;i<profile.optionCount;++i) {
            auto const& option=profile.options[i];
            if(!option.geometryOffset)continue;
            unsigned choice=LegacyField(c,option.offset)/option.factor%option.count;
            unsigned group=(option.geometryOffset-0x144)/4;
            unsigned value=option.geometryChoices[choice];
            if(group<19)c->m_data.m_geosets[group]=value;
            model->SetGeometryVisible(group==0?1:group*100,group*100+99,0);
            if(value || group!=0)model->SetGeometryVisible(value,value,1);
        }
        if(p.m_raceID==47 && profile.optionCount==12) {
            auto choice=[&](unsigned i) {auto const& o=profile.options[i];return LegacyField(c,o.offset)/o.factor%o.count;};
            auto set=[&](unsigned group,unsigned value) {
                c->m_data.m_geosets[group]=value;
                model->SetGeometryVisible(group*100,group*100+99,0);
                model->SetGeometryVisible(value,value,1);
            };
            unsigned modification=choice(7),visor=modification/4;
            set(1,choice(4)?101+choice(4):100);set(3,301+choice(5));
            set(5,501+choice(6));set(10,1001+choice(6));set(7,702+modification%4);
            set(16,visor?1601+(visor==4?2:visor):1600);set(18,visor==2?1803:1800);
            unsigned eye=choice(8),primal=profile.options[8].count-4;
            set(17,eye>=primal?1702+eye-primal:eye==14?1701:1700);
            unsigned bound=0;
            for(auto const& material:AppearanceMaterials()) {
                if(material.gender!=p.m_sexID || (bound&(1u<<material.slot)))continue;
                bool selected=true;
                for(auto const& selector:material.selectors)selected=selected && LegacyField(c,selector.offset)/selector.factor%selector.count==selector.value;
                if(!selected)continue;
                auto texture=LoadAssetTexture(material.path);
                if(texture)model->ReplaceTexture(material.slot,reinterpret_cast<CTexture*>(texture.get()));
                bound|=1u<<material.slot;
            }
        }
        // ESTERIA_SELECTOR_SKYBORNE_VARIANTS: match the original toggle and palette graph.
        if((p.m_raceID==52||p.m_raceID==53) && profile.optionCount>7) {
            auto choice=[&](unsigned i){auto const& option=profile.options[i];return LegacyField(c,option.offset)/option.factor%option.count;};
            unsigned feather=profile.options[2].geometryChoices[choice(2)];
            unsigned selected=feather&&choice(6)?feather+choice(7)*100:1600;
            c->m_data.m_geosets[16]=selected;
            model->SetGeometryVisible(4000,4799,0);
            if(selected>=4000&&selected<=4799)model->SetGeometryVisible(selected,selected,1);
        }
        if(p.m_raceID==52||p.m_raceID==53) {
            static auto const rows=ReadSelections(L"EsteriaSkyborneEyes.bin");
            auto const& option=profile.options[4];
            unsigned eye=LegacyField(c,option.offset)/option.factor%option.count;
            for(auto const& row:rows)if(row.gender==p.m_sexID && row.value==eye) {
                CStatus status;auto* texture=TextureCreate(row.path,CGxTexFlags(),&status,0);
                if(texture) {
                    genericTextures[c][8]=std::shared_ptr<void>(texture,[](void* value){HandleClose(reinterpret_cast<HOBJECT>(value));});
                    model->ReplaceTexture(8,texture);
                }break;
            }
        }
        return true;
    }
    return false;
}
int GenericCommand(lua_State* state,CCharacterComponent* c,char const* command) {
    auto const& p=c->m_data.m_preferences;
    int requested=int(lua_tonumber(state,2))-1;
    for(auto const& profile:Profiles()) {
        if(profile.race!=p.m_raceID||profile.gender!=p.m_sexID)continue;
        if(!std::strcmp(command,"EA_RANDOM")) {
            static std::mt19937 random(GetTickCount());
            for(auto offset:{0x24u,0x28u,0x2cu,0x30u,0x34u})LegacyField(c,offset)=0;
            for(unsigned i=0;i<profile.optionCount;++i) {auto const& o=profile.options[i];LegacyField(c,o.offset)+=o.factor*std::uniform_int_distribution<unsigned>(0,o.count-1)(random);}
            Refresh(c);return 0;
        }
        if(!std::strcmp(command,"EA_REAPPLY")){Refresh(c);return 0;}
        if(requested<0||unsigned(requested)>=profile.optionCount)return 0;
        auto const& option=profile.options[requested];auto& raw=LegacyField(c,option.offset);
        unsigned value=raw/option.factor%option.count;
        if(!std::strcmp(command,"EA_GET")){lua_pushnumber(state,value);lua_pushnumber(state,option.count);return 2;}
        if(!std::strcmp(command,"EA_DRACTHYR_RAW_GET")){lua_pushnumber(state,raw);lua_pushnumber(state,option.count);return 2;}
        if(!std::strcmp(command,"EA_CHOICES")) {std::string list;for(unsigned i=0;i<option.count;++i)list+=std::to_string(i)+",";lua_pushstring(state,list.c_str());return 1;}
        double input=lua_tonumber(state,3);if(!std::isfinite(input)||input!=std::floor(input))return 0;
        if(!std::strcmp(command,"EA_DRACTHYR_RAW_SET")){if(input>=0&&input<=255){raw=unsigned(input);Refresh(c);}return 0;}
        if(std::strcmp(command,"EA_SET")&&std::strcmp(command,"EA_CYCLE"))return 0;
        int next=!std::strcmp(command,"EA_SET")?int(input):(int(value)+int(input)+int(option.count))%int(option.count);
        if(next<0||unsigned(next)>=option.count)return 0;
        unsigned replacement=raw-value*option.factor+next*option.factor;
        if(replacement<=255){raw=replacement;Refresh(c);}return 0;
    }
    return 0;
}
void Refresh(CCharacterComponent* c) {
    if(!c)return;
    // ESTERIA_REFRESH_MODEL_PROFILE: posture and profile changes need a matching model.
    if(c==CCharacterCreation::m_character && c->m_data.m_model && c->m_data.m_model->m_shared) {
        auto const* current=c->m_data.m_model->m_shared->m_fileName;
        auto const* wanted=EsteriaAppearance::ModelPath(c->m_data.m_preferences,current);
        if(_stricmp(current,wanted)) {
            ComponentData descriptor=c->m_data;
            CCharacterCreation::InitCharacterComponents(&descriptor,false);
            return;
        }
    }
    auto desired=c->m_data.m_preferences;
    c->SetSkinColor(desired.m_skinID,true,true,nullptr);
    c->m_data.m_preferences=desired;c->SetFace(desired.m_faceId,true,nullptr);
    c->m_data.m_preferences=desired;c->SetHairColor(desired.m_hairColorId,true,nullptr);
    c->m_data.m_preferences=desired;c->SetHairStyle(desired.m_hairStyleId,nullptr);
    c->m_data.m_preferences=desired;c->SetBeardStyle(desired.m_facialHairId,true,nullptr);
    c->m_data.m_preferences=desired;
    c->ReplaceHairTexture(desired.m_hairStyleId,nullptr);
    c->ReplaceExtraSkinTexture(nullptr);
    c->m_flags|=4; c->SetDirtySections(0x3ff);
    EsteriaAppearance::Geometry(c);EsteriaAppearance::Bind(c);
}
} // namespace

    char const* ModernModelPath(unsigned race, unsigned gender, OrcAppearanceV2::Fields const& fields)
    {
        if (race == 5) return gender ? "Character\\HordeBatch\\undead\\Female\\UndeadRetailFemale.m2"
            : "Character\\HordeBatch\\undead\\Male\\UndeadRetailMale.m2";
        if (race == 8) return gender ? "Character\\HordeBatch\\troll\\Female\\TrollRetailFemale.m2"
            : "Character\\HordeBatch\\troll\\Male\\TrollRetailMale.m2";
        if (race == 9) return gender ? "Character\\HordeBatch\\goblin\\Female\\GoblinRetailFemale.m2"
            : "Character\\HordeBatch\\goblin\\Male\\GoblinRetailMale.m2";
        if (race == 20 || race == 19) return gender ? "Character\\HordeBatch\\pandaren\\Female\\PandarenRetailFemale.m2"
            : "Character\\HordeBatch\\pandaren\\Male\\PandarenRetailMale.m2";
        if (race == 18) return gender ? "Character\\HordeBatch\\vulpera\\Female\\VulperaRetailFemale.m2"
            : "Character\\HordeBatch\\vulpera\\Male\\VulperaRetailMale.m2";
        if (race == 22) return gender ? "Character\\HordeBatch\\zandalari\\Female\\ZandalariRetailFemale.m2"
            : "Character\\HordeBatch\\zandalari\\Male\\ZandalariRetailMale.m2";
        if (race == 26) return gender ? "Character\\HordeBatch\\nightborne\\Female\\NightborneRetailFemale.m2"
            : "Character\\HordeBatch\\nightborne\\Male\\NightborneRetailMale.m2";
        if (race == 28) return gender ? "Character\\HordeBatch\\manari\\Female\\ManariRetailFemale.m2"
            : "Character\\HordeBatch\\manari\\Male\\ManariRetailMale.m2";
        if (race == 31 || race == 29) return gender ? "Character\\HordeBatch\\earthen\\Female\\EarthenRetailFemale.m2"
            : "Character\\HordeBatch\\earthen\\Male\\EarthenRetailMale.m2";
        if (race == 1) return gender ? "Character\\AllianceBatch\\human\\Female\\HumanRetailFemale.m2"
            : "Character\\AllianceBatch\\human\\Male\\HumanRetailMale.m2";
        if (race == 3) return gender ? "Character\\AllianceBatch\\dwarf\\Female\\DwarfRetailFemale.m2"
            : "Character\\AllianceBatch\\dwarf\\Male\\DwarfRetailMale.m2";
        if (race == 7) return gender ? "Character\\AllianceBatch\\gnome\\Female\\GnomeRetailFemale.m2"
            : "Character\\AllianceBatch\\gnome\\Male\\GnomeRetailMale.m2";
        if (race == 11) return gender ? "Character\\AllianceBatch\\draenei\\Female\\DraeneiRetailFemale.m2"
            : "Character\\AllianceBatch\\draenei\\Male\\DraeneiRetailMale.m2";
        if (race == 12) return gender ? "Character\\AllianceBatch\\worgen\\Female\\WorgenRetailFemale.m2"
            : "Character\\AllianceBatch\\worgen\\Male\\WorgenRetailMale.m2";
        if (race == 13) return gender ? "Character\\AllianceBatch\\voidelf\\Female\\VoidelfRetailFemale.m2"
            : "Character\\AllianceBatch\\voidelf\\Male\\VoidelfRetailMale.m2";
        if (race == 16) return gender ? "Character\\AllianceBatch\\darkiron\\Female\\DarkironRetailFemale.m2"
            : "Character\\AllianceBatch\\darkiron\\Male\\DarkironRetailMale.m2";
        if (race == 25) return gender ? "Character\\AllianceBatch\\kultiran\\Female\\KultiranRetailFemale.m2"
            : "Character\\AllianceBatch\\kultiran\\Male\\KultiranRetailMale.m2";
        if (race == 27) return gender ? "Character\\AllianceBatch\\lightforged\\Female\\LightforgedRetailFemale.m2"
            : "Character\\AllianceBatch\\lightforged\\Male\\LightforgedRetailMale.m2";
        if (race == 6) return gender ? "Character\\ModernTauren\\Female\\TaurenRetailFemale.m2"
            : "Character\\ModernTauren\\Male\\TaurenRetailMale.m2";
        if (gender) return "Character\\ModernOrcs\\Female\\Default\\OrcRetailFemale.m2";
        return OrcAppearanceV2::Posture(race, gender, fields)
            ? "Character\\ModernOrcs\\Male\\Upright\\OrcRetailMaleUpright.m2"
            : "Character\\ModernOrcs\\Male\\Slouched\\OrcRetailMaleSlouched.m2";
    }

namespace EsteriaAppearance {
char const* ModelPath(CharacterPreferences const& p,char const* fallback) {
    if(p.m_sexID>1)return fallback;
    Fields fields{uint8_t(p.m_skinID),uint8_t(p.m_faceId),uint8_t(p.m_hairStyleId),uint8_t(p.m_hairColorId),uint8_t(p.m_facialHairId)};
    for(unsigned i=0;i<8;++i)fields[i+5]=uint8_t(p.m_esteriaExtra>>(8*i));
    bool modern=HordeBatchAppearance::IsModern(p.m_raceID,p.m_esteriaExtra)
        || (OrcAppearanceV2::Uses(p.m_raceID) && OrcAppearanceV2::IsModern(fields))
        || (p.m_raceID==6 && (p.m_esteriaExtra>>56)==0xC6);
    return modern?ModernModelPath(p.m_raceID,p.m_sexID,fields):fallback;
}
bool Handles(CharacterPreferences const& p) {
    if(p.m_raceID==72 && p.m_sexID==1)return false; // Kobold female uses the stock material pipeline.
    if(p.m_sexID>1 || !ExtendedRace(p.m_raceID)) return false;
    Fields fields{std::uint8_t(p.m_skinID),std::uint8_t(p.m_faceId),std::uint8_t(p.m_hairStyleId),
        std::uint8_t(p.m_hairColorId),std::uint8_t(p.m_facialHairId)};
    for(unsigned i=0;i<8;++i) fields[i+5]=std::uint8_t(p.m_esteriaExtra>>(i*8));
    if(NormalElfAppearance::Uses(p.m_raceID)) return true;
    return ValidateExtended(p.m_raceID,p.m_sexID,fields);
}
void Normalize(CharacterPreferences& p) {
    // Preserve full-width profile markers (for example Man'ari's 0xD8 byte).
    if(p.m_raceID==30||p.m_raceID==58||p.m_raceID==59)p.m_sexID=0;
    if(p.m_raceID==62||p.m_raceID==64)p.m_hairStyleId=0;
    if(p.m_raceID==54) {
        p.m_skinID%=6;p.m_faceId=0;p.m_hairStyleId=0;p.m_hairColorId=0;p.m_facialHairId=0;
    }
    Fields raw{std::uint8_t(p.m_skinID),std::uint8_t(p.m_faceId),std::uint8_t(p.m_hairStyleId),std::uint8_t(p.m_hairColorId),std::uint8_t(p.m_facialHairId)};
    for(unsigned i=0;i<8;++i)raw[i+5]=std::uint8_t(p.m_esteriaExtra>>(i*8));
    bool changed=false;
    if(HordeBatchAppearance::Uses(p.m_raceID)) {
        raw=HordeBatchAppearance::Normalize(p.m_raceID,p.m_sexID,raw,p.m_classId); changed=true;
    } else if(OrcAppearanceV2::Uses(p.m_raceID)) {
        raw=OrcAppearanceV2::Normalize(p.m_raceID,p.m_sexID,raw,p.m_classId); changed=true;
    }
    if(changed) {
        p.m_skinID=raw[0];p.m_faceId=raw[1];p.m_hairStyleId=raw[2];p.m_hairColorId=raw[3];p.m_facialHairId=raw[4];
        p.m_esteriaExtra=0;for(unsigned i=0;i<8;++i)p.m_esteriaExtra|=std::uint64_t(raw[i+5])<<(8*i);
    }
    if(NormalElfAppearance::Uses(p.m_raceID)) {
        Fields fields{std::uint8_t(p.m_skinID),std::uint8_t(p.m_faceId),std::uint8_t(p.m_hairStyleId),std::uint8_t(p.m_hairColorId),std::uint8_t(p.m_facialHairId)};
        for(unsigned i=0;i<8;++i)fields[i+5]=std::uint8_t(p.m_esteriaExtra>>(i*8));
        fields=(p.m_esteriaExtra&NormalElfAppearance::ModernMarker)?NormalElfAppearance::Decode(p.m_raceID,p.m_sexID,fields):NormalElfAppearance::Legacy(p.m_raceID,p.m_sexID,fields);
        fields=NormalElfAppearance::Normalize(p.m_raceID,p.m_sexID,fields,p.m_classId);
        p.m_skinID=fields[0];p.m_faceId=fields[1];p.m_hairStyleId=fields[2];p.m_hairColorId=fields[3];p.m_facialHairId=fields[4];
        p.m_esteriaExtra=NormalElfAppearance::Extra(p.m_raceID,p.m_sexID,fields);
    }
}
bool LoadLayer(CCharacterComponent* c,unsigned kind,unsigned slot,unsigned section) {
    if(!c || (c->m_data.m_flags&1) || !Handles(c->m_data.m_preferences) || kind>=5 || slot>=3) return false;
    if(c->m_data.m_preferences.m_raceID==62||c->m_data.m_preferences.m_raceID==64)c->m_data.m_preferences.m_hairStyleId=0;
    auto path=HaranirLayerPath(c,kind,slot);
    if(!path) return false;
    auto& layer=c->m_baseVariation[kind].m_texture[slot];
    if(layer)TextureCacheDestroyTexture(layer);
    layer=*path?LoadLayerTexture(path):nullptr;
    if(section<32)c->SetDirtySections(1u<<section);
    return true;
}
bool Bind(CCharacterComponent* c) {
    if (RetailRaceActive(c)) return true;
    if(!c || (c->m_data.m_flags&1) || !Handles(c->m_data.m_preferences)) return false;
    auto* ready=c->m_data.m_model;
    if(!ready || !(ready->m_fFlags&1) || !ready->m_shared || !(ready->m_shared->m_flags&2)) {if(ready)pendingModels[c]=ready;return false;}
    if(c->m_data.m_preferences.m_raceID==62)ready->SetGeometryVisible(21000,21999,0);
    if(!BindSelectorStaticTextures(c) && c->m_data.m_preferences.m_raceID==46)return false;
    if(c->m_data.m_preferences.m_raceID==46)return true;
    auto const& preferences=c->m_data.m_preferences;
    if(preferences.m_raceID==60||preferences.m_raceID==61) {
        // Reapply the authored skin/hair/wing palettes after every completed
        // component texture update. The legacy compositor can replace them.
        auto const& records=RetailSelections(preferences.m_raceID,preferences.m_sexID);
        OptionsFor(c,[&](auto const& options){SelectGeometry(c,options,records);});
        // Equipment is the last skin operation; it never replaces wing/hair slots.
        BindSelectorOutfit(c);
        return true;
    }
    BindSelectorOutfit(c);
    if(preferences.m_raceID==63 || preferences.m_raceID==69 || preferences.m_raceID==70 || (preferences.m_raceID==72 && !preferences.m_sexID)) {
        auto const& records=RetailSelections(preferences.m_raceID,preferences.m_sexID);
        OptionsFor(c,[&](auto const& options){SelectGeometry(c,options,records);});
        return true;
    }
    auto path=HaranirLayerPath(c,0,0);
    if(!path || !*path || !c->m_data.m_model || !c->m_data.m_model->m_scene
        || !c->m_data.m_model->m_shared || !c->m_data.m_model->m_shared->m_skinData) return false;
    auto& images=haranirImages[c];auto* model=c->m_data.m_model;
    auto const& p=c->m_data.m_preferences;
    auto bind=[&](unsigned group,unsigned type) {
        if(group>=images.paths.size() || images.paths[group].empty()) return;
        auto& texture=images.textures[group];
        if(!texture) texture=LoadTexture(images.paths[group].c_str());
        if(texture)model->ReplaceTexture(type,reinterpret_cast<CTexture*>(texture.get()));
    };
    if(BatchModern(c)) {
        constexpr unsigned types[]={0,5,6,19,8,20,9,13,14,7,21,22,24};
        for(unsigned group=1;group<13;++group)bind(group,types[group]);
        if((p.m_raceID==18||p.m_raceID==19||p.m_raceID==20) && c->m_baseSkinTexture)model->ReplaceTexture(1,c->m_baseSkinTexture);
        // Vulpera tail and scalp sample the full skin atlas outside Wrath's
        // clothing rectangles. Their dedicated material keeps those pixels.
        if(p.m_raceID==18)bind(0,23);
        if(p.m_raceID==11||p.m_raceID==27||p.m_raceID==28||p.m_raceID==20||((p.m_raceID==19||p.m_raceID==22)&&p.m_sexID==1)||((p.m_raceID==11||p.m_raceID==22||p.m_raceID==27)&&!p.m_sexID))bind(0,23);
    } else if(OrcModern(c)) {constexpr unsigned types[]={0,5,6,19,8,0,13};for(unsigned i=1;i<7;++i)if(types[i])bind(i,types[i]);}
    else if(TaurenModern(c)){constexpr unsigned types[]={0,5,6,19,8,20};for(unsigned i=1;i<6;++i)bind(i,types[i]);}
    else if(BloodElfProfile(p.m_raceID)) {
        bind(2,6);bind(3,5);bind(4,8);bind(6,13);
    } else {
        constexpr unsigned types[]={0,0,6,5,8,12,13,9,10};
        for(unsigned i=2;i<9;++i)bind(i,NightElfProfile(p.m_raceID)&&i==3?19:types[i]);
    }
    return true;
}
void Geometry(CCharacterComponent* c) {
    // The authored male Sethrak shield helper is bound to a forearm with a
    // ground-level offset; unlike the female rig it has no shield key bone.
    // Resolve the live left-hand socket instead of changing shared model data.
    if (c && c->m_data.m_preferences.m_raceID == 62 && c->m_data.m_preferences.m_sexID == 0) {
        auto* model = c->m_data.m_model;
        if (model && (model->m_fFlags & 1) && model->m_shared && model->m_shared->m_data) {
            auto* data = model->m_shared->m_data;
            if (data->m_attachmentIndicesById.m_count > ATTACH_HANDLEFT) {
                const auto index = data->m_attachmentIndicesById[ATTACH_HANDLEFT];
                if (index < data->m_attachments.m_count) {
                    const auto& hand = data->m_attachments[index];
                    for (auto* item = model->m_attachList; item; item = item->m_attachNext)
                        if (item->m_attachmentId == ATTACH_SHIELD)
                            item->SetAttachmentFrame(hand.m_bone, hand.m_position, 1.0f);
                }
            }
        }
    }
    if (RetailRaceActive(c)) return;
    if(!c || (c->m_data.m_flags&1))return;
    auto* ready=c->m_data.m_model;
    if(!ready || !(ready->m_fFlags&1) || !ready->m_shared || !(ready->m_shared->m_flags&2)) {if(ready)pendingModels[c]=ready;return;}
    if(!Handles(c->m_data.m_preferences)){
        GenericGeometry(c);BindSelectorStaticTextures(c);
        if(c->m_data.m_preferences.m_raceID==62)ready->SetGeometryVisible(21000,21999,0);
        return;
    }
    auto const& p=c->m_data.m_preferences;
    auto const& rows=p.m_raceID==46?HighmountainSelections():p.m_raceID==48||p.m_raceID==49?EarthenSelections():RetailSelections(p.m_raceID,p.m_sexID);
    if(NightElfProfile(p.m_raceID)&&p.m_sexID==1) {
        static std::unordered_map<CCharacterComponent*,std::uint64_t> last;
        std::uint64_t signature=p.m_esteriaExtra ^ (std::uint64_t(p.m_skinID)<<48) ^ (std::uint64_t(p.m_faceId)<<40);
        auto found=last.find(c);if(found==last.end()||found->second!=signature) {
            last[c]=signature;auto values=HighmountainFields(c);
            TraceSelector("NE state component=%p race=%u class=%u fields=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u jewelrySlot=%p",c,p.m_raceID,p.m_classId,
                values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8],values[9],values[10],values[11],values[12],c->m_data.m_model->m_textures[24]);
        }
    }
    OptionsFor(c,[&](auto const& options){SelectGeometry(c,options,rows);});
    // Sethrak's native male model includes an always-on melee armor overlay.
    // Its 21xxx skin sections are not part of the character's selectable body.
    if(p.m_raceID==62)ready->SetGeometryVisible(21000,21999,0);
}
void UpdatePending() {
    std::vector<CCharacterComponent*> completed;
    for(auto it=pendingModels.begin();it!=pendingModels.end();) {
        auto* c=it->first;auto* model=it->second;
        if(c->m_data.m_model!=model) {it=pendingModels.erase(it);continue;}
        if((model->m_fFlags&1) && model->m_shared && (model->m_shared->m_flags&2)) {
            completed.push_back(c);it=pendingModels.erase(it);
        } else ++it;
    }
    for(auto* c:completed){Geometry(c);Bind(c);}
    std::vector<CCharacterComponent*> retry(pendingSelectorBind.begin(),pendingSelectorBind.end());
    for(auto* c:retry) {
        if(!c || !c->m_data.m_model || !(c->m_data.m_model->m_fFlags&1)
            || !c->m_data.m_model->m_shared || !(c->m_data.m_model->m_shared->m_flags&2))continue;
        Geometry(c);Bind(c);
    }
}
void ResolveSection(CCharacterComponent* c,unsigned kind,int& variation,int& color) {
    auto const& p=c->m_data.m_preferences;
    if(p.m_raceID!=46 && p.m_raceID!=48 && p.m_raceID!=49)return;
    auto fields=HighmountainFields(c);
    if(p.m_raceID!=46) {
        variation=kind==1?fields[1]%10:0;
        color=kind==3?fields[3]%14:kind==2?0:fields[0]%14;return;
    }
    unsigned skin=fields[0]%9,face=fields[1]%(p.m_sexID?4:5);
    unsigned paint=p.m_sexID?(fields[1]/4)%4:fields[3]/64;
    unsigned pigment=p.m_sexID?fields[2]/81:fields[5]%3;
    variation=kind==1?face:0;
    color=kind==0||kind==1||kind==4?skin+9*(paint+4*pigment):0;
}
void Forget(CCharacterComponent* c){selectorOutfits.erase(c);pendingModels.erase(c);pendingSelectorBind.erase(c);haranirImages.erase(c);genericTextures.erase(c);previewGear.erase(c);}
void const* SkinOverride(char const* model,unsigned& bytes) {
    bytes=0;if(!model)return nullptr;
    std::string name=model;auto dot=name.rfind('.');if(dot!=std::string::npos)name.replace(dot,std::string::npos,".m2");
    // These models now ship corrected material references in their MPQ skins.
    // Their identical, older catalog copies would bypass the asset repair.
    if(!_stricmp(name.c_str(),"custom\\tarasek\\creature\\tarasek\\tarasek.m2")
        || !_stricmp(name.c_str(),"custom\\drakonid\\native\\drakonid.m2"))return nullptr;
    for(auto const& geometry:Geometries())if(!_stricmp(name.c_str(),geometry.model)) {
        bytes=unsigned(geometry.skin.size());return geometry.skin.data();
    }
    return nullptr;
}
void ReadRoster(CDataStore* packet) {
    roster.clear();retailRoster.clear();BodyShape::Roster().clear();RetailRaceClearLooks();
    if(!packet || !packet->m_data || packet->m_size<packet->m_base)return;
    unsigned size=packet->m_size-packet->m_base;
    if(size>packet->m_alloc)return;
    // The new fixed-size trailers follow our existing RCP1/HXE trailers.
    while (size >= 8) {
        unsigned magic = Field<unsigned>(packet->m_data, size - 4);
        unsigned stride = magic == CharacterBodyShape::EnumMagic ? CharacterBodyShape::EnumStride
            : magic == CharacterRetailLook::EnumMagic || magic == CharacterRetailLook::ParkedMagic ? CharacterRetailLook::EnumStride : 0;
        if (!stride) break;
        unsigned count = Field<unsigned>(packet->m_data, size - 8);
        if (count > 100 || count > (size - 8) / stride) break;
        unsigned start = size - 8 - count * stride;
        for (unsigned i = 0; i < count; ++i) {
            auto guid = Field<std::uint64_t>(packet->m_data, start + i * stride);
            auto* payload = packet->m_data + start + i * stride + 8;
            if (magic == CharacterBodyShape::EnumMagic) {
                CharacterBodyShape::Values values{};
                if (CharacterBodyShape::ReadPayload(payload, CharacterBodyShape::PayloadSize, values))
                    BodyShape::Roster()[guid] = BodyShape::FromWire(values);
            } else {
                CharacterRetailLook::Look look;
                if (CharacterRetailLook::ReadPayload(payload, CharacterRetailLook::PayloadSize, look))
                    if (magic == CharacterRetailLook::ParkedMagic) RetailRaceSetParkedLook(guid, look);
                    else RetailRaceSetLook(guid, look);
            }
        }
        size = start; packet->m_size = packet->m_base + size;
    }
    if(size>=12 && Field<unsigned>(packet->m_data,size-4)==0x31504352) {
        unsigned count=Field<unsigned>(packet->m_data,size-8);
        unsigned bytes=Field<unsigned>(packet->m_data,size-12);
        if(count<=100 && bytes<=size-12) {
            unsigned start=size-12-bytes, at=start;
            bool valid=true;
            for(unsigned i=0;i<count;++i) {
                if(at+10>size-12) { valid=false; break; }
                std::uint64_t guid=Field<std::uint64_t>(packet->m_data,at);at+=8;
                unsigned length=Field<std::uint16_t>(packet->m_data,at);at+=2;
                if(length>1024 || at+length>size-12) { valid=false; break; }
                retailRoster[guid]=std::string(reinterpret_cast<char const*>(packet->m_data+at),length);
                at+=length;
            }
            if(valid && at==size-12) { size=start;packet->m_size=packet->m_base+size; }
            else retailRoster.clear();
        }
    }
    while(size>=8) {
        unsigned magic=Field<unsigned>(packet->m_data,size-4);
        unsigned stride=magic==0x31455848?9:magic==0x32455848?16:0;
        if(!stride)break;
        unsigned count=Field<unsigned>(packet->m_data,size-8);
        if(count>100 || count>(size-8)/stride)break;
        unsigned start=size-8-count*stride;
        for(unsigned i=0;i<count;++i) {
            unsigned at=start+i*stride;
            roster[Field<std::uint64_t>(packet->m_data,at)]=stride==9?packet->m_data[at+8]:Field<std::uint64_t>(packet->m_data,at+8);
        }
        size=start;packet->m_size=packet->m_base+size;
    }
}
std::uint64_t RosterExtra(std::uint64_t guid){auto it=roster.find(guid);return it==roster.end()?0:it->second;}
std::string RosterRetailAppearance(std::uint64_t guid){auto it=retailRoster.find(guid);return it==retailRoster.end()?std::string{}:it->second;}

int CreatorCommand(lua_State* state) {
    if(!lua_isstring(state,1))return -1;
    auto command=lua_tolstring(state,1,nullptr);
    auto* c=CCharacterCreation::m_character;
    if(!command || !c)return 0;
    if (RetailRaceIndependentForms(c->m_data.m_preferences.m_raceID)
        && CCharacterCreation::m_alteredFormAtFront && CCharacterCreation::m_alteredCharacter)
        c = CCharacterCreation::m_alteredCharacter;
    // RetailRaceCycleCommand consumes unknown EA commands for mapped models, so handle the
    // creator gear toggle first. This keeps the control working for retail races and both forms.
    if(!std::strcmp(command,"EA_CREATOR_GEAR")) {
        const bool shown=lua_tonumber(state,2)!=0;
        for(auto* preview : {CCharacterCreation::m_character, CCharacterCreation::m_alteredCharacter}) {
            if(!preview) continue;
            auto& gear=previewGear[preview];
            bool changed=false;
            if(!shown) {
                for(unsigned i=0;i<12;++i) {
                    if(preview->m_itemDisplayID[i]) {
                        gear.displays[i]=preview->m_itemDisplayID[i];
                        preview->RemoveItem(static_cast<ITEM_SLOT>(i));
                        changed=true;
                    }
                }
                gear.hidden=true;
            } else if(gear.hidden) {
                for(unsigned i=0;i<12;++i)
                    if(gear.displays[i]) preview->AddItem(static_cast<ITEM_SLOT>(i),gear.displays[i],0);
                gear.hidden=false;
                gear.displays.fill(0);
                changed=true;
            }
            if(changed) preview->RenderPrep(0);
        }
        lua_pushnumber(state,shown?1:0);return 1;
    }
    const int retail = RetailRaceCycleCommand(state,c,command);
    if (retail >= 0) return retail;
    if(!std::strcmp(command,"EA_STOCK_GET")) {
        auto const& p=c->m_data.m_preferences;
        if(p.m_raceID==30||p.m_raceID==58||p.m_raceID==59) {
            int index=int(lua_tonumber(state,2))-1;
            if(index<0||index>=5)return 0;
            auto const& option=CreatureAppearance::ThinhumanMale[index];
            auto fields=HighmountainFields(c);
            lua_pushnumber(state,fields[option.field]);lua_pushnumber(state,option.count);return 2;
        }
        int index=int(lua_tonumber(state,2))-1;
        if(index<0||index>=5)return 0;
        int count=1,value=0;
        switch(index) {
            case 0:value=p.m_skinID;count=CCharacterComponent::GetNumSkins(p.m_raceID,p.m_sexID,p.m_classId,COMPONENT_CONTEXT_0);break;
            case 1:value=p.m_faceId;count=CCharacterComponent::GetNumFacesForSkin(p.m_raceID,p.m_sexID,p.m_classId,p.m_skinID,COMPONENT_CONTEXT_0);break;
            case 2:value=p.m_hairStyleId;count=CCharacterComponent::GetNumHairStylesForColor(p.m_raceID,p.m_sexID,p.m_classId,p.m_hairColorId,COMPONENT_CONTEXT_0);break;
            case 3:value=p.m_hairColorId;count=CCharacterComponent::GetNumHairColorsForStyle(p.m_raceID,p.m_sexID,p.m_classId,p.m_hairStyleId,COMPONENT_CONTEXT_0);break;
            case 4:value=p.m_facialHairId;count=CCharacterComponent::GetNumFacialFeaturesForHairColor(p.m_raceID,p.m_sexID,p.m_classId,p.m_hairColorId,COMPONENT_CONTEXT_0);break;
        }
        lua_pushnumber(state,value);lua_pushnumber(state,count);return 2;
    }
    if(!std::strcmp(command,"EA_BATCH_PROFILE")) {
        unsigned race=c->m_data.m_preferences.m_raceID;
        lua_pushnumber(state,HordeBatchAppearance::Uses(race)?race:0);return 1;
    }
    if(!std::strcmp(command,"EA_CREATOR_VIEW")||!std::strcmp(command,"EA_PREVIEW_SCALE")) {
        if(c->m_data.m_model) {
            auto const& p=c->m_data.m_preferences;
            bool head=!std::strcmp(command,"EA_CREATOR_VIEW")&&lua_tonumber(state,2)!=0;
            float scale=p.m_raceID==56||p.m_raceID==57?0.55f:1.0f;
            C3Vector position;position.x=0;position.y=0;position.z=head?-2.0f*scale*1.9f:0;
            float zoom=head?scale*2.4f:CCharacterCreation::BodyPreviewScale(p.m_raceID, p.m_sexID);
            bool adjust=p.m_raceID==1||p.m_raceID==3||p.m_raceID==7||p.m_raceID==47||p.m_raceID==29||p.m_raceID==31||p.m_raceID==48||p.m_raceID==49;
            if(head && adjust) {
                float height=p.m_raceID==1?1.9f:p.m_raceID==7||p.m_raceID==47?0.9f:1.35f;
                auto* shared=c->m_data.m_model->m_shared;
                if(shared && (shared->m_flags&1) && shared->m_data) {
                    auto* data=shared->m_data;
                    if(data->m_attachmentIndicesById.m_count>11) {
                        unsigned index=data->m_attachmentIndicesById[11];
                        if(index<data->m_attachments.m_count) {
                            auto const& attachment=data->m_attachments[index];
                            float candidate=attachment.m_position.z;
                            if(candidate<0.6f && attachment.m_bone<data->m_bones.m_count)
                                candidate+=data->m_bones[attachment.m_bone].m_pivot.z;
                            if(std::isfinite(candidate) && candidate>=0.6f && candidate<=3.0f)height=candidate;
                        }
                    }
                }
                height=std::clamp(height,0.6f,3.0f);
                zoom=2.4f*1.9f/height;
                position.z=1.5f-zoom*height;
            }
            c->m_data.m_model->SetWorldTransform(position,CCharacterCreation::m_charFacing,zoom);
            lua_pushnumber(state,head?1:0);return 1;
        }return 0;
    }
    if(!std::strcmp(command,"EA_SELECT")) {
        int index=int(lua_tonumber(state,2))-1;
        auto* display=index>=0?CCharacterSelection::GetCharacterDisplay(index):nullptr;
        if(!display)return 0;
        lua_pushnumber(state,display->m_characterInfo.m_raceID);lua_pushnumber(state,display->m_characterInfo.m_sexID);return 2;
    }
    if(HighmountainCycle(state,command))
        return !std::strcmp(command,"EA_GET")?3:!std::strcmp(command,"EA_CHOICES")?1:0;
    if(!ExtendedRace(c->m_data.m_preferences.m_raceID))return GenericCommand(state,c,command);
    int index=int(lua_tonumber(state,2))-1;
    int returns=0;
    OptionsFor(c,[&](auto const& options){
        if(index<0 || unsigned(index)>=options.size())return;
        auto fields=HighmountainFields(c);auto const& option=options[index];
        unsigned value=fields[option.field]/option.factor%option.count;
        if(!std::strcmp(command,"EA_GET")) {
            lua_pushnumber(state,value);lua_pushnumber(state,option.count);
            auto const& profile=BatchControlProfile(c->m_data.m_preferences.m_raceID,c->m_data.m_preferences.m_sexID);
            lua_pushstring(state,unsigned(index)<profile.labels.size()?profile.labels[index]:"Appearance");returns=3;return;
        }
        if(!std::strcmp(command,"EA_CHOICES")){
            std::string choices;
            for(unsigned candidate=0;candidate<option.count;++candidate) {
                auto values=fields;
                values[option.field]=std::uint8_t(values[option.field]-value*option.factor+candidate*option.factor);
                if(ValidateExtended(c->m_data.m_preferences.m_raceID,c->m_data.m_preferences.m_sexID,values))
                    choices+=std::to_string(candidate)+",";
            }
            lua_pushstring(state,choices.c_str());returns=1;return;
        }
        if(std::strcmp(command,"EA_SET") && std::strcmp(command,"EA_CYCLE"))return;
        int delta=int(lua_tonumber(state,3));
        unsigned next=!std::strcmp(command,"EA_SET")?unsigned(delta):unsigned((int(value)+delta%int(option.count)+int(option.count))%int(option.count));
        if(next>=option.count)return;
        unsigned encoded=fields[option.field]-value*option.factor+next*option.factor;
        if(encoded>255)return;fields[option.field]=std::uint8_t(encoded);
        auto const& p=c->m_data.m_preferences;
        if(HordeBatchAppearance::Uses(p.m_raceID))fields=HordeBatchAppearance::Normalize(p.m_raceID,p.m_sexID,fields,p.m_classId);
        else if(NormalElfAppearance::Uses(p.m_raceID))fields=NormalElfAppearance::Normalize(p.m_raceID,p.m_sexID,fields,p.m_classId);
        if(!ValidateExtended(p.m_raceID,p.m_sexID,fields))return;
        SetHighmountainFields(c,fields);
        const auto gearIt=previewGear.find(c);
        const bool preserveGear=gearIt!=previewGear.end() && gearIt->second.hidden;
        const PreviewGear savedGear=preserveGear?gearIt->second:PreviewGear{};
        Forget(c);
        if(preserveGear)previewGear[c]=savedGear;
        c->SetSkinColor(c->m_data.m_preferences.m_skinID,true,true,nullptr);
        c->m_flags|=4;c->SetDirtySections(0x3ff);Geometry(c);Bind(c);
    });
    return returns;
}
void AppendCreate(CDataStore* packet) {
    auto* c=CCharacterCreation::m_character;
    if(!packet||!c)return;
    if(!Handles(c->m_data.m_preferences)) { RetailRaceAppendCreate(packet); return; }
    auto const& p=c->m_data.m_preferences; auto fields=HighmountainFields(c);
    unsigned race=p.m_raceID;
    if(CreatureAppearance::Uses(race)) { RetailRaceAppendCreate(packet); return; }
    if(packet->m_size<=packet->m_base) { RetailRaceAppendCreate(packet); return; }
    packet->m_data[packet->m_size-packet->m_base-1]=
        race==50||race==51||race==6||HordeBatchAppearance::Uses(race)?0:fields[5];
    if(BloodElfProfile(race)||NightElfProfile(race)||OrcModern(c)) {
        packet->Put(std::uint8_t(NormalElfAppearance::Uses(race)?(p.m_esteriaExtra>>8)&255:fields[6]));
        packet->Put(std::uint8_t(fields[7]|(NormalElfAppearance::Uses(race)?0x80:0)));
        packet->Put(std::uint32_t(0x31434844));
    } else if(BatchModern(c)||TaurenModern(c)||race==50||race==51) {
        for(unsigned i=5;i<13;++i)packet->Put(fields[i]);
        packet->Put(std::uint32_t(BatchModern(c)?0x31484342:TaurenModern(c)?0x314E5254:0x31435248));
    }
    RetailRaceAppendCreate(packet);
}
} // namespace EsteriaAppearance
