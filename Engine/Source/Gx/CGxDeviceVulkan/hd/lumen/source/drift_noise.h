// Lumenveil GPL-2.0 adaptation; see licences and provenance.
#pragma once

#include <cstdint>
#include <vector>

namespace HD::Lumen::Source {

constexpr int kDriftNoiseSize = 128;
constexpr int kDriftCellsPerTile = 4;
constexpr int kDriftWispCellsPerTile = 8;
constexpr int kDriftChannels = 4;
constexpr int kDriftCoarseChannel = 0;
constexpr int kDriftFineChannel = 1;
constexpr int kDriftSwirlXChannel = 2;
constexpr int kDriftSwirlYChannel = 3;

struct DriftNoiseLevel
{
    int size;
    std::vector<uint32_t> argb;
};

std::vector<DriftNoiseLevel> BuildDriftNoise();
float DriftNoiseChannel(uint32_t argb, int channel);
float DriftNoiseMean(const DriftNoiseLevel& level, int channel);

}
