#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDFOREVER_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDFOREVER_H

// Waystone HD: WoW Forever light colours (not part of the original client).
//   hdForever      0 = original 3.3.5 lights (default)
//                  1 = lights that also exist in WoW Forever use Forever's colours
//                      (sun, ambient, sky, fog colour, sun and cloud colours).
//                      Fog distance, shadows, clouds layers and water keep the 3.3.5 values.
//                      Lights Forever does not have (TBC, WotLK, Turtle) are not touched.
//   hdForeverForce 0 = Forever setting by light (default); a setting id = use it for every light, also for
//                  hdFog and hdGrade (to match zones Forever has no light for in our data, e.g. Ghostlands)
//   hdForeverLight 1 = the world (ground, models, buildings) gets Forever's sun and ambient colours (default),
//                  0 = the 3.3.5 ones, in between = a mix. Sky, fog, clouds use Forever whenever hdForever is 1.
//                  Forever lights the world with 3 ambient colours (sky, horizon, ground; hdTerrainLight does it on the
//                  ground) and cuts the sun in shadow. The turquoise ground of the first Amani Pass test (2026-10-19)
//                  came from forcing setting 7624, which has the same sky and fog as 7618 but a yellow-green sun and
//                  teal ambient like the underwater settings (light slot 1); 7618 looks like the clear-weather one.
//   hdForeverInfo  read only, what was loaded:  /run print(GetCVar("hdForeverInfo"))
// Data file: forever_data\hd_forever_light.txt (made by forever_tools\make_hd_forever_light.py).
// Ambient:   forever_data\hd_forever_ambient.txt (made by forever_tools\make_hd_forever_ambient.py), horizon and
//            ground ambient colours for the terrain lighting (hdTerrainLight).
// Clouds:    forever_data\hd_forever_sky.txt (made by forever_tools\make_hd_forever_sky.py), band 12 (cloud base colour)
//            from Forever's CloudEmissiveColor, loaded like hd_forever_light.txt (optional; used by the clouds, see HdSky.h).
// Fog file:  forever_data\hd_forever_fog.txt (made by forever_tools\make_hd_forever_fog.py), used by hdFog.

#include <cstdint>

class CImVector;
class LightParamsRec;
class LightRec;

namespace HD {
    // Whether Forever supplied a light entry for this map/light pair.
    bool ForeverHasLight(const LightRec* light);
    // Called by DayNight::GetLightParams. Returns the record to use for this light and slot:
    // `original` when hdForever is off or Forever has nothing for it, else an HD record.
    const LightParamsRec* ForeverLightParams(const LightRec* light, int32_t slot, const LightParamsRec* original);

    // The 3.3.5 LightParams ID behind a record (the HD records keep the 3.3.5 one for bands Forever leaves empty).
    int32_t RealLightParamsId(const LightParamsRec* params);

    // True (and *color set) when this band comes from Forever for this record.
    bool ForeverIntBand(const LightParamsRec* params, int32_t band, int32_t time, CImVector* color);

    // How much of Forever's colour to use for this band (0 = 3.3.5 colour, 1 = Forever's): hdForeverLight for
    // the world light bands (0 sun, 1 ambient), 1 for the others.
    float ForeverBandWeight(int32_t band);

    // Forever's three ambient colours (0..1) for this light and slot at this time (0-2880 half-minutes), as its terrain
    // shader gets them: sky = LightData AmbientColor, horizon = HorizonAmbientColor, ground = GroundAmbientColor,
    // blended between Forever's time keys. A horizon or ground colour Forever leaves black is the sky colour.
    // False when hdForever is 0 or Forever has no setting for this light (sky/horizon/ground untouched).
    bool ForeverAmbientsAt(const LightRec* light, int32_t slot, int32_t time, float* sky, float* horizon, float* ground);

    // One Forever fog layer (LightDataGlobalVolumeFog), as Forever's global fog shader reads it (GlobalFogParams).
    // Only the layers the Forever client draws (flag 8) are in the file. Colours 0..1, values unscaled.
    struct ForeverFogLayer {
        int32_t flags;            // 1 = sun shadow (shadow colour/density when the sun is down), 2 = heights relative
                                  // to the player, 4 = noise, 8 = drawn; 0 = no layer here
        float diffuse[3];         // sun-lit colour, added toward the sun (column 1)
        float emissive[3];        // base colour (column 2)
        float shadow[3];          // base colour in shadow (column 3)
        float start;              // distance where the layer starts, yards (column 5)
        float density;            // x 0.01 = per yard (column 6)
        float shadowMul;          // density in shadow (column 7)
        float topRate, topHeight; // thins above topHeight: exp(-topRate * 0.01 * (z - topHeight)) (columns 8, 10)
        float bottomRate, bottomHeight; // thins below bottomHeight (columns 11, 12)
        float intensity;          // sun scatter strength (column 14)
        float g;                  // phase: 0 = even, 0.9 = strongly toward the sun (column 15)
        float strength, exponent; // denser with distance: 1 + strength * (distance share)^exponent (columns 25, 26)
    };

    // Blend two fog layers (t = 0 keeps a, 1 gives b). A missing layer (flags 0) takes the other's colours,
    // so it only fades in by density.
    void LerpFogLayer(ForeverFogLayer& a, const ForeverFogLayer& b, float t);

    struct ForeverFog {
        int32_t count;
        int32_t setting;
        ForeverFogLayer layers[3];
    };

    // Forever's fog for this light and slot at this time (0-2880 half-minutes), interpolated between
    // Forever's time keys. False when Forever has no fog for this light (TBC, WotLK, Turtle lights).
    bool ForeverFogAt(const LightRec* light, int32_t slot, int32_t time, ForeverFog* out);

    // Forever's colour grading for this light and slot at this time (0-2880 half-minutes): the LUTs of the two
    // time keys around it (forever_data\grading\<lut>.blp) and how far between them (0 = lutA, 1 = lutB).
    // False when Forever has no grading for this light. Data: forever_data\hd_forever_grading.txt.
    bool ForeverGradingAt(const LightRec* light, int32_t slot, int32_t time, int32_t* lutA, int32_t* lutB, float* t);
}

#endif
