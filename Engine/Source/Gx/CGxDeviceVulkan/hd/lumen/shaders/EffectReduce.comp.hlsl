// Generated Vulkan compute adaptation of Lumenveil 0.1.1. Original equations retained.
[[vk::binding(0,0)]] cbuffer LvUniforms {float4 LvConstants[128];};
[[vk::binding(1,0)]] SamplerState LvPoint;
[[vk::binding(2,0)]] SamplerState LvLinear;
[[vk::binding(3,0)]] SamplerState LvWrap;
[[vk::binding(20,0)]] RWTexture2D<float4> LvOutput;
#define cEffectReduce LvConstants[9]
#define cEffectGatherSize LvConstants[10]

[[vk::binding(4,0)]] Texture2D<float4> sLight;
[[vk::binding(5,0)]] Texture2D<float4> sPlace;

static const int kTexelsPerCell = 4;

float CellsAcross()
{
    return cEffectReduce.x;
}

float SumScale()
{
    return cEffectReduce.y;
}

float2 GatherSize()
{
    return cEffectGatherSize.xy;
}

float2 GatherTexelSize()
{
    return cEffectGatherSize.zw;
}

float4 LvPixel(float2 texel)
{
    bool place = texel.x >= CellsAcross();
    float2 cell = float2(place ? texel.x - CellsAcross() : texel.x, texel.y);
    float4 sum = 0;
    [loop] for (int y = 0; y < kTexelsPerCell; y++)
    {
        [loop] for (int x = 0; x < kTexelsPerCell; x++)
        {
            float2 gather = cell * kTexelsPerCell + float2(x, y);
            float4 uv = float4((gather + 0.5) * GatherTexelSize(), 0, 0);
            float4 value = place ? sPlace.SampleLevel(LvPoint,(uv).xy,(uv).w) : sLight.SampleLevel(LvPoint,(uv).xy,(uv).w);
            value = all(gather < GatherSize()) ? value : 0;
            sum.xyz += value.xyz;
            sum.w = place ? max(sum.w, value.w) : sum.w + value.w;
        }
    }
    return float4(sum.xyz * SumScale(), place ? sum.w : sum.w * SumScale());
}

[numthreads(8,8,1)] void CSMain(uint3 id : SV_DispatchThreadID) {
uint w,h;LvOutput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
LvOutput[id.xy]=LvPixel(float2(id.xy));
}
