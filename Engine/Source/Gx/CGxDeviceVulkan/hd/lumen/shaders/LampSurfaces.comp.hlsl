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

static const float kLampBodyYards = 1.5;
static const float kShadowStartYards = 0.2;
static const float kMaxShadowStartShare = 0.5;
static const float kShadowBias = 0.1;
static const float kShadowBiasPerViewZ = 0.003;
static const float kMinOccluderYards = 1;
static const float kOccluderThicknessInSteps = 1.5;
static const float kOccluderThicknessPerViewZ = 0.02;
static const float kMinSampleViewZ = 0.3;

float LampShadow(Texture2D<float4> depthTexture, float3 start, float3 toLamp, float distance, float jitter, int steps,
                 float clearYards, float startYards, float4 bodyBase, float4 bodyUp)
{
    float travel = saturate(1 - clearYards / max(distance, 1e-3));
    float3 path = toLamp * travel;
    float startShare = min(startYards / max(distance * travel, 1e-3), kMaxShadowStartShare);
    float thickness = max(kMinOccluderYards, distance * travel / steps * kOccluderThicknessInSteps);
    float3 lamp = start + toLamp;
    float lit = 1;
    [loop] for (int k = 0; k < steps && lit > 0; k++)
    {
        float3 samplePosition = start + path * lerp(startShare, 1, (k + jitter) / steps);
        [branch] if (samplePosition.z >= kMinSampleViewZ)
        {
            float2 samplePixel = ViewToPixel(samplePosition);
            [branch] if (!OutsideViewport(samplePixel))
            {
                float sceneDepth = SampleDepth(depthTexture, samplePixel);
                float sceneViewZ = LinearDepth(sceneDepth);
                float behind = samplePosition.z - sceneViewZ;
                float3 occluderPosition = samplePosition * (sceneViewZ / samplePosition.z);
                float3 fromLamp = occluderPosition - lamp;
                bool occluder = !BeyondFarClip(sceneDepth) &&
                                behind > kShadowBias + kShadowBiasPerViewZ * samplePosition.z &&
                                behind < max(thickness, kOccluderThicknessPerViewZ * samplePosition.z) &&
                                dot(fromLamp, fromLamp) > clearYards * clearYards &&
                                !InsideBody(occluderPosition, bodyBase, bodyUp);
                lit = occluder ? 0 : 1;
            }
        }
    }
    return lit;
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

[[vk::binding(4,0)]] Texture2D<float4> sDepth;

static const float kMissingNeighbour = 1e9;
static const float kThinStep = 0.1;
static const float kThinStepPerViewZ = 0.005;
static const float kSliverPixels = 3;

float3 ViewPositionAt(float2 pixel)
{
    return ViewRayAtUnitDepth(pixel) * LinearDepth(SampleDepth(sDepth, pixel));
}

float StepScore(float3 step)
{
    return dot(step, step) > 1e-10 ? abs(step.z) : kMissingNeighbour;
}

float DepthAt(float2 pixel)
{
    return LinearDepth(SampleDepth(sDepth, clamp(pixel, ViewportOrigin() + 0.5, ViewportLastPixelCentre())));
}

bool Sliver(float2 pixel, float2 step, float z, float threshold)
{
    float ahead = DepthAt(pixel + step) - z;
    float behind = DepthAt(pixel - step) - z;
    return ahead * behind > 0 && min(abs(ahead), abs(behind)) > threshold;
}

float4 SurfaceNormal(float2 pixel, float3 centre)
{
    float2 low = ViewportOrigin() + 0.5;
    float2 high = ViewportLastPixelCentre();
    float3 right = ViewPositionAt(min(pixel + float2(1, 0), high)) - centre;
    float3 left = centre - ViewPositionAt(max(pixel - float2(1, 0), low));
    float3 below = ViewPositionAt(min(pixel + float2(0, 1), high)) - centre;
    float3 above = centre - ViewPositionAt(max(pixel - float2(0, 1), low));
    float3 alongX = StepScore(right) < StepScore(left) ? right : left;
    float3 alongY = StepScore(below) < StepScore(above) ? below : above;
    float3 facing = cross(alongX, alongY);
    float lengthSquared = dot(facing, facing);
    float3 normal = lengthSquared > 1e-12 ? facing * rsqrt(lengthSquared) : -normalize(centre);
    float threshold = kThinStep + kThinStepPerViewZ * centre.z;
    bool thin = Sliver(pixel, float2(kSliverPixels, 0), centre.z, threshold) ||
                Sliver(pixel, float2(0, kSliverPixels), centre.z, threshold);
    return float4(dot(normal, centre) > 0 ? -normal : normal, thin ? 1 : 0);
}


[[vk::binding(8,0)]] Texture2D<float4> sNativeMaterial;
[[vk::binding(9,0)]] Texture2D<float4> sNativeNormals;
#define cLampSurfaces LvConstants[9]
#define cSunInView LvConstants[10]
#define cCarrierBase LvConstants[11]
#define cCarrierUp LvConstants[12]
#define cLampTiles LvConstants[13]

[[vk::binding(5,0)]] Texture2D<float4> sLampTable;
[[vk::binding(6,0)]] Texture2D<float4> sOccluderDepth;
[[vk::binding(7,0)]] Texture2D<float4> sLampTiles;

static const int kShadowSteps = 16;
static const float kNearYards = 0.5;
static const float kFalloffPerYard = 0.7;
static const float kFalloffPerSquareYard = 0.03;
static const float kWrap = 0.15;
static const float kMinShadowedLight = 0.002;
static const float kThinFacing = 0.6;
static const float kWindowWrap = 0.1;
static const float kThinFacingCode = 1;
static const float kFacingCodeScale = 0.5;
static const float3 kLuminanceWeights = float3(0.2126, 0.7152, 0.0722);

float SlotCount()
{
    return cLampSurfaces.x;
}

float SunFacingCode(float4 normal)
{
    return normal.w > 0 ? kThinFacingCode : saturate(dot(normal.xyz, cSunInView.xyz)) * kFacingCodeScale;
}

float4 LvPixel(float2 targetPixel)
{
    float2 pixel = ViewportOrigin() + targetPixel + 0.5;
    float depth = SampleDepth(sDepth, pixel);
    [branch] if (BeyondFarClip(depth))
        return float4(0, 0, 0, 1);
    float3 surface = ViewRayAtUnitDepth(pixel) * LinearDepth(depth);
    float4 normal = SurfaceNormal(pixel, surface);
    if(LvConstants[14].x>0.5){float4 n=sNativeNormals.Load(int3((int2)pixel,0));float tag=abs(n.w);float decoded=(tag-.505)/.49*16-4;bool covered=n.w>.5&&(tag>=.999||(tag>=.505&&tag<=.995&&abs(decoded-clamp(log2(max(surface.z,.0625)),-4,12))<=.03));if(covered&&dot(n.xyz,n.xyz)>1e-8){normal.xyz=normalize(n.xyz);normal.xyz=dot(normal.xyz,surface)>0?-normal.xyz:normal.xyz;}}

    float jitter = InterleavedGradientNoise(pixel);

    float2 tile = LampTileOf(cLampTiles, pixel);
    float listed = LampTileListed(sLampTiles, cLampTiles, tile);
    bool every = listed > kLampTileCapacity;
    float count = every ? SlotCount() : listed;
    float3 pointLight = 0;
    float entry = 0;
    [loop] while (entry < count)
    {
        float slot = every ? entry : LampTileEntry(sLampTiles, cLampTiles, tile, entry + 1);
        float slotU = SlotU(slot);
        uint coverage=(uint)LampTexel(sLampTable,slotU,kLampCoreRow).w;
        if(coverage!=0){uint cls=((uint)round(sNativeMaterial.Load(int3((int2)pixel,0)).a*255))&31;if((coverage&(1u<<cls))!=0){entry+=1;continue;}}
        float4 lamp = LampTexel(sLampTable, slotU, kLampViewRow);
        float3 toLamp = lamp.xyz - surface;
        float distanceSquared = dot(toLamp, toLamp);
        float reachSquared = lamp.w * lamp.w;
        [branch] if (distanceSquared < reachSquared)
        {
            float4 light = LampTexel(sLampTable, slotU, kLampSurfaceRow);
            float4 emit = LampTexel(sLampTable, slotU, kLampEmitRow);
            float distance = sqrt(distanceSquared);
            float3 direction = toLamp / max(distance, 1e-4);
            bool carried = emit.w < 0;
            bool lantern = carried && emit.y > 0;
            float nearYards = emit.w != 0 ? abs(emit.w) : kNearYards;
            float spread = distanceSquared + nearYards * nearYards;
            float falloff = 1 / (kFalloffPerYard * sqrt(spread) + kFalloffPerSquareYard * spread);
            float lambert = saturate((dot(normal.xyz, direction) + kWrap) / (1 + kWrap));
            float facing = normal.w > 0 ? max(lambert, kThinFacing) : lambert;
            float outward = emit.w > 0 ? saturate((kWindowWrap - dot(emit.xyz, direction)) / (1 + kWindowWrap)) : 1;
            float window = saturate(1 - distanceSquared * distanceSquared / (reachSquared * reachSquared));
            float3 lit = light.rgb * (facing * outward * window * window * falloff);
            bool shadowed = light.w > 0 && (normal.w == 0 || lantern);
            [branch] if (shadowed && dot(lit, kLuminanceWeights) > kMinShadowedLight)
                lit *= lerp(1, LampShadow(sOccluderDepth, surface, toLamp, distance, jitter, kShadowSteps,
                                          carried ? emit.x : kLampBodyYards, carried ? emit.z : kShadowStartYards,
                                          float4(cCarrierBase.xyz, lantern ? cCarrierBase.w : 0), cCarrierUp),
                            light.w);
            pointLight += lit;
        }
        entry += 1;
    }
    return float4(pointLight, SunFacingCode(normal));
}

[numthreads(8,8,1)] void CSMain(uint3 id : SV_DispatchThreadID) {
uint w,h;LvOutput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
LvOutput[id.xy]=LvPixel(float2(id.xy));
}
