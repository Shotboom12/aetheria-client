#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <sstream>
#include <set>
#include <cstring>

namespace {
uint32_t Id(const char* argument) {
    const std::string s = argument;
    uint32_t value = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), value);
    if (!value || result.ec != std::errc() || result.ptr != s.data() + s.size()) throw std::runtime_error("Invalid FileDataID");
    return value;
}
}

int main(int argc, char** argv) {
    if (argc != 5 && argc != 6) {
        std::fprintf(stderr, "Usage: RetailAssetAcquire <config> <modelID> <textureID> <new-output-directory> [--raw]\n");
        return 2;
    }
    const bool raw = argc == 6 && std::string(argv[5]) == "--raw";
    if (argc == 6 && !raw) return 2;
    try {
        const auto modelId = Id(argv[2]);
        std::vector<uint32_t> textureIds;
        std::istringstream ids(argv[3]); std::string id;
        while (std::getline(ids, id, ',')) textureIds.push_back(Id(id.c_str()));
        if (textureIds.empty()) throw std::runtime_error("At least one display texture is required");
        const std::filesystem::path output(argv[4]);
        if (std::filesystem::exists(output)) throw std::runtime_error("Acquisition output must be a new directory");
        Retail::CascSource source;
        std::string error;
        if (!source.Configure(argv[1], error)) throw std::runtime_error(error);
        std::map<uint32_t, std::vector<uint8_t>> originals;
        auto fetch = [&](uint32_t id, const char* extension) -> const std::vector<uint8_t>& {
            auto found = originals.find(id);
            if (found != originals.end()) return found->second;
            std::vector<uint8_t> bytes;
            if (!source.Inspect(Retail::FileName(id, extension).c_str(), bytes, error)) throw std::runtime_error(error);
            return originals.emplace(id, std::move(bytes)).first->second;
        };
        const auto& model = fetch(modelId, ".m2");
        if (raw) {
            // Original CDN bytes only: no runtime preparation, so split skeletons and
            // Retail-only ribbon layouts can be handed to the offline converter.
            Retail::ModelFiles files;
            if (!Retail::InspectModel(model, files, error)) throw std::runtime_error(error);
            // Match the runtime's SKPD inheritance and AFID dependency traversal.
            // A Centaur's animation files are declared by its split skeleton.
            std::vector<uint32_t> skeletons;
            std::set<uint32_t> inspected;
            if (files.skeleton) skeletons.push_back(files.skeleton);
            for (size_t index = 0; index < skeletons.size(); ++index) {
                if (inspected.size() >= 8) throw std::runtime_error("Skeleton dependency depth exceeds limit");
                if (!inspected.insert(skeletons[index]).second) continue;
                const auto& bytes = fetch(skeletons[index], ".skel");
                const auto word = [&](size_t at) { uint32_t value; std::memcpy(&value, bytes.data() + at, 4); return value; };
                for (size_t p = 0; p < bytes.size();) {
                    if (bytes.size() - p < 8 || word(p + 4) > bytes.size() - p - 8)
                        throw std::runtime_error("Invalid skeleton chunk bounds");
                    const uint32_t tag = word(p), size = word(p + 4);
                    p += 8;
                    if (tag == 0x44504b53 && size >= 12 && word(p + 8)) skeletons.push_back(word(p + 8));
                    if (tag == 0x44494641) {
                        if (size % 8) throw std::runtime_error("Invalid skeleton animation table");
                        for (size_t row = p; row < p + size; row += 8)
                            if (word(row + 4)) fetch(word(row + 4), ".anim");
                    }
                    p += size;
                }
            }
            for (auto skin : files.skins) if (skin) fetch(skin, ".skin");
            for (const auto& anim : files.animations) if (anim.fileId) fetch(anim.fileId, ".anim");
            for (auto texture : files.textures) if (texture) fetch(texture, ".blp");
            for (auto texture : textureIds) fetch(texture, ".blp");
        } else {
            Retail::ModelFiles graph;
            std::vector<uint8_t> prepared;
            if (!Retail::PrepareModel(model, prepared, graph, error)) throw std::runtime_error(error);
            // Validate temporary copies. Persist only the original decoded source bytes.
            const size_t activeSkins = std::min<size_t>(graph.skins.size(), std::min<size_t>(4, prepared[0x44]));
            for (size_t i = 0; i < activeSkins; ++i) {
                auto copy = fetch(graph.skins[i], ".skin");
                if (!Retail::PrepareSkin(copy, prepared, error)) throw std::runtime_error(error);
            }
            for (const auto& anim : graph.animations) {
                if (!anim.fileId) continue;
                auto copy = fetch(anim.fileId, ".anim"); // validate a copy; the stored original keeps its AFM2 chunk header
                if (!Retail::StripAnimationChunk(copy, error) || !Retail::ValidateAnimation(copy, prepared, anim.id, anim.variation, error))
                    throw std::runtime_error(error);
            }
            graph.textures.insert(graph.textures.end(), textureIds.begin(), textureIds.end());
            for (auto texture : graph.textures) if (texture) {
                const auto& bytes = fetch(texture, ".blp");
                if (bytes.size() < 148 || std::string(reinterpret_cast<const char*>(bytes.data()), 8) != std::string("BLP2\1\0\0\0", 8))
                    throw std::runtime_error("Unsupported texture layout");
            }
        }
        std::filesystem::create_directories(output);
        for (const auto& file : originals) {
            std::ofstream stream(output / (std::to_string(file.first) + ".bin"), std::ios::binary);
            if (!stream.write(reinterpret_cast<const char*>(file.second.data()), file.second.size()))
                throw std::runtime_error("Cannot write original asset");
        }
        std::printf("Acquired %zu original files; model %u; no converted assets written.\n", originals.size(), modelId);
        return 0;
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "%s\n", exception.what());
        return 1;
    }
}
