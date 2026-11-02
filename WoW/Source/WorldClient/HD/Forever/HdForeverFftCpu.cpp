#include "WoW/Source/WorldClient/HD/Forever/HdForeverFftCpu.h"
#include "WoW/Source/WorldClient/HD/Forever/FieldEncoding.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <locale>
#include <set>
#include <sstream>
namespace HD::ForeverNative {
namespace {
constexpr int32_t kN=64;
constexpr double kPi=3.14159265358979, kG=9.81;
using Tile=FftTile;
struct Complex { float re=0, im=0; };
struct Cascade { float size=0; std::vector<Complex> h0; std::vector<float> omega; };
// Arithmetic excerpt from the supplied Waystone adapter; see receipt for source hash.
#include "WoW/Source/WorldClient/HD/Forever/FftKernel.inl"
}
bool ParsePinnedFft(const std::string& text, FftRegistry& output) {
    output={};
    if(text.empty() || text.size()>256*1024) return false;
    FftRegistry parsed;
    std::istringstream stream(text); stream.imbue(std::locale::classic());
    std::string line; bool version=false;
    while(std::getline(stream,line)) {
        std::istringstream row(line); row.imbue(std::locale::classic());
        std::string tag; if(!(row>>tag) || tag[0]=='#')continue;
        int32_t id=0; std::string extra;
        if(tag=="V") {
            int v=0; if(version || !(row>>v) || v!=1 || (row>>extra))return false;
            version=true;
        } else if(tag=="F") {
            FftTile tile;
            if(!(row>>id>>tile.size>>tile.seed>>tile.wind>>tile.gamma))return false;
            for(auto& group:tile.group)for(float& value:group)if(!(row>>value) || !std::isfinite(value))return false;
            if((row>>extra) || id<=0 || !std::isfinite(tile.size) || tile.size<=0 || tile.size>65536 ||
                !std::isfinite(tile.seed) || tile.seed<0 || double(tile.seed)>4294967295.0 ||
                !std::isfinite(tile.wind) || !std::isfinite(tile.gamma) || !parsed.tiles.emplace(id,tile).second)return false;
        } else if(tag=="L") {
            if(!(row>>id) || id<=0)return false;
            std::vector<int32_t> links; std::set<int32_t> unique;
            int32_t tile=0; while(row>>tile){if(tile<=0 || !unique.insert(tile).second)return false;links.push_back(tile);}
            if(!row.eof() || links.empty() || links.size()>4 || !parsed.links.emplace(id,links).second)return false;
        } else return false;
        if(parsed.tiles.size()>1024 || parsed.links.size()>1024)return false;
    }
    if(!version || parsed.tiles.empty() || parsed.links.empty())return false;
    for(const auto& entry:parsed.links)for(int32_t tile:entry.second)if(!parsed.tiles.count(tile))return false;
    output=std::move(parsed);return true;
}
bool EvaluatePinnedFft(const FftRegistry& registry,int32_t profile,double heading,double seconds,float amplitude,FftField& output) {
    output.count=0; output.inverseTile.fill(0.0f);
    if(!std::isfinite(heading) || !std::isfinite(seconds) || seconds<0 || !std::isfinite(amplitude) || amplitude<0 || amplitude>4)return false;
    auto profileIt=registry.links.find(profile);if(profileIt==registry.links.end() || profileIt->second.empty() || profileIt->second.size()>4)return false;
    std::vector<Tile> tiles;
    for(int32_t id:profileIt->second){auto found=registry.tiles.find(id);if(found==registry.tiles.end())return false;tiles.push_back(found->second);}
    std::stable_sort(tiles.begin(),tiles.end(),[](const Tile& a,const Tile& b){return a.size>b.size;});
    for (auto& cascade : output.cascades) {
        if (cascade.size() < 7) cascade.resize(7);
        for (auto& mip : cascade) { mip.width=0; mip.height=0; mip.rgba.clear(); }
    }
    double low=0;
    std::vector<float> data; data.reserve(size_t(kN)*kN*4);
    for(size_t i=0;i<4;++i) {
        data.clear();
        if(i<tiles.size()) {
            const double high=i+1<tiles.size()?4.0*2.0*kPi/tiles[i+1].size:1e9;
            Cascade cascade;BuildCascade(cascade,tiles[i],low,high,heading);low=high;
            // No hourly time reset: discontinuity is not part of the pinned input.
            Evaluate(cascade,seconds,amplitude,data);
            if(!EncodeField(kN,kN,data,true,output.cascades[i]))return false;
            output.inverseTile[i]=1.0f/tiles[i].size;
        } else {
            // Keep every cascade at the reserved dimensions, including unused
            // calm cascades, so output capacity and sampler shape stay stable.
            data.assign(size_t(kN)*kN*4,0.0f);
            for(size_t p=3;p<data.size();p+=4)data[p]=1.0f;
            if(!EncodeField(kN,kN,data,true,output.cascades[i]))return false;
        }
    }
    output.count=uint32_t(tiles.size());return true;
}
}
