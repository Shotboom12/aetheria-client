// Lumenveil GPL-2.0 algorithms ported to native Vulkan compute; see provenance.
[[vk::binding(0,0)]] cbuffer LvUniforms {float4 LvConstants[128];};
[[vk::binding(1,0)]] SamplerState LvPoint;
[[vk::binding(2,0)]] SamplerState LvLinear;
[[vk::binding(3,0)]] SamplerState LvWrap;
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

[[vk::binding(4,0)]] Texture2D<float4> Scene;
[[vk::binding(5,0)]] Texture2D<float4> Depth;
[[vk::binding(6,0)]] Texture2D<float4> Lamps;
[[vk::binding(7,0)]] Texture2D<float4> Albedo;
[[vk::binding(8,0)]] Texture2D<float4> Light;
[[vk::binding(9,0)]] Texture2D<float4> Normal;
[[vk::binding(10,0)]] Texture2D<float4> Fog;
[[vk::binding(11,0)]] Texture2D<float4> Halos;
[[vk::binding(20,0)]] RWTexture2D<float4> LvOutput;
float3 ToRGB(float3 x){return LvConstants[9].w>0?x.bgr:x;}
float3 Linear(float3 x){return pow(max(x,0),2.2);}
float3 Gamma(float3 x){return pow(max(x,0),1/2.2);}
float3 PointGain(float3 base,float3 irradiance){
 float3 sum=base+irradiance;float brightest=max(max(sum.r,sum.g),sum.b);
 float3 hueKept=sum/max(brightest,1);float whitening=saturate((brightest-1)/(brightest+1));
 float3 lit=lerp(hueKept,min(sum,1),whitening);return max(lit*lit-base*base,0);
}
float4 LowRes(float2 local,float viewZ,out float3 halo){
 float2 size=LvConstants[11].xy,scale=LvConstants[11].zw;
 float2 coord=(local+.5)/scale-.5,base=floor(coord),frac=coord-base;
 float4 fog=0;halo=0;float sum=0;
 [unroll]for(int y=0;y<2;y++)[unroll]for(int x=0;x<2;x++){
 float2 at=clamp(base+float2(x,y),0,size-1),uv=(at+.5)/size;
 float2 full=ViewportOrigin()+(at+.5)*scale;
 float raw=Depth.SampleLevel(LvPoint,full*DepthTexelSize(),0).r;
 float tapZ=LinearDepth(raw);float2 b=lerp(1-frac,frac,float2(x,y));
 float weight=b.x*b.y/(.001+abs(tapZ-viewZ)/max(viewZ,.001))+1e-6;
 fog+=Fog.SampleLevel(LvLinear,uv,0)*weight;halo+=Halos.SampleLevel(LvLinear,uv,0).rgb*weight;sum+=weight;}
 halo/=sum;return fog/sum;
}
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
 uint w,h;LvOutput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
 float4 original=Scene.Load(int3(id.xy,0));float2 local=float2(id.xy)-ViewportOrigin();
 if(any(local<0)||any(local>=ViewportSize())){LvOutput[id.xy]=original;return;}
 float raw=Depth.Load(int3(id.xy,0)).r;float z=LinearDepth(raw);float3 before=ToRGB(original.rgb),result=before;
 if(LvConstants[9].x>0&&!BeyondFarClip(raw)){
 float4 material=Albedo.Load(int3(id.xy,0)),normal=Normal.Load(int3(id.xy,0));float tag=abs(normal.w);
 uint cls=((uint)round(material.a*255))&31;
 float decoded=tag>=.505&&tag<=.995?(tag-.505)/.49*16-4:0;
 bool covered=normal.w>.5&&(tag>=.999||(tag>=.505&&tag<=.995&&abs(decoded-clamp(log2(max(z,.0625)),-4,12))<=.03));
 if(covered&&cls>=1&&cls<=4){float3 albedo=material.rgb;float3 lit=Light.Load(int3(id.xy,0)).rgb;
 float3 irradiance=max(Lamps.Load(int3((int2)local,0)).rgb,0);
 // Preserve the already rendered scene; add only the selected lamp delta to its real material.
 if(any(irradiance>0))result=Gamma(Linear(before)+Linear(albedo)*PointGain(lit,irradiance)*LvConstants[10].x);
 }}
 if(LvConstants[9].y>0||LvConstants[9].z>0){float3 halo;float4 fog=LowRes(local,z,halo);
 float opacity=LvConstants[9].y>0?saturate(fog.a):0;float3 radiance=LvConstants[9].y>0?max(fog.rgb,0):0;
 float3 hlight=LvConstants[9].z>0?max(halo,0):0;
 if(opacity>0||any(radiance>0)||any(hlight>0))result=Gamma(Linear(result)*(1-opacity)+radiance+hlight);}
 LvOutput[id.xy]=float4(ToRGB(result),original.a);
}
