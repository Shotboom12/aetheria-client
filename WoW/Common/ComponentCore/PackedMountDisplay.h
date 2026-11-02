#pragma once
#include "Engine/Source/Retail/PackedMounts.h"
#include "WoW/Source/DB/Rec/CreatureDisplayInfoRec.h"
#include "WoW/Source/DB/Rec/CreatureModelDataRec.h"

// Scoped render records: never mutate shared DBCs or server-owned display IDs.
// Strings live through model creation and ReplaceMonsterSkin's synchronous texture binding.
class PackedMountDisplay {
    CreatureDisplayInfoRec display;
    CreatureModelDataRec model;
    std::string path, textures[3];
public:
    void Apply(const CreatureDisplayInfoRec*& displayRec, const CreatureModelDataRec*& modelRec) {
        Retail::PackedMount appearance;
        if (!displayRec || !modelRec || !Retail::GetPackedMount(displayRec->m_id, appearance)) return;
        display = *displayRec;
        model = *modelRec;
        path = "mountstudio\\" + std::to_string(appearance.model) + ".m2";
        model.m_modelName = path.c_str();
        display.m_creatureModelScale = appearance.scale;
        for (unsigned i = 0; i < 3; ++i) {
            textures[i] = appearance.textures[i] ? std::to_string(appearance.textures[i]) + ".blp" : "";
            display.m_textureVariation[i] = textures[i].c_str();
        }
        displayRec = &display;
        modelRec = &model;
    }
};
