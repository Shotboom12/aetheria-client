#pragma once
#include "LvSuite.h"
namespace HD::Lumen::Client {
bool Startup(const WXL_VkRenderApi*,const char* cataloguePath,const char* regionsText="");
// Submit a typed snapshot on the scene thread, before opaque rendering. Does not modify native settings.
bool Prepare(NativeFrame,VkExtent2D actualRenderExtent,VkRect2D worldViewport,uint64_t completed);
bool Shutdown(uint64_t completed);
bool NeedsBeforeSpells();bool NeedsAfterSpells();bool ComposedFog(uint64_t serial);bool ComposedShafts(uint64_t serial);
const char* Status();void Stop();uint64_t LastUsed();bool SafeAdvancedSetting(const char*);
// HD 2026-11-02 lot L2: the loaded lamp catalogue (read only, scene/render thread), nullptr before Startup.
const Source::LampCatalog* Catalogue();
// Why the last Startup failed (empty when it did not). Aetheria diagnostics.
const char* FailureReason();
}
