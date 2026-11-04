// Lumenveil GPL-2.0 algorithms ported to native Vulkan compute; see provenance.
[[vk::binding(0,0)]] cbuffer LvUniforms {float4 LvConstants[128];};
[[vk::binding(1,0)]] SamplerState LvPoint;
[[vk::binding(2,0)]] SamplerState LvLinear;
[[vk::binding(3,0)]] SamplerState LvWrap;
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


#define cRealign LvConstants[9]
#define cNewCamera LvConstants[10]
#define cNewU LvConstants[11]
#define cNewV LvConstants[12]
#define cNewL LvConstants[13]
#define cOldCamera LvConstants[14]
#define cOldU LvConstants[15]
#define cOldV LvConstants[16]
#define cOldL LvConstants[17]

[[vk::binding(4,0)]] Texture2D<float4> sOld;

static const float4 kCulledPoint = float4(2, 2, 2, 1);

struct RealignPoint
{
    float4 position : POSITION;
    float size : PSIZE;
    float code : TEXCOORD0;
};

float2 TileOrigin()
{
    return cRealign.xy;
}

float HalfWindowTexels()
{
    return cRealign.z;
}

float RealignSeed()
{
    return cRealign.w;
}

float3 PlaceInCell(float2 texel)
{
    return frac(float3(dot(texel, float2(0.7548777, 0.5698403)), dot(texel, float2(0.5698403, 0.2548777)),
                       dot(texel, float2(0.3141593, 0.8191725))) +
                RealignSeed() * float3(0.6180340, 0.3819660, 0.7548777));
}

RealignPoint LvPixel(float2 grid : POSITION)
{
    MemoryFrame before = LoadMemoryFrame(cOldCamera, cOldU, cOldV, cOldL);
    MemoryFrame after = LoadMemoryFrame(cNewCamera, cNewU, cNewV, cNewL);
    float2 texel = TileOrigin() + grid;
    float code = sOld.SampleLevel(LvPoint,(float4((texel + 0.5) / before.size, 0, 0)).xy,(float4((texel + 0.5) / before.size, 0, 0)).w).r;
    float3 offset = MemoryPointWithin(before, MemoryTexelFromCamera(before, texel), code, PlaceInCell(texel));
    MemoryPlacement placement = PlaceInMemory(after, offset, DecodeMemory(code).slopeClass, HalfWindowTexels());
    RealignPoint o;
    o.position = MemoryCodeKnown(code) && placement.inside ? MemoryTexelPosition(placement.texel, after.size)
                                                           : kCulledPoint;
    o.size = 1;
    o.code = placement.code;
    return o;
}

[[vk::binding(20,0)]] RWTexture2D<uint> LvOutput;
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){
uint w,h;LvOutput.GetDimensions(w,h);
if(id.x>=w||id.y>=h)return;RealignPoint o=LvPixel(float2(id.xy));
if(any(o.position.xy < -1)||any(o.position.xy>1))return;
uint2 cell=(uint2)floor(float2((o.position.x+1)*.5,(1-o.position.y)*.5)*float2(w,h)+.01);
if(cell.x<w&&cell.y<h){uint previous;InterlockedMax(LvOutput[cell],(uint)o.code,previous);}}
