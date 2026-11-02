#pragma once
#include <cstring>
#include <string>
#include <algorithm>
#include <cctype>

// Apply corrections before shader substitution; all M2 arrays are relocated here.
inline void M2RepairSelectorMaterials(CM2Shared* shared) {
    auto* data=shared->m_data;auto* skin=shared->m_skinData;
    if(!data||!skin)return;
    std::string name=shared->m_fileName;
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
    bool draenei=name.find("draeneiretail")!=std::string::npos||name.find("lightforgedretail")!=std::string::npos;
    bool skyborne=name.find("skyborne")!=std::string::npos;
    bool nightElf=name.find("nightelf")!=std::string::npos;
    bool nightElfFemale=nightElf&&name.find("female")!=std::string::npos;
    if(!draenei&&!skyborne&&!nightElf)return;
    unsigned bareCombo=~0u, vineCombo=~0u, vineMaterial=~0u;
    for(unsigned i=0;i<data->m_textureCombos.m_count;++i) {
        unsigned index=data->m_textureCombos[i];
        if(index>=data->m_textures.m_count)continue;
        if(data->m_textures[index].m_type==23&&bareCombo==~0u)bareCombo=i;
        if(data->m_textures[index].m_type==10&&vineCombo==~0u)vineCombo=i;
    }
    if(nightElfFemale)for(unsigned i=0;i<data->m_materials.m_count;++i)
        if(data->m_materials[i].m_flags==4&&data->m_materials[i].m_blendMode==1){vineMaterial=i;break;}
    for(unsigned i=0;i<skin->m_batches.m_count;++i) {
        auto& batch=skin->m_batches[i];
        if(batch.m_skinSectionIndex>=skin->m_skinSections.m_count||batch.m_materialIndex>=data->m_materials.m_count||batch.m_textureCount!=1||batch.m_textureComboIndex>=data->m_textureCombos.m_count)continue;
        unsigned index=data->m_textureCombos[batch.m_textureComboIndex];
        if(index>=data->m_textures.m_count)continue;
        auto const& texture=data->m_textures[index];
        unsigned id=skin->m_skinSections[batch.m_skinSectionIndex].m_skinSectionId&65535;
        // Retail's leafy necklace/headdress use replaceable type 10, not the
        // static metal jewelry atlas assigned by the converted player model.
        // Restore their original two-sided cutout material as well.
        if(nightElfFemale&&(id==3603||id==3702)&&vineCombo<65536&&vineMaterial<65536) {
            batch.m_textureComboIndex=static_cast<uint16_t>(vineCombo);
            batch.m_materialIndex=static_cast<uint16_t>(vineMaterial);
            continue;
        }
        // Tail UVs overlap the head/clothing compositor. Retain the authored bare atlas.
        if(draenei && id/100==38 && texture.m_type==1 && bareCombo<65536)
            batch.m_textureComboIndex=static_cast<uint16_t>(bareCombo);
        if(texture.m_type||!texture.m_filename.m_count)continue;
        auto* path=texture.m_filename.Data();
        if(!path||!std::memchr(path,0,texture.m_filename.m_count))continue;
        bool cutout=(skyborne&&std::strstr(path,"feather_color"))||(nightElf&&std::strstr(path,"FemaleJewelry"));
        if(cutout) {
            // Jewelry contains smooth chain/filigree coverage, not a binary mask.
            data->m_materials[batch.m_materialIndex].m_blendMode=nightElf?2:1;
            if(skyborne) {
                auto const& section=skin->m_skinSections[batch.m_skinSectionIndex];
                unsigned end=unsigned(section.m_vertexStart)+section.m_vertexCount;
                if(end>skin->m_vertices.m_count)continue;
                for(unsigned vertex=section.m_vertexStart;vertex<end;++vertex) {
                    unsigned index=skin->m_vertices[vertex];
                    if(index>=data->m_vertices.m_count)continue;
                    auto& uv=data->m_vertices[index].m_texcoord[0];
                    // Expanded models retain a half-atlas U offset; the shipped feather
                    // BLPs contain their nontransparent coverage only in the left half.
                    if(uv.x>=0.5f&&uv.x<=1.0f)uv.x-=0.5f;
                }
            }
        }
    }
}
