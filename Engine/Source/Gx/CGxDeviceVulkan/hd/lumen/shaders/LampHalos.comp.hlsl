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


static const float kLampSlots = 237;
static const float kLampTableRows = 8;
static const float kLampSphereRow = 0;
static const float kLampLightRow = 1;
static const float kLampScreenRow = 2;
static const float kLampTestRow = 3;
static const float kLampSurfaceRow = 4;
static const float kLampViewRow = 5;
static const float kLampEmitRow = 6;
static const float kLampCoreRow = 7;

float SlotU(float slot)
{
    return (slot + 0.5) / kLampSlots;
}

float4 LampTexel(Texture2D<float4> table, float slotU, float row)
{
    return table.SampleLevel(LvPoint,(float4(slotU, (row + 0.5) / kLampTableRows, 0, 0)).xy,(float4(slotU, (row + 0.5) / kLampTableRows, 0, 0)).w);
}

static const float kLampTileBlockWidth = 16;
static const float kLampTileBlockHeight = 8;
static const float kLampTileCapacity = kLampTileBlockWidth * kLampTileBlockHeight - 1;
static const float kLampTileEveryLamp = 255;

float LampTileEntry(Texture2D<float4> tiles, float4 layout, float2 tile, float index)
{
    float row = floor(index / kLampTileBlockWidth);
    float column = index - row * kLampTileBlockWidth;
    float2 texel = float2(tile.x * kLampTileBlockWidth + column, layout.z + tile.y * kLampTileBlockHeight + row);
    return floor(tiles.SampleLevel(LvPoint,(float4((texel + 0.5) * layout.xy, 0, 0)).xy,(float4((texel + 0.5) * layout.xy, 0, 0)).w).r * 255 + 0.5);
}

float2 LampTileOf(float4 layout, float2 pixel)
{
    return floor((pixel - ViewportOrigin()) / max(layout.w, 1));
}

float LampTileListed(Texture2D<float4> tiles, float4 layout, float2 tile)
{
    return layout.w > 0 ? LampTileEntry(tiles, layout, tile, 0) : kLampTileEveryLamp;
}


#define cLampPass LvConstants[9]
#define cLampTiles LvConstants[10]

[[vk::binding(4,0)]] Texture2D<float4> sDepth;
[[vk::binding(5,0)]] Texture2D<float4> sFog;
[[vk::binding(6,0)]] Texture2D<float4> sLampVisibility;
[[vk::binding(7,0)]] Texture2D<float4> sLampTable;
[[vk::binding(8,0)]] Texture2D<float4> sLampTiles;

static const float kCoreRadius = 1.25;
static const float kFalloffShareOfReach = 0.2;
static const float kAnisotropy = 0.2;
static const float kInverseFourPi = 0.07957747;
static const float kEdgeFadeStart = 0.72;
static const float kEnergyScale = 0.9;
static const float kEnergyCap = 0.75;
static const float kMinTransmittance = 0.02;
static const float kMinRayYards = 1;
static const float kFarCoreYards = 0.4;
static const float kFarCoreSpread = 3;
static const float kFarCoreFrontMargin = 2;
static const float kFarCoreFrontMarginPerYard = 0.02;

float SlotCount()
{
    return cLampPass.x;
}

float FarCoreAnglePerTexel()
{
    return cLampPass.y;
}

float FarCoreAngle(float along)
{
    return max(FarCoreAnglePerTexel(), kFarCoreYards / along);
}

float LampPhase(float cosine)
{
    float denominator = 1 + kAnisotropy * kAnisotropy - 2 * kAnisotropy * cosine;
    return kInverseFourPi * (1 - kAnisotropy * kAnisotropy) / (denominator * sqrt(denominator));
}

float4 LvPixel(float2 lowResTexel)
{
    float2 pixel = LowResTexelToFullPixel(lowResTexel);
    float viewZ = LinearDepth(SampleDepth(sDepth, pixel));
    float3 viewRay = ViewRayAtUnitDepth(pixel);
    float3 ray = ViewToWorldDirection(viewRay);
    float yardsPerViewZ = length(ray);
    ray /= yardsPerViewZ;
    float surface = viewZ * yardsPerViewZ;
    float fogOpacity = sFog.SampleLevel(LvLinear,(float4(LvConstants[12].xy+LowResTexelToUv(lowResTexel)*(all(LvConstants[12].zw>0)?LvConstants[12].zw:float2(1,1)), 0, 0)).xy,(float4(LvConstants[12].xy+LowResTexelToUv(lowResTexel)*(all(LvConstants[12].zw>0)?LvConstants[12].zw:float2(1,1)), 0, 0)).w).a;
    if(LvConstants[11].x>0.5)fogOpacity=1-fogOpacity;
    float extinction = -log(max(1 - fogOpacity, kMinTransmittance)) / max(surface, kMinRayYards);

    float2 tile = LampTileOf(cLampTiles, pixel);
    float listed = LampTileListed(sLampTiles, cLampTiles, tile);
    bool every = listed > kLampTileCapacity;
    float count = every ? SlotCount() : listed;
    float3 energy = 0;
    float entry = 0;
    [loop] while (entry < count)
    {
        float slot = every ? entry : LampTileEntry(sLampTiles, cLampTiles, tile, entry + 1);
        float slotU = SlotU(slot);
        float4 sphere = LampTexel(sLampTable, slotU, kLampSphereRow);
        float along = dot(sphere.xyz, ray);
        float offsetSquared = max(dot(sphere.xyz, sphere.xyz) - along * along, 0);
        float reachSquared = sphere.w * sphere.w;
        [branch] if (offsetSquared < reachSquared)
        {
            float halfSpan = sqrt(reachSquared - offsetSquared);
            float start = max(0, along - halfSpan);
            float end = min(surface, along + halfSpan);
            [branch] if (end > start)
            {
                float falloff = max(sphere.w * kFalloffShareOfReach, 2 * kCoreRadius);
                float inner = sqrt(offsetSquared + kCoreRadius * kCoreRadius);
                float outer = sqrt(offsetSquared + falloff * falloff);
                float2 span = float2(start, end) - along;
                float2 innerAngles = atan(span / inner);
                float2 outerAngles = atan(span / outer);
                float integral = falloff / (falloff - kCoreRadius) *
                                 ((innerAngles.y - innerAngles.x) / inner - (outerAngles.y - outerAngles.x) / outer);
                float closest = clamp(along, start, end);
                float3 lampToSample = ray * closest - sphere.xyz;
                float3 lightPath = lampToSample / max(length(lampToSample), 1e-4);
                float edge = 1 - smoothstep(sphere.w * kEdgeFadeStart, sphere.w, sqrt(offsetSquared));
                float4 light = LampTexel(sLampTable, slotU, kLampLightRow);
                float visible = sLampVisibility.SampleLevel(LvPoint,(float4(slotU, 0.5, 0, 0)).xy,(float4(slotU, 0.5, 0, 0)).w).r;
                energy += light.rgb * (integral * LampPhase(dot(lightPath, -ray)) * edge *
                                       lerp(light.w, 1, visible) * exp(-extinction * closest));
            }
        }
        float coreAngle = FarCoreAngle(max(along, kMinRayYards));
        float angleSquared = offsetSquared / max(along * along, kMinRayYards);
        [branch] if (along > kMinRayYards && angleSquared < kFarCoreSpread * kFarCoreSpread * coreAngle * coreAngle &&
                     along < surface + max(kFarCoreFrontMargin, kFarCoreFrontMarginPerYard * along))
        {
            float4 core = LampTexel(sLampTable, slotU, kLampCoreRow);
            float visible = sLampVisibility.SampleLevel(LvPoint,(float4(slotU, 0.5, 0, 0)).xy,(float4(slotU, 0.5, 0, 0)).w).r;
            energy += core.rgb * (exp(-0.5 * angleSquared / (coreAngle * coreAngle)) * visible *
                                  exp(-extinction * along));
        }
        entry += 1;
    }
    return float4(1 - exp(-min(energy * kEnergyScale, kEnergyCap)), 1);
}

[numthreads(8,8,1)] void CSMain(uint3 id : SV_DispatchThreadID) {
uint w,h;LvOutput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
LvOutput[id.xy]=LvPixel(float2(id.xy));
}
