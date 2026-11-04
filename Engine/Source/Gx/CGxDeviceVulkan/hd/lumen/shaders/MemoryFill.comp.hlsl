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


#define cFill LvConstants[9]

[[vk::binding(4,0)]] Texture2D<float4> sRealigned;

static const float kMemoryCentreSubTexel = 10;
static const float kFillMinNeighbours = 2.5;

float TexelUv()
{
    return cFill.x;
}

float CodeAt(float2 vpos)
{
    return sRealigned.SampleLevel(LvPoint,(float4((vpos + 0.5) * TexelUv(), 0, 0)).xy,(float4((vpos + 0.5) * TexelUv(), 0, 0)).w).r;
}

float4 LvPixel(float2 vpos)
{
    float code = CodeAt(vpos);
    float4 around = float4(CodeAt(vpos + float2(1, 0)), CodeAt(vpos - float2(1, 0)), CodeAt(vpos + float2(0, 1)),
                           CodeAt(vpos - float2(0, 1)));
    float4 known = around >= kMemoryCodesPerDepthStep;
    float count = dot(known, 1);
    float4 steps = floor(around / kMemoryCodesPerDepthStep);
    float4 classes = floor((around - steps * kMemoryCodesPerDepthStep) / kMemoryCodesPerSlopeClass);
    float4 low = known ? steps : 1e9;
    float4 high = known ? steps : -1e9;
    float lowest = min(min(low.x, low.y), min(low.z, low.w));
    float highest = max(max(high.x, high.y), max(high.z, high.w));
    float middle = dot(known ? steps : 0, 1) - lowest - highest;
    middle = count > 3.5 ? floor(middle * 0.5) : middle;
    float4 apart = known ? abs(steps - middle) : 1e9;
    float nearest = min(min(apart.x, apart.y), min(apart.z, apart.w));
    float4 nearClasses = apart <= nearest + 0.5 ? classes : 0;
    float slopeClass = max(max(nearClasses.x, nearClasses.y), max(nearClasses.z, nearClasses.w));
    float filled = middle * kMemoryCodesPerDepthStep + slopeClass * kMemoryCodesPerSlopeClass + kMemoryCentreSubTexel;
    return MemoryCodeKnown(code) || count < kFillMinNeighbours ? code : filled;
}

[[vk::binding(20,0)]] RWTexture2D<float4> LvOutput;
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){uint w,h;LvOutput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;LvOutput[id.xy]=LvPixel(float2(id.xy));}
