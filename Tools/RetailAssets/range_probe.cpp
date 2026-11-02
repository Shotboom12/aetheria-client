#include "Engine/Source/Retail/CdnRange.h"
#include <cstdlib>
#include <fstream>
#include <filesystem>
int main(int argc,char** argv) {
    if(argc!=5)return 2;
    const std::string input=argv[1];
    const std::wstring url(input.begin(),input.end());
    std::vector<uint8_t> bytes;std::string error;
    if(!Retail::ReadCdnRange(url,std::strtoull(argv[2],nullptr,10),uint32_t(std::strtoul(argv[3],nullptr,10)),bytes,error)) {
        std::fprintf(stderr,"%s\n",error.c_str());return 1;
    }
    if(std::filesystem::exists(argv[4]))return 2;
    std::ofstream out(argv[4],std::ios::binary);
    if(!out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()))return 1;
    std::printf("Exact CDN range: %zu bytes\n",bytes.size());return 0;
}
