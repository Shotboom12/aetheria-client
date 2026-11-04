// HD: framerate-independent smoothing of the shared visual weather weight. No image history.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace HD {
struct WeatherBlendState {
    double value=0;uint32_t tick=0;int32_t map=0;bool valid=false;
    void Reset(){valid=false;}
    float Update(float target,uint32_t now,int32_t mapId,float halfLife){
        if(!std::isfinite(target))return valid?float(value):0.f;
        target=std::clamp(target,0.f,1.f);
        if(!valid||mapId!=map||!std::isfinite(halfLife)||halfLife<=0){value=target;tick=now;map=mapId;valid=true;return target;}
        const uint32_t elapsed=now-tick;tick=now;
        // An implausible unsigned delta means a clock discontinuity; don't turn it into a colour jump.
        if(elapsed>60000)return float(value);
        const double alpha=-std::expm1(-.6931471805599453094*double(elapsed)*.001/double(halfLife));
        value+=(double(target)-value)*alpha;
        if(std::abs(value-target)<1e-7)value=target;
        return float(value);
    }
};
}
