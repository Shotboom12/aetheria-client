#ifndef ENGINE_SOURCE_RETAIL_CASCSOURCE_H
#define ENGINE_SOURCE_RETAIL_CASCSOURCE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "Engine/Source/Retail/SourceContext.h"

namespace Retail {
struct ModelFiles;

struct HandItemAppearance {
    uint32_t modelId = 0;
    uint32_t textureId = 0; // Replaceable object-skin texture (type 2).
    bool offHandHalfTurn = false; // Rotate around local Z for the downward, outward-facing left-hand grip.
};

struct CreatureAppearance {
    uint32_t fourthTextureId = 0; // Retail's fourth creature skin binds texture type 5.
    uint32_t dissolveProfile = 0;
    uint32_t dissolveTextures[3] = {};
};

// One source per pinned build. Storage calls and dependency publication are serialized.
// Each source owns its retail/ or forever/ namespace plus explicit aliases; stock paths stay in MPQ.
class CascSource {
public:
    explicit CascSource(SourceKind kind = SourceKind::Retail);
    ~CascSource();
    CascSource(const CascSource&) = delete;
    CascSource& operator=(const CascSource&) = delete;

    bool Configure(const char* configPath, std::string& error);
    // Dedicated offline MPQ namespace; cannot replace/reconfigure the live Retail source.
    bool ConfigurePacked(const std::string& product, const std::string& build, uint32_t locale, std::string& error);
    bool Configured();
    // A second retail build layered under this one. Reads this source cannot satisfy are retried
    // against it, and character-model registration is mirrored to it. Used for identities that
    // exist only in the older "Forever" client (Skyborne). Null keeps this source standalone.
    void AttachSecondary(CascSource& secondary);
    bool Handles(const char* name);
    bool Exists(const char* name, std::string& error);
    bool Read(const char* name, std::vector<uint8_t>& bytes, std::string& error);
    bool Inspect(const char* name, std::vector<uint8_t>& bytes, std::string& error);
    // Explicit player body/collection scope for legacy material approximation. Generic models
    // keep strict unsupported-shader rejection unless their existing shared-skeleton path applies.
    void RegisterCharacterModel(uint32_t fileDataId);
    // Like Inspect, but encrypted blocks without a key are zero-filled instead of failing.
    bool ReadTable(const char* name, std::vector<uint8_t>& bytes, std::string& error);
    void ReleaseModel(const char* name);
    bool CachedSize(const char* name, uint32_t& size);
    // Locality/progress checks may use durable original size before client preparation.
    // CachedSize above remains the exact prepared size for SFile reads.
    bool CachedOriginalSize(const char* name, uint32_t& size);
    bool HandItem(uint32_t displayId, HandItemAppearance& appearance, bool mainHand = false);
    bool SetHandItem(uint32_t displayId, HandItemAppearance appearance);
    bool SetMainHandPreview(HandItemAppearance appearance);
    bool CreatureSkin(uint32_t displayId, CreatureAppearance& appearance);
    bool HasClientTable(const char* name);
    bool MergeTable(const char* name, const std::vector<uint8_t>& base,
        std::vector<uint8_t>& merged, std::string& error);
    struct Statistics { uint64_t reused = 0, acquired = 0; };
    Statistics Stats();

private:
    // Own-storage attempts, without the secondary-build retry the public methods add.
    bool ReadOwn(const char* name, std::vector<uint8_t>& bytes, std::string& error);
    bool InspectOwn(const char* name, std::vector<uint8_t>& bytes, std::string& error);
    bool ExistsOwn(const char* name, std::string& error);
    bool CachedSizeOwn(const char* name, uint32_t& size);
    bool DescribeLoadedModelOwn(const char* name, ModelFiles& files);
    bool WarmModelOwn(const char* name, std::string& error);
    bool ReleaseModelOwn(const char* name);
    CascSource* Secondary();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Lazy process-wide source, configured by WOW_RETAIL_CONFIG or RetailCasc.ini in the
// working directory. Initialization does no network IO until a Retail reference is read.
CascSource& Source();
CascSource& SourceForPath(const char* name);

// Optional second build, configured only by WOW_RETAIL_CONFIG_SECONDARY. Left unconfigured
// (and inert) when that variable is absent, so the accepted primary path is unchanged.
CascSource& SecondarySource();

}
#endif
