#ifndef ENGINE_MODEL2_HD_EXTRA_LAMPS_H
#define ENGINE_MODEL2_HD_EXTRA_LAMPS_H

#include <cstdint>

class M2Data;

namespace HD::ExtraLamps {
// HD: hdExtraLamps. Many Blizzard lamp posts and lanterns ship without an M2 point light (only a glowing
// texture), so nothing lights the ground, the characters or the objects around them. Before the file is
// relocated (CM2Shared::FinishLoading), a model listed in HdExtraLampsTable.inc gets one point light appended
// to its data, exactly like a light Blizzard would have authored: every native and HD light path then sees it.
// Returns true when the model data was replaced (data/size updated).
bool Patch(M2Data*& data, uint32_t& size, const char* fileName);

// Registers hdExtraLamps, hdExtraLampsIntensity and hdExtraLampsRange. Main thread only (HD::LightsMode calls it):
// models can load on another thread or before the console exists, so Patch never registers, it only reads them
// (defaults until they exist).
void RegisterCVars();

// hdExtraLampsObjects 0 (default): the added light is only lit on map decor (ADT and building doodads). On the
// models of world objects (game objects, creatures: event decorations among them) it stays off, their own Blizzard
// lights are untouched. 1 = on everywhere.
void MarkPatched(const void* shared);      // CM2Shared whose data Patch changed
void ForgetShared(const void* shared);     // CM2Shared destroyed
void SetObjectModel(const void* model, bool object); // CM2Model of a world object (CMapEntity), false when destroyed
// True when light `index` of `model` (of `shared`) is our added light on a world object and must stay off.
bool HideLight(const void* shared, const void* model, uint32_t index, uint32_t count);

// The added lamps are lit at night only, like the windows (DayNight's m_curve0: 1 at night, 0 by day, a fade at
// dusk and dawn). hdExtraLampsDay = how much of their light stays by day (default 0).
// HD 2026-11-05 lot J2: how lit the lamps are by the clock (dayProgression 0..1): 1 until 6:00, off at 7:00,
// starting at 21:30, full at 22:00. Lumenveil uses the same hours (lumen/source/fog_model.cpp LampHourWeight).
float NightAtDay(float dayProgression);
void SetNight(float night);                // DayNight::SetColors, each frame
// Light `index` of `shared` is our added light: its colour scale now (0..1). -1 = not ours (left to Blizzard).
float LightScale(const void* shared, uint32_t index, uint32_t count, const void* model = nullptr);

// HD J1 2026-11-05 (Max: lights off by day outdoors, on at night, always on indoors). The Blizzard lights of map
// decor (ADT and building doodads, CM2Model flag 0x8000) follow the night too, except a doodad of an interior building
// group (MAPOBJ_FLAG_INTERIOR, set by CMapObj::CreateRefs): hdLampsDay = how much stays by day (default 0, 1 = Blizzard).
void SetInteriorModel(const void* model, bool interior); // CMapObj::CreateRefs; false when the model is destroyed
// Colour scale (0..1) of the Blizzard lights of a map-decor model now; -1 = left to Blizzard (interior, or hdLampsDay 1).
float DoodadLightScale(const void* model);
}

#endif
