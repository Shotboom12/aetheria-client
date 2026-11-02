// SPDX-License-Identifier: GPL-3.0-or-later
layout(local_size_x=8,local_size_y=8) in;
layout(set=0,binding=0) uniform sampler2D sceneDepth;
layout(set=0,binding=1) uniform sampler2D sceneNormal;
layout(set=0,binding=2) uniform sampler2D lowEndpoint;
layout(set=0,binding=3) uniform sampler2D highEndpoint;
layout(set=0,binding=4) uniform sampler2D shadow0;
layout(set=0,binding=5) uniform sampler2D shadow1;
layout(set=0,binding=6) uniform sampler2D shadow2;
layout(set=0,binding=7) uniform sampler2D shadow3;
layout(set=0,binding=8) uniform sampler2D shadow4;
layout(set=0,binding=9) uniform sampler2D contactSource;
layout(rgba8,set=0,binding=10) uniform writeonly image2D contactTarget;
layout(rgba8,set=0,binding=11) uniform image2D sceneColour;
layout(std140,set=0,binding=12) uniform Params {
 vec4 screen,view,rect,projection,depth,sun,settings,contact,jitter;
 vec4 rows[15];vec4 maps[5];
} c;
layout(push_constant) uniform Pass {uvec4 flags;} pc;
float linearDepth(ivec2 p) {float d=texelFetch(sceneDepth,clamp(p,ivec2(0),textureSize(sceneDepth,0)-1),0).r*c.screen.z+c.screen.w;return d>=c.depth.z?0:c.depth.y/max(c.depth.x-d,1e-8);}
vec3 pointAt(vec2 uv,float z) {
 vec2 v=(uv-c.rect.xy)*c.rect.zw;
 v-=c.jitter.xy*c.screen.xy*c.rect.zw;
 return vec3(v*c.view.xy+c.view.zw,1)*z;
}
vec2 project(vec3 p) {vec2 ndc=p.xy/p.z*c.projection.xy+c.projection.zw;return (vec2(ndc.x*.5+.5,.5-ndc.y*.5))/c.rect.zw+c.rect.xy+c.jitter.xy*c.screen.xy;}
bool matching(float tag,float z) {return abs(abs(tag)-(.505+.49*clamp((log2(max(z,.0625))+4)/16,0,1)))<c.depth.w;}
// HD: the depth a record was written at, from its tag (the inverse of the tag's log2 scale)
float tagDepth(float tag) {return exp2((abs(tag)-.505)/.49*16-4);}
// HD: a draw without a G-buffer variant (the grass) over another surface's record, that surface close behind it
// (the record's depth read from the normal's tag: SNORM, 0.1 %, finer than the endpoints' SFLOAT, so the blade's foot
// already counts as grass, not as the ground; the endpoints must be the same record)
float recordDepth(ivec2 px) {return tagDepth(texelFetch(sceneNormal,px,0).w);}
bool overRecord(ivec2 px,vec4 lo,vec4 hi,float z) {float t=texelFetch(sceneNormal,px,0).w,r=tagDepth(t);return z>0&&abs(t)>.5&&abs(t)<.999&&abs(lo.a)>.5&&lo.a*hi.a>0&&matching(lo.a,r)&&r>z*1.003&&r-z<max(c.jitter.w,z*.5);} // seen low, a tall blade stands far in front of the ground behind it
bool ownRecord(vec4 lo,vec4 hi,float z) {return z>0&&matching(lo.a,z)&&matching(hi.a,z)&&lo.a*hi.a>0;}
// HD: grass with no ground close behind it (against the sky, a far slope): no record to borrow, its own sun is taken
bool looseGrass(ivec2 px,vec4 lo,vec4 hi,float z) {return z>0&&z<300&&!ownRecord(lo,hi,z)&&!overRecord(px,lo,hi,z);}
vec3 normalAt(ivec2 px,vec3 p) {
 vec4 n=texelFetch(sceneNormal,px,0);if(matching(n.w,p.z)&&dot(n.xyz,n.xyz)>.25)return normalize(n.xyz);
 vec2 uv=(vec2(px)+.5)*c.screen.xy;
 float l=linearDepth(px-ivec2(1,0)),r=linearDepth(px+ivec2(1,0)),u=linearDepth(px-ivec2(0,1)),d=linearDepth(px+ivec2(0,1));
 vec3 dx=abs(l-p.z)<abs(r-p.z)?p-pointAt(uv-vec2(c.screen.x,0),l):pointAt(uv+vec2(c.screen.x,0),r)-p;
 vec3 dy=abs(u-p.z)<abs(d-p.z)?p-pointAt(uv-vec2(0,c.screen.y),u):pointAt(uv+vec2(0,c.screen.y),d)-p;
 vec3 nn=cross(dx,dy);if(dot(nn,nn)<1e-12)return vec3(0,0,-1);nn=normalize(nn);return dot(nn,p)>0?-nn:nn;
}
float mapDepth(int i,vec2 uv) {if(i==0)return textureLod(shadow0,uv,0).r;if(i==1)return textureLod(shadow1,uv,0).r;if(i==2)return textureLod(shadow2,uv,0).r;if(i==3)return textureLod(shadow3,uv,0).r;return textureLod(shadow4,uv,0).r;}
vec3 shadowCoord(int i,vec3 p){vec4 q=vec4(p,1);return vec3(dot(q,c.rows[3*i]),dot(q,c.rows[3*i+1]),dot(q,c.rows[3*i+2]));}
const vec2 disk[24]=vec2[24](vec2(.143,.123),vec2(-.203,.245),vec2(.044,-.397),vec2(.31,.35),vec2(-.53,-.094),vec2(.507,-.311),vec2(-.139,.63),vec2(-.326,-.613),vec2(.697,.233),vec2(-.73,.264),vec2(.388,-.718),vec2(.21,.827),vec2(-.739,-.5),vec2(.897,-.245),vec2(-.515,.82),vec2(-.07,-.978),vec2(.72,.62),vec2(-.944,.06),vec2(.628,-.711),vec2(-.36,-.24),vec2(.03,.054),vec2(.025,.79),vec2(.62,-.08),vec2(-.5,.36));
float pcss(int i,vec3 p) {
 if(c.maps[i].z<.5)return 1;
 vec3 q=shadowCoord(i,p);vec2 uv=q.xy*.5+.5;
 if(max(abs(q.x),abs(q.y))>=.995||q.z<=0||q.z>=1)return 1;
 float texel=c.maps[i].y,search=min(c.settings.z/(2*c.maps[i].x),.06);
 int blockers=pc.flags.z==0u?8:12,filters=pc.flags.z==0u?16:24;
 float sum=0,count=0;
 for(int n=0;n<blockers;++n){vec2 tap=uv+disk[n]*max(search,texel);if(any(lessThan(tap,vec2(0)))||any(greaterThan(tap,vec2(1))))continue;float d=mapDepth(i,tap);if(d<q.z){sum+=d;count++;}}
 if(count==0)return 1;
 // Native R32F stores linear light-view Z / 4000. Directional emitter: no perspective blocker division.
 float radius=clamp((q.z-sum/count)*c.maps[i].w*c.settings.x/(2*c.maps[i].x),texel*.65,max(search,texel*.65));
 float visible=0;
 for(int n=0;n<filters;++n){vec2 tap=uv+disk[n]*radius;visible+=(any(lessThan(tap,vec2(0)))||any(greaterThan(tap,vec2(1)))||mapDepth(i,tap)>=q.z)?1:0;}
 return visible/float(filters);
}
float environment(vec3 p) {
 // HD: same result as blending bands 3..1, but a band that fully holds the point (inside its .85 edge) hides the
 // bands after it, so their PCSS is not computed (2 = band not used, as before)
 float s[4];int last=0;
 for(int i=1;i<=3;++i){s[i]=2;if(c.maps[i].z>.5){vec3 q=shadowCoord(i,p);float edge=max(abs(q.x),abs(q.y));if(edge<.98)s[i]=smoothstep(.85,.98,edge);}last=i;if(s[i]==0)break;}
 float v=1;
 for(int i=last;i>=1;--i)if(s[i]<1.5)v=mix(pcss(i,p),v,s[i]);
 return v;
}
float visibility(vec3 p,bool interior) {
 if(interior)return pcss(4,p);
 float env=environment(p);vec3 q=shadowCoord(0,p);float edge=max(abs(q.x),abs(q.y));
 return c.maps[0].z>.5&&edge<.98?mix(min(env,pcss(0,p)),env,smoothstep(.85,.98,edge)):env;
}
