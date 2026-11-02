#pragma once
namespace Liquid { class CMaterialSettings; }
class CAaSphere;
namespace HD::ForeverWater {
bool Requested();
// Appends a line to Logs\HdForeverWater.log (first 80 lines per run only; diagnostic).
 void Trace(const char* format, ...);
// Sets hdForeverWaterInfo (for callers that decide not to bind).
void Status(const char* message);
// Called only after the stock procedural-water geometry/constants/textures are set,
// inside its render-state scope. False leaves that stock draw entirely unchanged.
bool Bind(Liquid::CMaterialSettings* settings, const CAaSphere* bounds, bool lake = false);
void Shutdown();
}
