// SPDX-License-Identifier: GPL-3.0-or-later
// HD: Cata layout/formulas checked against wowdecomp15595-w15595, LiquidMaterial.cpp.
#include "CataWater.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace HD::CataWater {
namespace {
uint32_t Word(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
float Float(uint32_t bits) { float f; std::memcpy(&f, &bits, 4); return f; }
int32_t Int(uint32_t bits) { int32_t n; std::memcpy(&n, &bits, 4); return n; }
bool Finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool Fail(std::string& error, const char* message) { error = message; return false; }
Matrix RotationScale(float angle, float scale) {
    Matrix m = Identity();
    float c = std::cos(angle), s = std::sin(angle);
    m[0] = { c * scale, s * scale, 0, 0 };
    m[1] = { -s * scale, c * scale, 0, 0 };
    m[2].z = scale;
    return m;
}
void PutMatrix(std::array<Vec4, 53>& dest, size_t row, const Matrix& m) {
    std::copy(m.begin(), m.end(), dest.begin() + row);
}
void PutMatrix(std::array<Vec4, 32>& dest, size_t row, const Matrix& m) {
    std::copy(m.begin(), m.end(), dest.begin() + row);
}
}
Matrix Identity() { return {{{1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}}}; }
MaterialConstants::MaterialConstants() {
    for (size_t row = 14; row <= 46; row += 4) PutMatrix(vertex,row,Identity());
    for (size_t row = 5; row <= 13; row += 4) PutMatrix(pixel,row,Identity());
}
Vec4 Transform(Vec4 v, const Matrix& m) {
    return {v.x*m[0].x + v.y*m[1].x + v.z*m[2].x + v.w*m[3].x,
            v.x*m[0].y + v.y*m[1].y + v.z*m[2].y + v.w*m[3].y,
            v.x*m[0].z + v.y*m[1].z + v.z*m[2].z + v.w*m[3].z,
            v.x*m[0].w + v.y*m[1].w + v.z*m[2].w + v.w*m[3].w};
}
Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix m{};
    for (size_t i = 0; i < 4; ++i) m[i] = Transform(a[i], b);
    return m;
}
Matrix Reflection(float height) {
    Matrix m = Identity(); m[2].z = -1; m[3].z = height * 2; return m;
}
Matrix ReflectedView(const Matrix& mainView, float height) { return Multiply(Reflection(height), mainView); }
Vec3 ReflectPoint(Vec3 point, float height) { point.z = 2 * height - point.z; return point; }

bool LiquidTypes::ReadFile(const std::string& path, std::string& error) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return Fail(error, "cannot open LiquidType.dbc");
    auto size = f.tellg();
    if (size < 20 || size > 16 * 1024 * 1024) return Fail(error, "DBC size outside limits");
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        return Fail(error, "incomplete DBC read");
    return Read(bytes, error);
}
bool LiquidTypes::Read(const std::vector<uint8_t>& dbc, std::string& error) {
    if (dbc.size() < 20 || dbc.size() > 16 * 1024 * 1024 || std::memcmp(dbc.data(), "WDBC", 4))
        return Fail(error, "expected a bounded WDBC file");
    uint32_t count = Word(dbc.data() + 4), fields = Word(dbc.data() + 8);
    uint32_t stride = Word(dbc.data() + 12), strings = Word(dbc.data() + 16);
    if (fields != 45 || stride != 180 || count > 65536 || !strings)
        return Fail(error, "expected Cata LiquidType: 45 fields, 180 bytes per row");
    uint64_t stringStart = 20ull + uint64_t(count) * stride;
    if (stringStart + strings != dbc.size()) return Fail(error, "DBC lengths disagree");
    const char* text = reinterpret_cast<const char*>(dbc.data() + size_t(stringStart));
    if (text[0] != '\0') return Fail(error, "DBC string block must start with an empty string");
    auto readString = [&](uint32_t offset, std::string& out) {
        if (offset >= strings) return false;
        const char* end = static_cast<const char*>(std::memchr(text + offset, 0, strings - offset));
        if (!end) return false;
        out.assign(text + offset, end);
        return true;
    };
    std::map<uint32_t, LiquidType> candidate;
    for (uint32_t i = 0; i < count; ++i) {
        LiquidType row;
        const uint8_t* src = dbc.data() + 20 + size_t(i) * stride;
        for (size_t j = 0; j < row.words.size(); ++j) row.words[j] = Word(src + 4 * j);
        row.id = row.words[0]; row.material = row.words[14];
        if (!readString(row.words[1], row.name)) return Fail(error, "invalid liquid name offset");
        for (size_t j = 0; j < 6; ++j)
            if (!readString(row.words[15 + j], row.textures[j])) return Fail(error, "invalid texture offset");
        for (size_t j = 0; j < 18; ++j) {
            row.floats[j] = Float(row.words[23 + j]);
            if (!std::isfinite(row.floats[j])) return Fail(error, "nonfinite material parameter");
        }
        for (size_t j = 0; j < 4; ++j) row.ints[j] = Int(row.words[41 + j]);
        uint32_t id = row.id;
        if (!candidate.emplace(id, std::move(row)).second) return Fail(error, "duplicate liquid ID");
    }
    rows_.swap(candidate); error.clear(); return true;
}
const LiquidType* LiquidTypes::Find(uint32_t id) const {
    auto i = rows_.find(id); return i == rows_.end() ? nullptr : &i->second;
}
bool ValidateBytecode(const std::vector<uint8_t>& code, ShaderKind kind, std::string& error) {
    if (code.size() < 8 || code.size() > 1024 * 1024 || code.size() % 4)
        return Fail(error, "invalid shader byte count");
    uint32_t expected = kind == ShaderKind::Vertex ? 0xfffe0300u : 0xffff0300u;
    if (Word(code.data()) != expected || Word(code.data() + code.size() - 4) != 0x0000ffffu)
        return Fail(error, "expected SM3 bytecode for the requested stage, with END token");
    error.clear(); return true;
}
bool SelectPermutation(uint32_t detail, uint32_t depthMode, uint32_t dc, ReflectionKind reflection,
                       bool hardwareDepth, WaterPass pass, Permutation& out, std::string& error) {
    uint32_t r = static_cast<uint32_t>(reflection);
    if (detail > 3 || depthMode < 1 || depthMode > 3 || dc > 2 || r > 2)
        return Fail(error, "unsupported Cata water detail/depth/displacement/reflection");
    Permutation p;
    p.vertexName = detail ? "CataProcWater" : "CataWater";
    p.vertex = detail ? dc * 4 : 0; // Exact Cata DrawInstance index; no 3.3.5 lightCount added.
    if (!detail) p.pixelName = "CataWater";
    else if (detail == 1) p.pixelName = pass == WaterPass::Below ? "CataMediumWaterBelow" : "CataMediumWater";
    else {
        uint32_t d = depthMode - 1, base = 3 * d + dc;
        if (pass == WaterPass::AboveFog) { p.pixelName = "CataProcWaterAboveFog"; p.pixel = base; }
        else if (pass == WaterPass::Below) { p.pixelName = "CataProcWaterBelow"; p.pixel = base * 3 + r; }
        else { p.pixelName = "CataProcWaterAbove"; p.pixel = (hardwareDepth ? 1u : 0u) + 2 * (3 * base + r); }
    }
    out = p; error.clear(); return true;
}
bool SetMaterialConstants(MaterialConstants& out, const LiquidType& type, uint32_t timeMs, Vec3 camera,
                          float worldScale, float resolution, bool relative,
                          const std::vector<Displacement>& displacement, std::string& error) {
    if (!type.IsWater() || !Finite(camera) || !std::isfinite(worldScale) || worldScale <= 0 ||
        !std::isfinite(resolution) || resolution <= 0 || displacement.size() > 2)
        return Fail(error, "invalid water material inputs");
    for (float f : type.floats) if (!std::isfinite(f)) return Fail(error, "nonfinite material value");
    for (const auto& d : displacement) {
        if (!Finite(d.position) || !std::isfinite(d.sizeX) || !std::isfinite(d.sizeY) ||
            d.sizeX <= 0 || d.sizeY <= 0 || !d.texWidth || !d.texHeight ||
            !std::isfinite(d.interpolation) || d.interpolation < 0 || d.interpolation > 1)
            return Fail(error, "invalid displacement map");
    }
    MaterialConstants next = out;
    auto& v = next.vertex; auto& p = next.pixel; const auto& f = type.floats;
    float angle = f[5] * 0.017453292f;
    PutMatrix(v, 26, RotationScale(angle, worldScale * f[3]));
    PutMatrix(v, 30, RotationScale(angle, worldScale * f[4]));
    Matrix detail = Identity(); detail[1].y = worldScale * f[2];
    PutMatrix(v, 34, detail);
    Matrix scroll = Identity();
    // Exact GetTexScrollMtx(time, (0.015,0)): the source truncates the float period to uint32.
    const uint32_t period = static_cast<uint32_t>(static_cast<int64_t>(1000.0f / 0.015f));
    scroll[3].x = static_cast<float>(static_cast<double>(timeMs % period) / period);
    PutMatrix(v, 38, scroll);
    float wave = static_cast<float>(static_cast<double>(timeMs % 10000) * 0.000099999997);
    float amplitude = f[7] * 0.34999999f;
    p[17] = {camera.x, camera.y, camera.z, 1};
    p[23] = {f[13], f[14], f[15], f[6]};
    p[27] = {f[12], f[11], f[9], f[10]};
    v[50] = {wave * 3.1415927f, amplitude, resolution, f[8]};
    p[28] = {wave * 3.1415927f, amplitude, resolution, 1};
    v[51] = {}; v[52] = {}; p[29] = {}; p[30] = {}; p[31] = {};
    for (size_t i = 0; i < displacement.size(); ++i) {
        const auto& d = displacement[i];
        Vec3 pos = d.position;
        if (relative) { pos.x -= camera.x; pos.y -= camera.y; pos.z -= camera.z; }
        v[51 + i] = {pos.x, pos.y, 1 / d.sizeX, 1 / d.sizeY};
        float tx = 1.0f / static_cast<float>(d.texWidth), ty = 1.0f / static_cast<float>(d.texHeight);
        p[29 + i] = {tx, ty, tx * 0.5f, ty * 0.5f};
        if (!i) p[31].x = d.interpolation; else p[31].y = d.interpolation;
    }
    for (const auto& row : v) if (!std::isfinite(row.x) || !std::isfinite(row.y) || !std::isfinite(row.z) || !std::isfinite(row.w))
        return Fail(error, "overflow in vertex constants");
    for (const auto& row : p) if (!std::isfinite(row.x) || !std::isfinite(row.y) || !std::isfinite(row.z) || !std::isfinite(row.w))
        return Fail(error, "overflow in pixel constants");
    out = next; error.clear(); return true;
}
void SetTransformConstants(MaterialConstants& out, Matrix model, const Matrix& view,
                           const Matrix& projection, Vec3 camera, bool relative) {
    if (relative) { model[3].x -= camera.x; model[3].y -= camera.y; model[3].z -= camera.z; }
    PutMatrix(out.vertex, 14, model);
    PutMatrix(out.vertex, 18, Multiply(model, view));
    PutMatrix(out.vertex, 22, projection);
    PutMatrix(out.pixel, 5, view);
    PutMatrix(out.pixel, 9, projection);
    PutMatrix(out.pixel, 13, Multiply(view, projection));
}
bool SetScreenConstants(MaterialConstants& out, const ScreenSettings& s, std::string& error) {
    if (!s.width || !s.height || s.width > 16384 || s.height > 16384 || !std::isfinite(s.nearClip) ||
        !std::isfinite(s.farClip) || !std::isfinite(s.viewportDepthMax) || s.nearClip <= 0 ||
        s.farClip <= s.nearClip || s.viewportDepthMax <= 0 || s.viewportDepthMax > 1 ||
        !std::isfinite(s.minX) || !std::isfinite(s.minY) || !std::isfinite(s.maxX) || !std::isfinite(s.maxY) ||
        s.minX < 0 || s.minY < 0 || s.maxX > 1 || s.maxY > 1 || s.minX >= s.maxX || s.minY >= s.maxY)
        return Fail(error, "invalid Cata screen/depth parameters");
    float halfX = 0.5f / static_cast<float>(s.width), halfY = 0.5f / static_cast<float>(s.height);
    Vec4 params{s.nearClip, s.farClip, (1 / s.farClip - 1 / s.nearClip) / s.viewportDepthMax, 1 / s.nearClip};
    if (!std::isfinite(params.z) || !std::isfinite(params.w)) return Fail(error, "depth parameters overflow");
    float x = ((s.minY + 1) - (1 - s.maxY)) * .5f;
    float scaleY = (s.maxX - s.minX) * (s.texelCenterOnEdge ? .5f : -.5f);
    float y = (s.texelCenterOnEdge ? (s.minX + 1) - (1 - s.maxX) : ((1 - s.maxX) + 1) - s.minX) * .5f;
    if (s.pixelCenterOnEdge) { x += halfX; y += halfY; }
    out.pixel[22] = params;
    out.pixel[24] = {static_cast<float>(s.width),static_cast<float>(s.height),halfX*2,halfY*2};
    out.pixel[25] = {halfX,halfY,1/halfX,1/halfY};
    out.pixel[26] = {(s.maxY-s.minY)*.5f,scaleY,x,y};
    error.clear(); return true;
}
bool RippleClock::Reset(uint32_t time) {
    if (period_ != 25 && period_ != 50) return false;
    last_ = time - period_; initialized_ = true; return true;
}
bool RippleClock::Tick(uint32_t time, RippleStep& out) {
    if (!initialized_) return false;
    uint32_t elapsed = time - last_, steps = elapsed / period_;
    out.interpolation = static_cast<float>(elapsed % period_) / static_cast<float>(period_);
    if (!steps) { out.count = 0; return true; }
    out.count = std::min(steps, 2u);
    if (steps >= 2) last_ = time; else last_ += steps * period_;
    return true;
}
const LiquidType* ResolveType(const LiquidTypes& types, uint32_t id, const std::map<uint32_t, uint32_t>& map) {
    auto i = map.find(id);
    const LiquidType* row = types.Find(i == map.end() ? id : i->second);
    return row && row->IsWater() ? row : nullptr;
}
PlaneChoice ChoosePlane(const std::vector<WaterBody>& bodies, const LiquidTypes& types,
                        const std::map<uint32_t, uint32_t>& map, Vec3 eye, float tolerance, float bias) {
    PlaneChoice out;
    if (!Finite(eye) || !std::isfinite(tolerance) || tolerance < 0 || !std::isfinite(bias) || bias < 0) return out;
    double nearest = std::numeric_limits<double>::max();
    for (const auto& body : bodies) {
        if (!Finite(body.low) || !Finite(body.high) || body.low.x > body.high.x || body.low.y > body.high.y ||
            body.low.z > body.high.z || body.high.z - body.low.z > tolerance || eye.z <= body.high.z + bias ||
            !ResolveType(types, body.liquidId, map)) continue;
        double dx = std::max({double(body.low.x) - eye.x, double(eye.x) - body.high.x, 0.0});
        double dy = std::max({double(body.low.y) - eye.y, double(eye.y) - body.high.y, 0.0});
        double dz = double(eye.z) - body.high.z;
        double distance = dx * dx + dy * dy + dz * dz;
        if (distance > nearest || (out.valid && distance == nearest && body.id >= out.bodyId)) continue;
        nearest = distance; out.valid = true; out.bodyId = body.id;
        out.height = body.low.z * 0.5f + body.high.z * 0.5f;
    }
    if (out.valid) {
        out.mirrorEye = ReflectPoint(eye, out.height);
        out.worldClip = {0, 0, 1, -out.height - bias};
        if (!Finite(out.mirrorEye) || !std::isfinite(out.worldClip.w)) return {};
    }
    return out;
}
bool SameFrame(const FrameKey& a, const FrameKey& b) {
    return a.serial && a.width && a.height && a.serial == b.serial && a.resizeGeneration == b.resizeGeneration &&
        a.viewId == b.viewId && a.width == b.width && a.height == b.height;
}
bool CanDraw(const PreparedInputs& in, const FrameKey& now, std::string& reason) {
    if (!SameFrame(in.frame, now)) return Fail(reason, "inputs belong to another frame/view/extent/resize");
    if (!in.capturedBeforeLiquids) return Fail(reason, "scene copy was not captured before water");
    if (!in.color || !in.depth) return Fail(reason, "scene color/depth missing");
    if (!in.reflection || !in.blurredReflection) return Fail(reason, "reflection inputs missing");
    if (!in.depthEncodingMatchesPermutation) return Fail(reason, "depth encoding not matched to Cata permutation");
    if (!in.descriptorsReady || !in.shadersKnown) return Fail(reason, "shader/descriptor setup incomplete");
    reason.clear(); return true;
}
}
