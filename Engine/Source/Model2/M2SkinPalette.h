#pragma once
#include "Engine/Source/Model2/M2BoneBudget.h"
#include <algorithm>
#include <array>
#include <vector>
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Os/OsMemory.h"

// Retail skins often declare every skeleton bone in every section, although
// the section uses far fewer. Overlapping windows share a compact palette.
// This changes the loaded model's private arrays; the archived assets stay intact.
inline bool M2CompactSkinPalette(CM2Shared* shared) {
#if defined(WOWDECOMP_ARCH_64)
    auto* data=shared->m_data;
    auto* skin=shared->m_skinData;
    if(!data || !skin || shared->m_esteriaBoneCombos) return true;
    unsigned limit=M2BoneLimit(shared->m_fileName);
    bool needed=false;
    for(unsigned i=0;i<skin->m_skinSections.m_count;++i)
        needed=needed || skin->m_skinSections[i].m_boneCount>limit;
    if(!needed)return true;
    unsigned count=skin->m_vertices.m_count;
    if(!count || count>65535 || skin->m_bones.m_count<count)return false;
    std::vector<int> owner(count,-1);
    std::vector<unsigned> parent(skin->m_skinSections.m_count);
    for(unsigned i=0;i<parent.size();++i)parent[i]=i;
    auto find=[&](unsigned i) { while(parent[i]!=i) {parent[i]=parent[parent[i]];i=parent[i];}return i; };
    std::vector<std::array<int,4>> global(count);
    for(auto& item:global)item.fill(-1);
    std::vector<ubyte4> indices(skin->m_bones.Data(),skin->m_bones.Data()+count);
    std::vector<M2SkinSection> sections(skin->m_skinSections.Data(),skin->m_skinSections.Data()+skin->m_skinSections.m_count);
    std::vector<uint16_t> combos;
    unsigned maxUsed=0;
    for(unsigned si=0;si<sections.size();++si) {
        auto const& section=sections[si];
        unsigned begin=section.m_vertexStart, end=begin+section.m_vertexCount;
        if(end>count || section.m_boneCount>256 || section.m_boneInfluences>4)return false;
        for(unsigned v=begin;v<end;++v) {
            if(skin->m_vertices[v]>=data->m_vertices.m_count)return false;
            if(owner[v]>=0)parent[find(si)]=find(static_cast<unsigned>(owner[v]));
            else owner[v]=si;
            auto const& vertex=data->m_vertices[skin->m_vertices[v]];
            for(unsigned k=0;k<4;++k) {
                if(!vertex.m_weights.b[k])continue;
                unsigned local=skin->m_bones[v].b[k];
                unsigned source=unsigned(section.m_boneComboIndex)+local;
                if(local>=section.m_boneCount || source>=data->m_boneCombos.m_count)return false;
                uint16_t bone=data->m_boneCombos[source];
                if(bone>=data->m_bones.m_count)return false;
                if(global[v][k]>=0 && global[v][k]!=bone)return false;
                global[v][k]=bone;
            }
        }
    }
    std::vector<std::vector<uint16_t>> palettes(sections.size());
    for(unsigned v=0;v<count;++v)if(owner[v]>=0) {
        auto& palette=palettes[find(static_cast<unsigned>(owner[v]))];
        for(auto bone:global[v])if(bone>=0 && std::find(palette.begin(),palette.end(),bone)==palette.end())palette.push_back(static_cast<uint16_t>(bone));
        if(palette.size()>limit)return false;
    }
    std::vector<unsigned> bases(sections.size());
    for(unsigned i=0;i<palettes.size();++i)if(find(i)==i) {
        auto& palette=palettes[i];if(palette.empty())palette.push_back(0);
        bases[i]=static_cast<unsigned>(combos.size());
        combos.insert(combos.end(),palette.begin(),palette.end());
        maxUsed=std::max(maxUsed,static_cast<unsigned>(palette.size()));
    }
    if(combos.size()>65535)return false;
    for(unsigned i=0;i<sections.size();++i) {
        unsigned root=find(i);
        sections[i].m_boneComboIndex=static_cast<uint16_t>(bases[root]);
        sections[i].m_boneCount=static_cast<uint16_t>(palettes[root].size());
    }
    for(unsigned v=0;v<count;++v)if(owner[v]>=0) {
        auto const& palette=palettes[find(static_cast<unsigned>(owner[v]))];
        for(unsigned k=0;k<4;++k)indices[v].b[k]=global[v][k]<0?0:static_cast<uint8_t>(std::find(palette.begin(),palette.end(),global[v][k])-palette.begin());
    }
    auto* memory=static_cast<uint16_t*>(OsAllocNear(data,combos.size()*sizeof(uint16_t)));
    if(!memory)return false;
    std::copy(combos.begin(),combos.end(),memory);
    std::copy(indices.begin(),indices.end(),skin->m_bones.Data());
    std::copy(sections.begin(),sections.end(),skin->m_skinSections.Data());
    shared->m_esteriaBoneCombos=memory;
    data->m_boneCombos.Set(memory);
    data->m_boneCombos.m_count=static_cast<uint32_t>(combos.size());
    skin->m_boneCountMax=limit;
    DebugLog("Compacted skin palettes: %s sections=%u maxUsed=%u",shared->m_fileName,static_cast<unsigned>(sections.size()),maxUsed);
#endif
    return true;
}
