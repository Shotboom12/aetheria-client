#include "Engine/Source/Retail/AssetCache.h"
#include <cstdio>
#include <stdexcept>

static void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    try {
        const std::filesystem::path root=argv[1];
        Check(!std::filesystem::exists(root),"Test directory must be new");
        Retail::AssetCache cache;
        cache.Configure(root,1024);
        std::vector<uint8_t> bytes{1,2,3,4,5},result;
        Check(!cache.Read(42,false,result),"Missing file must miss");
        Check(cache.Write(42,false,bytes),"Write failed");
        Check(cache.Read(42,false,result)&&result==bytes,"Read did not round-trip");
        const auto file=root/"42.rca";
        const auto stamp=std::filesystem::last_write_time(file);
        Check(cache.Write(42,false,bytes),"Repeated write failed");
        Check(std::filesystem::last_write_time(file)==stamp,"Repeated read/write rewrote cached bytes");
        Retail::AssetCache restarted;restarted.Configure(root,1024);
        Check(restarted.Read(42,false,result)&&result==bytes,"New process-equivalent cache missed");
        Check(!cache.Read(42,true,result),"Lenient table data leaked into strict asset cache");
        Check(!cache.Write(43,false,std::vector<uint8_t>(1024,7)),"Budget accepted an oversized entry");
        { std::fstream damaged(file,std::ios::binary|std::ios::in|std::ios::out);damaged.seekp(25);damaged.put(char(99)); }
        Check(!cache.Read(42,false,result),"Damaged checksum accepted");
        Check(cache.Write(42,false,bytes)&&cache.Read(42,false,result)&&result==bytes,"Corrupt entry not repaired");
        std::filesystem::resize_file(file,8);
        Check(!cache.Read(42,false,result),"Truncated header accepted");
        Check(cache.Write(42,false,bytes)&&cache.Read(42,false,result),"Truncated entry not repaired");
        for(const auto& entry:std::filesystem::directory_iterator(root))
            Check(entry.path().extension()==".rca","Temporary file leaked");
        std::puts("PASS: restart reuse, unchanged file timestamps, CRC/truncation rejection and repair, strict/table separation, disk budget, atomic publication cleanup.");
        return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
