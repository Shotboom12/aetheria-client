// Generated Vulkan compute adaptation of Lumenveil 0.1.1. Original equations retained.
[[vk::binding(0,0)]] cbuffer LvUniforms {float4 LvConstants[128];};
[[vk::binding(1,0)]] SamplerState LvPoint;
[[vk::binding(2,0)]] SamplerState LvLinear;
[[vk::binding(3,0)]] SamplerState LvWrap;
[[vk::binding(20,0)]] RWTexture2D<float4> LvOutput;
#define cWorldViewportPx LvConstants[0]
#define cPixelGrid LvConstants[1]
#define cViewToNdc LvConstants[2]
#define cDepthToViewZ LvConstants[3]
#define cViewToWorld float4x4(LvConstants[4],LvConstants[5],LvConstants[6],LvConstants[7])
#define cLowResTarget LvConstants[8]

static const float kSkyMinDepth = 0.99903;

float2 ViewportOrigin()
{
    return cWorldViewportPx.xy;
}

float2 ViewportSize()
{
    return cWorldViewportPx.zw;
}

float2 ViewportLastPixelCentre()
{
    return ViewportOrigin() + ViewportSize() - 0.5;
}

bool OutsideViewport(float2 pixel)
{
    return any(pixel < ViewportOrigin()) || any(pixel > ViewportOrigin() + ViewportSize());
}

float FullPixelsPerLowResTexel()
{
    return cPixelGrid.x;
}

float FrameIndex()
{
    return cPixelGrid.y;
}

float2 DepthTexelSize()
{
    return cPixelGrid.zw;
}

float2 ViewToNdcScale()
{
    return cViewToNdc.xy;
}

float2 ViewToNdcOffset()
{
    return cViewToNdc.zw;
}

float DepthAtInfiniteViewZ()
{
    return cDepthToViewZ.x;
}

float DepthPerInverseViewZ()
{
    return cDepthToViewZ.y;
}

float MaxFogDistance()
{
    return cDepthToViewZ.z;
}

float DeepestWorldDepth()
{
    return cDepthToViewZ.w;
}

float3 CameraPositionWorld()
{
    return cViewToWorld[3].xyz;
}

float3 ViewToWorldDirection(float3 viewDirection)
{
    return mul(viewDirection, (float3x3)cViewToWorld);
}

float2 LowResSize()
{
    return cLowResTarget.xy;
}

float2 LowResTexelSize()
{
    return cLowResTarget.zw;
}

float SampleDepth(Texture2D<float4> depthSampler, float2 pixel)
{
    return depthSampler.SampleLevel(LvPoint,(float4(pixel * DepthTexelSize(), 0, 0)).xy,(float4(pixel * DepthTexelSize(), 0, 0)).w).r;
}

bool BeyondFarClip(float depth)
{
    return depth > DeepestWorldDepth();
}

bool IsSky(float depth)
{
    return depth >= max(DeepestWorldDepth(), kSkyMinDepth);
}

float LinearDepth(float depth)
{
    return BeyondFarClip(depth) ? MaxFogDistance()
                                : min(DepthPerInverseViewZ() / (depth - DepthAtInfiniteViewZ()), MaxFogDistance());
}

static const float kFirstEdgeOccluderViewZ = 8;
static const float kEdgeOccluderViewZRatio = 2.5;

float4 EdgeOccluderViewZs()
{
    return kFirstEdgeOccluderViewZ * float4(1, kEdgeOccluderViewZRatio, kEdgeOccluderViewZRatio * kEdgeOccluderViewZRatio,
                                            kEdgeOccluderViewZRatio * kEdgeOccluderViewZRatio * kEdgeOccluderViewZRatio);
}

float EdgeOccluderLevel(float viewZ)
{
    return log2(viewZ / kFirstEdgeOccluderViewZ) / log2(kEdgeOccluderViewZRatio);
}

float2 LowResTexelToFullPixel(float2 lowResTexel)
{
    float2 pixel = ViewportOrigin() + lowResTexel * FullPixelsPerLowResTexel() +
                   floor(FullPixelsPerLowResTexel() * 0.5) + 0.5;
    return min(pixel, ViewportLastPixelCentre());
}

float2 FullPixelToLowResTexel(float2 pixel)
{
    return (pixel - ViewportOrigin() - floor(FullPixelsPerLowResTexel() * 0.5) - 0.5) / FullPixelsPerLowResTexel();
}

float2 LowResTexelToUv(float2 lowResTexel)
{
    return (lowResTexel + 0.5) * LowResTexelSize();
}

float2 PixelToNdc(float2 pixel)
{
    return float2((pixel.x - ViewportOrigin().x) / ViewportSize().x * 2 - 1,
                  1 - (pixel.y - ViewportOrigin().y) / ViewportSize().y * 2);
}

float2 NdcToPixel(float2 ndc)
{
    return ViewportOrigin() + float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5) * ViewportSize();
}

float3 ViewRayAtUnitDepth(float2 pixel)
{
    float2 ndc = PixelToNdc(pixel);
    return float3((ndc.x - ViewToNdcOffset().x) / ViewToNdcScale().x,
                  (ndc.y - ViewToNdcOffset().y) / ViewToNdcScale().y, 1);
}

bool InsideBody(float3 position, float4 bodyBase, float4 bodyUp)
{
    float3 fromBase = position - bodyBase.xyz;
    float along = dot(fromBase, bodyUp.xyz);
    float3 across = fromBase - bodyUp.xyz * along;
    return along > 0 && along < bodyUp.w && dot(across, across) < bodyBase.w * bodyBase.w;
}

float2 ViewToPixel(float3 viewPosition)
{
    float2 ndc = float2(viewPosition.x / viewPosition.z * ViewToNdcScale().x + ViewToNdcOffset().x,
                        viewPosition.y / viewPosition.z * ViewToNdcScale().y + ViewToNdcOffset().y);
    return NdcToPixel(ndc);
}

static const float kSkyRatioFloor = 1e-3;
static const float kSkyRatioFloorShare = 0.05;
static const float kSkyRatioMax = 64;

float3 SkyRatioBase(float3 target)
{
    return max(target, max(kSkyRatioFloor, kSkyRatioFloorShare * max(target.r, max(target.g, target.b))));
}

float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

static const float kMemoryDepthOffset = 4096;
static const float kMemoryDepthRange = 8192;
static const float kMemoryDepthSteps = 8;
static const float kMemoryCodesPerDepthStep = 256;
static const float kMemoryCodesPerSlopeClass = 16;
static const float kMemorySubTexels = 4;
static const float kMemorySlopeClasses = 16;
static const float kMemorySlopeClassesPerOctave = 1.5;
static const float kMemorySlopeOctaveOffset = 4;

struct MemoryFrame
{
    float2 cameraIndex;
    float2 cameraFraction;
    float3 u;
    float texelYards;
    float3 v;
    float size;
    float3 l;
    float cameraDepth;
};

struct MemoryPlacement
{
    float2 texel;
    float code;
    bool inside;
};

struct MemoryCode
{
    float depthStep;
    float slopeClass;
    float2 subCell;
};

MemoryFrame LoadMemoryFrame(float4 camera, float4 u, float4 v, float4 l)
{
    MemoryFrame frame;
    frame.cameraIndex = camera.xy;
    frame.cameraFraction = camera.zw;
    frame.u = u.xyz;
    frame.texelYards = u.w;
    frame.v = v.xyz;
    frame.size = v.w;
    frame.l = l.xyz;
    frame.cameraDepth = l.w;
    return frame;
}

bool MemoryCodeKnown(float code)
{
    return code >= kMemoryCodesPerDepthStep;
}

MemoryCode DecodeMemory(float code)
{
    MemoryCode decoded;
    decoded.depthStep = floor(code / kMemoryCodesPerDepthStep);
    float rest = code - decoded.depthStep * kMemoryCodesPerDepthStep;
    decoded.slopeClass = floor(rest / kMemoryCodesPerSlopeClass);
    float subTexel = rest - decoded.slopeClass * kMemoryCodesPerSlopeClass;
    float subRow = floor(subTexel / kMemorySubTexels);
    decoded.subCell = float2(subTexel - subRow * kMemorySubTexels, subRow);
    return decoded;
}

float MemorySlopeClass(float slope)
{
    float octaves = log2(max(slope, 1e-6)) + kMemorySlopeOctaveOffset;
    return clamp(ceil(octaves * kMemorySlopeClassesPerOctave), 0, kMemorySlopeClasses - 1);
}

float MemorySlope(float slopeClass)
{
    return exp2(slopeClass / kMemorySlopeClassesPerOctave - kMemorySlopeOctaveOffset);
}

float SlopeAcrossLight(float3 normal, float3 toLight)
{
    float along = abs(dot(normal, toLight));
    return sqrt(saturate(1 - along * along)) / max(along, 1e-6);
}

MemoryPlacement PlaceInMemory(MemoryFrame frame, float3 offsetFromCamera, float slopeClass, float halfWindowTexels)
{
    float2 texels = float2(dot(offsetFromCamera, frame.u), dot(offsetFromCamera, frame.v)) / frame.texelYards;
    float depth = frame.cameraDepth + dot(offsetFromCamera, frame.l) + kMemoryDepthOffset;
    float2 index = frame.cameraIndex + frame.cameraFraction + texels;
    float2 wrapped = index - frame.size * floor(index / frame.size);
    float2 subTexel = floor((wrapped - floor(wrapped)) * kMemorySubTexels);
    MemoryPlacement placement;
    placement.texel = floor(wrapped);
    placement.code = floor(depth * kMemoryDepthSteps) * kMemoryCodesPerDepthStep +
                     slopeClass * kMemoryCodesPerSlopeClass + subTexel.y * kMemorySubTexels + subTexel.x;
    placement.inside = all(abs(texels) < halfWindowTexels) && depth >= 0 && depth < kMemoryDepthRange;
    return placement;
}

float4 MemoryTexelPosition(float2 texel, float size)
{
    return float4(texel.x * 2 / size - 1, 1 - texel.y * 2 / size, 0.5, 1);
}

float2 MemoryTexelFromCamera(MemoryFrame frame, float2 texel)
{
    float2 fromCamera = texel - frame.cameraIndex;
    return fromCamera - frame.size * floor(fromCamera / frame.size + 0.5);
}

float3 MemoryPointWithin(MemoryFrame frame, float2 texelFromCamera, float code, float3 within)
{
    MemoryCode decoded = DecodeMemory(code);
    float2 inTexel = (decoded.subCell + within.xy) / kMemorySubTexels;
    float2 across = (texelFromCamera + inTexel - frame.cameraFraction) * frame.texelYards;
    float along = (decoded.depthStep + within.z) / kMemoryDepthSteps - kMemoryDepthOffset - frame.cameraDepth;
    return across.x * frame.u + across.y * frame.v + along * frame.l;
}

float3 MemoryPointFromCamera(MemoryFrame frame, float2 texelFromCamera, float code)
{
    return MemoryPointWithin(frame, texelFromCamera, code, 0.5);
}


#define cMemoryMove LvConstants[9]
#define cMemoryCamera LvConstants[10]
#define cMemoryU LvConstants[11]
#define cMemoryV LvConstants[12]
#define cMemoryL LvConstants[13]

[[vk::binding(4,0)]] Texture2D<float4> sPrevious;
[[vk::binding(5,0)]] Texture2D<float4> sDepth;

static const float kNearestViewZ = 0.3;
static const float kKeepMarginYards = 1;
static const float kKeepMarginPerViewZ = 0.02;
static const float kPlacementYards = 0.1;
static const float kMinTapPixels = 1.5;

float2 WindowShift()
{
    return cMemoryMove.xy;
}

bool Forget()
{
    return cMemoryMove.z > 0;
}

float FocalPixels()
{
    return cMemoryMove.w;
}

float3 WorldToViewDirection(float3 worldDirection)
{
    return mul((float3x3)cViewToWorld, worldDirection);
}

float SceneViewZ(float2 pixel)
{
    return LinearDepth(SampleDepth(sDepth, clamp(pixel, ViewportOrigin() + 0.5, ViewportLastPixelCentre())));
}

float NearestSceneViewZ(float2 pixel, float radius)
{
    return min(min(SceneViewZ(pixel), min(SceneViewZ(pixel + float2(radius, 0)), SceneViewZ(pixel - float2(radius, 0)))),
               min(SceneViewZ(pixel + float2(0, radius)), SceneViewZ(pixel - float2(0, radius))));
}

float4 LvPixel(float2 vpos)
{
    MemoryFrame frame = LoadMemoryFrame(cMemoryCamera, cMemoryU, cMemoryV, cMemoryL);
    float code = sPrevious.SampleLevel(LvPoint,(float4((vpos + 0.5) / frame.size, 0, 0)).xy,(float4((vpos + 0.5) / frame.size, 0, 0)).w).r;
    float2 fromCamera = MemoryTexelFromCamera(frame, vpos);
    float2 before = fromCamera + WindowShift();
    bool known = MemoryCodeKnown(code) && !Forget() && all(before >= -0.5 * frame.size) &&
                 all(before < 0.5 * frame.size);
    float3 view = WorldToViewDirection(MemoryPointFromCamera(frame, fromCamera, code));
    float2 pixel = ViewToPixel(view);
    bool seen = known && view.z > kNearestViewZ && !OutsideViewport(pixel);
    float kept = known ? code : 0;
    [branch] if (seen)
    {
        float radius = max(kMinTapPixels, kPlacementYards * FocalPixels() / view.z);
        float margin = max(kKeepMarginYards, kKeepMarginPerViewZ * view.z);
        kept = NearestSceneViewZ(pixel, radius) > view.z + margin ? 0 : code;
    }
    return kept;
}

[numthreads(8,8,1)] void CSMain(uint3 id : SV_DispatchThreadID) {
uint w,h;LvOutput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
LvOutput[id.xy]=LvPixel(float2(id.xy));
}
