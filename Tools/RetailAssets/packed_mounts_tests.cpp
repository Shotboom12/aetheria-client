#include "Engine/Source/Retail/PackedMounts.h"
#include "Engine/Source/Retail/RetailModel.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
void Check(bool pass, const char* message) {
    if (!pass) { std::fprintf(stderr, "FAILED: %s\n", message); std::exit(1); }
}
std::vector<uint8_t> Bytes(const std::string& text) { return {text.begin(), text.end()}; }
std::filesystem::path fixture;
Retail::PackedResult Reader(const char* name, std::vector<uint8_t>* bytes, uint32_t& size, std::string& error) {
    std::string relative(name);
    for (auto& c : relative) if (c == '\\') c = '/';
    std::ifstream input(fixture / relative, std::ios::binary | std::ios::ate);
    if (!input) return Retail::PackedResult::Missing;
    const auto length = input.tellg();
    if (length <= 0 || length > 128 * 1024 * 1024) { error = "Invalid fixture size"; return Retail::PackedResult::Invalid; }
    size = static_cast<uint32_t>(length);
    if (bytes) {
        bytes->resize(size); input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes->data()), size)) return Retail::PackedResult::Invalid;
    }
    return Retail::PackedResult::Ready;
}
}
int main(int argc, char** argv) {
    const std::string row = "25836;wow;dcfc90fffd79ba00406ae46f5f657592;2;234564;234596;0;0;1.0\n";
    Retail::PackedMountRegistry registry;
    std::string error;
    Check(Retail::ParsePackedMounts(Bytes("# registry\r\n" + row), registry, error) && registry.mounts.size() == 1,
        "valid registry with optional zero textures");
    Check(!Retail::ParsePackedMounts(Bytes(row + row), registry, error) && registry.mounts.empty(), "duplicate rows rejected atomically");
    auto other = row; other.replace(0, 5, "25837"); other.replace(other.find("dcfc"), 4, "abcd");
    Check(!Retail::ParsePackedMounts(Bytes(row + other), registry, error), "mixed builds rejected");
    for (const char* bad : {"nan", "inf", "0", "-1", "101", "1junk"}) {
        auto invalid = row; invalid.replace(invalid.find("1.0"), 3, bad);
        Check(!Retail::ParsePackedMounts(Bytes(invalid), registry, error), "invalid scale rejected");
    }
    Check(Retail::IsCdnPath("MOUNTSTUDIO\\234564.m2") && !Retail::IsCdnPath("Creature\\Drake.m2"), "isolated namespace routing");
    {
        Retail::SourceScope scope(Retail::SourceKind::MountStudio);
        Check(Retail::FileName(123, ".blp") == "mountstudio/123.blp", "dependency namespace retained");
    }
    Check(Retail::FileName(123, ".blp") == "retail/123.blp", "primary namespace restored");
    Retail::CascSource primary;
    Check(!primary.ConfigurePacked("wow", "dcfc90fffd79ba00406ae46f5f657592", 2, error), "primary source cannot be repinned by a pack");
    if (argc == 2) {
        fixture = argv[1];
        Retail::SetPackedReader(Reader);
        std::vector<uint8_t> csv;
        uint32_t size = 0;
        Check(Reader("EsteriaMounts\\mounts.csv", &csv, size, error) == Retail::PackedResult::Ready, "read extracted original registry");
        Check(Retail::ParsePackedMounts(csv, registry, error), error.c_str());
        Check(registry.mounts.size() == 55, "exactly 55 validated mounts");
        auto& source = Retail::MountStudioSource();
        Check(source.Configured(), "dedicated packed source configured");
        std::vector<uint8_t> original, prepared, dependency;
        for (const auto& entry : registry.mounts) {
            std::printf("Checking display %u model %u\n", entry.first, entry.second.model);
            std::fflush(stdout);
            Retail::PackedMount selected;
            Check(Retail::GetPackedMount(entry.first, selected) && selected.model == entry.second.model, "display mapping");
            const auto model = "mountstudio/" + std::to_string(selected.model) + ".m2";
            Check(&Retail::SourceForPath(model.c_str()) == &source, "source dispatch");
            Check(source.Exists(model.c_str(), error) && source.CachedOriginalSize(model.c_str(), size), "packed locality/size");
            Check(source.Inspect(model.c_str(), original, error), error.c_str());
            Retail::ModelFiles files;
            Check(Retail::InspectModel(original, files, error), error.c_str());
            Check(source.Read(model.c_str(), prepared, error), error.c_str());
            Check(prepared.size() > 0x47, "prepared model header bounds");
            const uint32_t profiles = uint32_t(prepared[0x44]) | uint32_t(prepared[0x45]) << 8 |
                uint32_t(prepared[0x46]) << 16 | uint32_t(prepared[0x47]) << 24;
            Check(profiles && profiles <= 4 && profiles <= files.skins.size(), "advertised runtime profile count");
            // SFID also lists Retail LOD originals. Those stay preserved, but the
            // Wrath loader registers only the model header's advertised profiles.
            for (size_t i = 0; i < profiles; ++i) {
                char suffix[24]; std::snprintf(suffix, sizeof(suffix), "%02u.skin", unsigned(i));
                const auto skin = "mountstudio/" + std::to_string(selected.model) + suffix;
                Check(source.Read(skin.c_str(), dependency, error), error.c_str());
            }
            for (uint32_t texture : selected.textures) if (texture) {
                const auto path = "mountstudio/" + std::to_string(texture) + ".blp";
                Check(source.Read(path.c_str(), dependency, error), error.c_str());
            }
            for (uint32_t texture : files.textures) if (texture) {
                const auto path = "mountstudio/" + std::to_string(texture) + ".blp";
                Check(source.Read(path.c_str(), dependency, error), error.c_str());
            }
            for (const auto& animation : files.animations) if (animation.fileId) {
                char suffix[32]; std::snprintf(suffix, sizeof(suffix), "%04u-%02u.anim", animation.id, animation.variation);
                const auto path = "mountstudio/" + std::to_string(selected.model) + suffix;
                Check(source.Read(path.c_str(), dependency, error), error.c_str());
            }
            source.ReleaseModel(model.c_str());
        }
        Check(source.Stats().acquired == 0, "zero network acquisitions");
        Check(!source.Exists("mountstudio/4294967295.blp", error), "missing packed files fail without CDN fallback");
        std::puts("55 packed mount models, skins, selected textures and external animations passed offline.");
    } else Check(argc == 1, "usage: PackedMountTests [extracted-pack]");
    std::puts("Packed mount routing and registry checks passed.");
}
