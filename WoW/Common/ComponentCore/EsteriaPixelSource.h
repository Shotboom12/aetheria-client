#include "Engine/Source/Gx/Blit.h"
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct AccessoryPixelSource
{
    std::vector<unsigned char> bytes;
    std::array<std::vector<unsigned char>, 16> levels;
    std::string path;

    unsigned Word(unsigned offset) const
    {
        unsigned value = 0;
        if (offset <= bytes.size() && bytes.size() - offset >= 4)
            std::memcpy(&value, bytes.data() + offset, 4);
        return value;
    }

    bool Valid() const
    {
        return bytes.size() >= 148 && Word(0) == 0x32504C42 && Word(4) == 1
            && (bytes[8] >= 1 && bytes[8] <= 3) && Word(12) && Word(12) <= 1024
            && Word(16) && Word(16) <= 1024
            && (bytes[8] != 1 || bytes.size() >= 1172
                && (bytes[9] == 0 || bytes[9] == 1 || bytes[9] == 4 || bytes[9] == 8));
    }

    bool Mip(unsigned level, unsigned& pitch, void*& pixels)
    {
        pitch = 0;
        pixels = nullptr;
        if (!Valid() || level >= levels.size()) return false;
        unsigned const width = (std::max)(1u, Word(12) >> level);
        unsigned const height = (std::max)(1u, Word(16) >> level);
        unsigned const count = width * height;
        unsigned const offset = Word(20 + level * 4);
        unsigned const length = Word(84 + level * 4);
        unsigned const expected = bytes[8] == 2 ? ((width+3)/4)*((height+3)/4)*(bytes[10]==0?8:16) : bytes[8] == 3 ? count * 4
            : count + (count * bytes[9] + 7) / 8;
        if (offset < (bytes[8] == 1 ? 1172u : 148u) || offset > bytes.size()
            || length < expected || length > bytes.size() - offset) return false;
        auto& data = levels[level];
        if (data.empty())
        {
            data.resize(count * 4);
            // ESTERIA_DXT_PIXELS
            if(bytes[8]==2) {
                auto format=bytes[10]==0?BlitFormat_Dxt1:bytes[10]==1?BlitFormat_Dxt3:BlitFormat_Dxt5;
                C2iVector dimensions={static_cast<int>(width),static_cast<int>(height)};
                if(!Blit(dimensions,BlitAlpha_0,bytes.data()+offset,((width+3)/4)*(bytes[10]==0?8:16),format,data.data(),width*4,BlitFormat_Argb8888)){data.clear();return false;}
            }
            else if (bytes[8] == 3)
                std::copy_n(bytes.data() + offset, data.size(), data.data());
            else
            {
                for (unsigned i = 0; i < count; ++i)
                {
                    unsigned const index = bytes[offset + i];
                    std::copy_n(bytes.data() + 148 + index * 4, 3, data.data() + i * 4);
                    unsigned alpha = 255;
                    if (bytes[9] == 8) alpha = bytes[offset + count + i];
                    else if (bytes[9] == 4)
                        alpha = ((bytes[offset + count + i / 2] >> ((i % 2) * 4)) & 15) * 17;
                    else if (bytes[9] == 1)
                        alpha = ((bytes[offset + count + i / 8] >> (i % 8)) & 1) * 255;
                    data[i * 4 + 3] = static_cast<unsigned char>(alpha);
                }
            }
        }
        pitch = width * 4;
        pixels = data.data();
        return true;
    }

    void Finish()
    {
        for (auto& level : levels) std::vector<unsigned char>().swap(level);
    }
};
