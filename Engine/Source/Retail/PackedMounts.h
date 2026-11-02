#pragma once
#include "Engine/Source/Retail/CascSource.h"
#include <map>

namespace Retail {
enum class PackedResult { Missing, Ready, Invalid };
// The game supplies its raw MPQ reader. Standalone tools remain independent of Storm.
// Null bytes requests only a bounded original size; never calls the Retail adapter.
using PackedReader = PackedResult (*)(const char*, std::vector<uint8_t>*, uint32_t&, std::string&);
void SetPackedReader(PackedReader reader);
PackedResult ReadPackedFile(const char* name, std::vector<uint8_t>* bytes, uint32_t& size, std::string& error);

struct PackedMount {
    uint32_t model = 0, textures[3] = {};
    float scale = 1.0f;
};
struct PackedMountRegistry {
    std::string product, build;
    uint32_t locale = 0;
    std::map<uint32_t, PackedMount> mounts;
};
bool ParsePackedMounts(const std::vector<uint8_t>& bytes, PackedMountRegistry& registry, std::string& error);
bool GetPackedMount(uint32_t display, PackedMount& mount);
CascSource& MountStudioSource();
}
