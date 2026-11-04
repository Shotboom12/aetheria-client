#include "LvFogConstants.h"
#include <algorithm>
#include <cmath>
namespace HD::Lumen::Source {
namespace {constexpr float kCloudMinLightRise=.02f,kEdgeOccluderViewZRatio=2.5f,kShaftFadeStart=.7f,kDriftFlattening=2.5f,kBankFlattening=1.3f,kFineDriftScale=5.f,kSwirlYardsPerPatch=.8f,kDriftFootprintPerStep=.25f,kMistRange=500.f;constexpr double kSwirlCentre=.5;}
BankConstants BuildBankConstants(const FrameInputs& in, const FogParams& fog, const TerrainShadowConstants& floors,
                                 bool floorsKnown)
{
    BankConstants c = {};
    const bool known = floorsKnown && floors.uvPerYard > 0.0f;
    c.floor = {floors.uvAtCamera[0], floors.uvAtCamera[1], known ? floors.uvPerYard : 0.0f, known ? 1.0f : 0.0f};
    c.shape = {fog.bankSharpness, fog.bankThresholdRise, fog.bankPoolGain, fog.bankLift};
    for (int g = 0; g < kBankGrounds; ++g)
        c.grounds[g] = {fog.banks[g].threshold, fog.banks[g].norm, std::max(fog.banks[g].height, 1.0f),
                        fog.banks[g].density};
    c.light = {in.toLight[0] * fog.bankShadeYards, in.toLight[1] * fog.bankShadeYards,
               in.toLight[2] * fog.bankShadeYards, fog.bankShade};
    c.misc = {fog.referenceZ, fog.bankStrength, fog.bankValleysOnly, fog.bankShadeNoiseScale};
    c.noise = {kBankWispErosion, 0.0f, 0.0f, 0.0f};
    return c;
}

CloudLookup CloudConstants(const FrameInputs& in, const FogParams& fog, const double* offset, bool noise)
{
    if (!noise || !(fog.cloudShadow > 0.0f) || !(fog.cloudTileYards > 0.0f))
        return {{0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};
    const double tile = fog.cloudTileYards;
    const double u = (static_cast<double>(in.camPos[0]) - offset[0]) / tile;
    const double v = (static_cast<double>(in.camPos[1]) - offset[1]) / tile;
    const float rise = std::max(in.toLight[2], kCloudMinLightRise);
    return {{static_cast<float>(u - std::floor(u)), static_cast<float>(v - std::floor(v)),
             static_cast<float>(1.0 / tile), fog.cloudHeight},
            {in.toLight[0] / rise, in.toLight[1] / rise, 1.0f - fog.cloudCover, fog.cloudShadow}};
}

Float4 ShaftFadeConstants(float shaftRange, float shaftDepth, float shaftGlare)
{
    const float edgeThicknessLevels = std::log(1.0f + shaftDepth) / std::log(kEdgeOccluderViewZRatio);
    const float glarePerBrightness = 1.0f / std::max(shaftGlare, kMinShaftGlare);
    if (shaftRange <= 0.0f)
        return {0.0f, 1.0f, edgeThicknessLevels, glarePerBrightness};
    const float perYard = 1.0f / ((1.0f - kShaftFadeStart) * shaftRange);
    return {perYard, -kShaftFadeStart * shaftRange * perYard, edgeThicknessLevels, glarePerBrightness};
}

double Wrapped(double textureCoordinate)
{
    return textureCoordinate - std::floor(textureCoordinate);
}

float CoarseDriftPerYard(const Config& cfg)
{
    return 1.0f / (kDriftCellsPerTile * cfg.driftSize);
}

DriftConstants BuildDriftConstants(const FrameInputs& in, const FogParams& fog, const Config& cfg,
                                   const double* coarseDrift, const double* fineDrift, bool noiseBound)
{
    const float coarseXY = CoarseDriftPerYard(cfg);
    const float flattening =
        kDriftFlattening + (kBankFlattening - kDriftFlattening) * std::clamp(fog.bankStrength, 0.0f, 1.0f);
    const float coarseZ = coarseXY * flattening;
    const float fineXY = coarseXY * kFineDriftScale;
    const float fineZ = fineXY * flattening;
    const double coarsePerYard[3] = {coarseXY, coarseXY, coarseZ};
    const double finePerYard[3] = {fineXY, fineXY, fineZ};
    const float swirl = cfg.driftSwirl * kSwirlYardsPerPatch * cfg.driftSize * fineXY;
    const double swirlCentring[3] = {-kSwirlCentre * swirl, -kSwirlCentre * swirl, 0.0};
    float coarseOrigin[3];
    float fineOrigin[3];
    for (int k = 0; k < 3; ++k)
    {
        coarseOrigin[k] = static_cast<float>(Wrapped(in.camPos[k] * coarsePerYard[k] + coarseDrift[k]));
        fineOrigin[k] = static_cast<float>(Wrapped(in.camPos[k] * finePerYard[k] + fineDrift[k] + swirlCentring[k]));
    }
    const float amplitude = noiseBound ? fog.drift : 0.0f;
    DriftConstants c;
    c.coarse = {coarseOrigin[0], coarseOrigin[1], coarseOrigin[2], 2.0f * amplitude};
    c.fine = {fineOrigin[0], fineOrigin[1], fineOrigin[2], std::log2(kFineDriftScale)};
    c.scale = {coarseXY, coarseZ, fineXY, fineZ};
    c.mist = {fog.mistTop, fog.mistFalloff, fog.mistBoost, 1.0f / kMistRange};
    c.detail = {kDriftNoiseSize * coarseXY * kDriftFootprintPerStep, swirl, cfg.driftDetail, 1.0f - amplitude};
    return c;
}
}
