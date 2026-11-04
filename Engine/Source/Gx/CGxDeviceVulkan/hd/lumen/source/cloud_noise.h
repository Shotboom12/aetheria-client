// Lumenveil 0.1.1 native x64 adaptation. GPL-2.0; see NOTICES.md and upstream manifest.
#pragma once

#include <cstdint>
#include <vector>

namespace HD::Lumen::Source {

constexpr int kCloudNoiseSize = 256;
constexpr int kCloudCellsPerTile = 4;
constexpr int kCloudOctaves = 5;

struct CloudNoiseLevel
{
    int size;
    std::vector<uint8_t> texels;
};

std::vector<CloudNoiseLevel> BuildCloudNoise();
float CloudNoiseShareAbove(const CloudNoiseLevel& level, float threshold);

}
