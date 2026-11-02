#pragma once
#include <algorithm>
#include <cstring>
#include <vector>
#include "Engine/Source/Gx/Blit.h"
#include "WoW/Common/TextureCache.h"

// The legacy compositor only handles indexed BLPs. Decode the other BLP2
// formats into BGRA before blending, rather than painting the green placeholder.
inline bool EsteriaPasteDecoded(void* texture,MipBits* destination,C2iVector const& dst,
    C2iVector const& src,C2iVector const& size,unsigned side,unsigned sourceStart,bool upscale=false) {
    auto* entry=static_cast<CACHEENTRY*>(texture);
    if(!entry || !entry->m_data || entry->m_loadFailed || !destination)return false;
    auto const& header=*entry->m_data;
    if((header.m_colorEncoding!=2 && header.m_colorEncoding!=3) || header.m_width>4096 || header.m_height>4096)return false;
    for(unsigned level=0;side && level<16;++level,side>>=1) {
        unsigned mip=upscale?(level?level-1:0):sourceStart+level;
        if(mip>=16 || mip>=entry->m_info.m_mipCount)break;
        unsigned width=std::max(1u,header.m_width>>mip),height=std::max(1u,header.m_height>>mip);
        unsigned offset=header.m_mipOffsets[mip],length=header.m_mipSizes[mip];
        if(offset>entry->m_size || length>entry->m_size-offset || !destination->m_mip[level])return false;
        auto const* raw=reinterpret_cast<unsigned char const*>(entry->m_data)+offset;
        std::vector<unsigned char> pixels(width*height*4);
        if(header.m_colorEncoding==3) {
            if(length<pixels.size())return false;
            std::memcpy(pixels.data(),raw,pixels.size());
        } else {
            unsigned bytes=header.m_preferredFormat==0?8:16;
            unsigned stride=((width+3)/4)*bytes;
            if(length<stride*((height+3)/4))return false;
            BlitFormat format=header.m_preferredFormat==0?BlitFormat_Dxt1:header.m_preferredFormat==1?BlitFormat_Dxt3:BlitFormat_Dxt5;
            C2iVector dimensions={static_cast<int>(width),static_cast<int>(height)};
            if(!Blit(dimensions,BlitAlpha_0,raw,stride,format,pixels.data(),width*4,BlitFormat_Argb8888))return false;
        }
        unsigned x0=static_cast<unsigned>(std::max(0,dst.x))>>level;
        unsigned y0=static_cast<unsigned>(std::max(0,dst.y))>>level;
        unsigned copyWidth=std::max(1u,static_cast<unsigned>(size.x)>>level);
        unsigned copyHeight=std::max(1u,static_cast<unsigned>(size.y)>>level);
        unsigned sx=static_cast<unsigned>(std::max(0,src.x))>>level;
        unsigned sy=static_cast<unsigned>(std::max(0,src.y))>>level;
        auto* output=reinterpret_cast<unsigned char*>(destination->m_mip[level]);
        for(unsigned y=0;y<copyHeight && y0+y<side;++y)for(unsigned x=0;x<copyWidth && x0+x<side;++x) {
            unsigned tx=upscale && !level?(sx+x)/2:sx+x;
            unsigned ty=upscale && !level?(sy+y)/2:sy+y;
            if(tx>=width || ty>=height)continue;
            auto const* color=pixels.data()+4*(ty*width+tx);
            auto* pixel=output+4*((y0+y)*side+x0+x);
            unsigned alpha=header.m_alphaSize?color[3]:255;
            for(unsigned k=0;k<3;++k)pixel[k]=static_cast<unsigned char>((color[k]*alpha+pixel[k]*(255-alpha)+127)/255);
            pixel[3]=255;
        }
    }
    return true;
}
