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


#define cViewToPreviousClip float4x4(LvConstants[9],LvConstants[10],LvConstants[11],LvConstants[12])
#define cTemporal LvConstants[13]

[[vk::binding(4,0)]] Texture2D<float4> sCurrent;
[[vk::binding(5,0)]] Texture2D<float4> sHistory;
[[vk::binding(6,0)]] Texture2D<float4> sDepth;

static const float kSameSurfaceDepthShare = 0.1;

float HistoryWeight()
{
    return cTemporal.x;
}

bool HistoryInvalid()
{
    return cTemporal.y <= 0;
}

float ViewZAtLowResTexel(float2 lowResTexel)
{
    return LinearDepth(SampleDepth(sDepth, LowResTexelToFullPixel(lowResTexel)));
}

void CurrentNeighbourhoodRange(float2 lowResTexel, float2 uv, float4 centre, float viewZ, out float4 lowest,
                               out float4 highest)
{
    lowest = centre;
    highest = centre;
    static const float2 kNeighbourOffsets[8] = {
        float2(-1, -1), float2(0, -1), float2(1, -1), float2(-1, 0),
        float2(1, 0), float2(-1, 1), float2(0, 1), float2(1, 1)
    };
    [unroll] for (int k = 0; k < 8; k++)
    {
        float4 neighbour = sCurrent.SampleLevel(LvLinear,(float4(uv + kNeighbourOffsets[k] * LowResTexelSize(), 0, 0)).xy,(float4(uv + kNeighbourOffsets[k] * LowResTexelSize(), 0, 0)).w);
        float neighbourViewZ = ViewZAtLowResTexel(lowResTexel + kNeighbourOffsets[k]);
        bool sameSurface = abs(neighbourViewZ - viewZ) < kSameSurfaceDepthShare * viewZ;
        lowest = sameSurface ? min(lowest, neighbour) : lowest;
        highest = sameSurface ? max(highest, neighbour) : highest;
    }
}

float4 LvPixel(float2 lowResTexel)
{
    float2 uv = LowResTexelToUv(lowResTexel);
    float4 current = sCurrent.SampleLevel(LvLinear,(float4(uv, 0, 0)).xy,(float4(uv, 0, 0)).w);
    [branch] if (HistoryInvalid())
        return current;

    float2 pixel = LowResTexelToFullPixel(lowResTexel);
    float viewZ = LinearDepth(SampleDepth(sDepth, pixel));
    float4 lowest;
    float4 highest;
    CurrentNeighbourhoodRange(lowResTexel, uv, current, viewZ, lowest, highest);

    float4 previousClip = mul(float4(ViewRayAtUnitDepth(pixel) * viewZ, 1), cViewToPreviousClip);
    [branch] if (previousClip.w <= 1e-3)
        return current;
    float2 previousPixel = NdcToPixel(previousClip.xy / previousClip.w);
    float2 previousUv = LowResTexelToUv(FullPixelToLowResTexel(previousPixel));
    [branch] if (any(previousUv < 0) || any(previousUv > 1))
        return current;
    float4 history = clamp(sHistory.SampleLevel(LvLinear,(float4(previousUv, 0, 0)).xy,(float4(previousUv, 0, 0)).w), lowest, highest);
    return lerp(current, history, HistoryWeight());
}

[numthreads(8,8,1)] void CSMain(uint3 id : SV_DispatchThreadID) {
uint w,h;LvOutput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
LvOutput[id.xy]=LvPixel(float2(id.xy));
}
