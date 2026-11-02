#pragma once
#include <cstdint>
class CGxTex;

namespace HD::ForeverWater {
// Render-thread only. Allowed IDs come from the pinned, generated profile table.
// A missing/invalid texture returns null; never supplies a fabricated replacement.
CGxTex* OriginalTexture(uint32_t fileDataId);
void ReleaseOriginalTextures();
}
