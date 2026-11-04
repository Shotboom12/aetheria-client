// Lumenveil GPL-2.0 algorithms ported to native Vulkan compute; see provenance.
[[vk::binding(0,0)]] cbuffer LvUniforms {float4 LvConstants[128];};
[[vk::binding(1,0)]] SamplerState LvPoint;
[[vk::binding(2,0)]] SamplerState LvLinear;
[[vk::binding(3,0)]] SamplerState LvWrap;
[[vk::binding(4,0)]] Texture2D<float4> Source;
[[vk::binding(20,0)]] RWTexture2D<uint> LvOutput;
[numthreads(8,8,1)]void CSMain(uint3 id:SV_DispatchThreadID){uint w,h;LvOutput.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;LvOutput[id.xy]=(uint)max(Source.Load(int3(id.xy,0)).r,0);}
