// SPDX-License-Identifier: GPL-3.0-or-later
// HD: preparation for the Cata water port. No GPU work or engine hooks in this library.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace HD::CataWater {
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Vec4 { float x = 0, y = 0, z = 0, w = 0; };
using Matrix = std::array<Vec4, 4>; // row-vector convention: clip = position * matrix
Matrix Identity();
Matrix Multiply(const Matrix& a, const Matrix& b);
Vec4 Transform(Vec4 v, const Matrix& m);
Matrix Reflection(float height); // world -> mirrored world, z' = 2 * height - z
Matrix ReflectedView(const Matrix& mainView, float height);
Vec3 ReflectPoint(Vec3 point, float height);

// DBC row: exact 45-word/180-byte Cata layout; never cast this C++ object onto a file.
struct LiquidType {
    uint32_t id = 0, material = 0;
    std::array<uint32_t, 45> words{}; // original bits, including unused fields
    std::string name;
    std::array<std::string, 6> textures;
    std::array<float, 18> floats{};
    std::array<int32_t, 4> ints{};
    bool IsWater() const { return material == 1 || material == 3; }
};
class LiquidTypes {
public:
    // Transactional: on failure, prior data remains usable. Bounded parsing and string offsets.
    bool Read(const std::vector<uint8_t>& dbc, std::string& error);
    bool ReadFile(const std::string& path, std::string& error);
    const LiquidType* Find(uint32_t id) const;
    size_t Size() const { return rows_.size(); }
private:
    std::map<uint32_t, LiquidType> rows_;
};
enum class ShaderKind { Vertex, Pixel };
bool ValidateBytecode(const std::vector<uint8_t>& code, ShaderKind kind, std::string& error);

// Exact permutation arithmetic from Cata LiquidMaterial.cpp. depthMode is its 1..3,
// NOT wxl's raw depth format. The integration must establish which encoding it supplies.
enum class ReflectionKind : uint32_t { None = 0, Real = 1, Fake = 2 };
enum class WaterPass { Above, Below, AboveFog };
struct Permutation {
    uint32_t vertex = 0, pixel = 0;
    const char* vertexName = nullptr;
    const char* pixelName = nullptr;
};
bool SelectPermutation(uint32_t detail, uint32_t depthMode, uint32_t displacementCount,
                       ReflectionKind reflection, bool hardwareDepth, WaterPass pass,
                       Permutation& out, std::string& error);

struct Displacement {
    Vec3 position;
    float sizeX = 0, sizeY = 0;
    uint32_t texWidth = 0, texHeight = 0;
    float interpolation = 0;
};
struct MaterialConstants {
    std::array<Vec4, 53> vertex{};
    std::array<Vec4, 32> pixel{};
    MaterialConstants(); // Cata's initial identity transform/texture matrices, other rows zero
};
// Writes only Cata's material-owned ranges VS26..41,VS50..52,PS17,PS23,PS27..31.
// Lighting, transforms, screen/depth reconstruction and fog MUST be supplied separately.
// The source's detail matrix changes b1 only; it is intentionally not a uniform scale.
bool SetMaterialConstants(MaterialConstants& out, const LiquidType& type, uint32_t timeMs,
                          Vec3 camera, float worldCoordScale, float surfaceResolution,
                          bool cameraRelative, const std::vector<Displacement>& displacement,
                          std::string& error);
void SetTransformConstants(MaterialConstants& out, Matrix model, const Matrix& view,
                           const Matrix& nativeProjection, Vec3 camera, bool cameraRelative);
struct ScreenSettings {
    uint32_t width = 0, height = 0;
    float nearClip = 0, farClip = 0, viewportDepthMax = 0;
    // Cata ViewSettings::SetViewExtents: normalized viewport rectangle, typically 0..1.
    float minX = 0, minY = 0, maxX = 1, maxY = 1;
    bool texelCenterOnEdge = false, pixelCenterOnEdge = false;
};
// Exact ViewSettings::CalculateParams, CalculateHomToScreen and Liquid::SetScreenConstants.
// Assumes viewportDepthMin == 0, like Cata. Preserve raw depth with depthMode DM_Texture (3),
// hardwareDepth=true for R32F (the false variant is Cata's ATI D24 encoding, enum 23).
bool SetScreenConstants(MaterialConstants& out, const ScreenSettings& screen, std::string& error);
struct RippleStep { uint32_t count = 0; float interpolation = 0; };
// Exact CPU tick policy from LiquidProcTextures::UpdateDisplMap: 25/50 ms, at most two
// propagation draws per update. Unsigned subtraction intentionally handles the 32-bit time wrap.
class RippleClock {
public:
    explicit RippleClock(uint32_t periodMs) : period_(periodMs) {}
    bool Reset(uint32_t timeMs);
    bool Tick(uint32_t timeMs, RippleStep& out);
private:
    uint32_t period_ = 0, last_ = 0;
    bool initialized_ = false;
};

// Map IDs explicitly when a mod uses a different LiquidType numbering. No guess or water fallback.
const LiquidType* ResolveType(const LiquidTypes& types, uint32_t clientLiquidId,
                             const std::map<uint32_t, uint32_t>& explicitMap);

// One COMPLETE body's bounds, aggregated by the caller before frustum culling. Do not infer flatness
// from a centre, a single chunk, or an animated wave height. Planar water only; no waterfalls.
struct WaterBody {
    uint64_t id = 0;
    uint32_t liquidId = 0;
    Vec3 low, high;
};
struct PlaneChoice {
    bool valid = false;
    uint64_t bodyId = 0;
    float height = 0;
    Vec3 mirrorEye;
    Vec4 worldClip; // keep dot(worldPosition, worldClip) >= 0 BEFORE the reflection transform
    bool reverseWinding = true;
};
PlaneChoice ChoosePlane(const std::vector<WaterBody>& bodies, const LiquidTypes& types,
                        const std::map<uint32_t, uint32_t>& idMap, Vec3 eye,
                        float flatTolerance = 0.05f, float clipBias = 0.02f);

// A copy is usable only in its own frame, view, extent and resize generation.
// This does NOT allocate images, insert barriers, or implement independent scene culling.
struct FrameKey {
    uint64_t serial = 0, resizeGeneration = 0, viewId = 0;
    uint32_t width = 0, height = 0;
};
bool SameFrame(const FrameKey& a, const FrameKey& b);
struct PreparedInputs {
    FrameKey frame;
    bool capturedBeforeLiquids = false;
    bool color = false, depth = false, reflection = false, blurredReflection = false;
    bool depthEncodingMatchesPermutation = false;
    bool descriptorsReady = false, shadersKnown = false;
};
bool CanDraw(const PreparedInputs& in, const FrameKey& current, std::string& reason);

// Cata CMaterialWater::SetupRender sampler semantics; source settings numbers are not 3.3.5 slots.
enum class SampleInput { SceneColor, Reflection, DepthRamp, BlurredReflection,
                         Displacement0, SurfaceNormal, SceneDepth, Foam, Displacement1 };
constexpr std::array<SampleInput, 9> Samplers = { SampleInput::SceneColor, SampleInput::Reflection,
    SampleInput::DepthRamp, SampleInput::BlurredReflection, SampleInput::Displacement0,
    SampleInput::SurfaceNormal, SampleInput::SceneDepth, SampleInput::Foam, SampleInput::Displacement1 };
}
