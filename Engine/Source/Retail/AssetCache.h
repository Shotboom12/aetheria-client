#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>

namespace Retail {
// Decoded originals only. Each directory belongs to one immutable build/product/locale.
// Conversion is deliberately excluded so renderer fixes never reuse stale converted models.
class AssetCache {
    std::filesystem::path root_;
    uint64_t limit_ = 0;
    static constexpr uint64_t MaxFile = 128ull * 1024 * 1024;
    static uint32_t Crc(const std::vector<uint8_t>& bytes) {
        static const auto table = [] {
            std::vector<uint32_t> t(256);
            for (uint32_t i=0;i<256;++i) {
                uint32_t c=i;
                for (int b=0;b<8;++b) c=(c>>1)^((c&1)?0xedb88320u:0);
                t[i]=c;
            }
            return t;
        }();
        uint32_t crc=~0u;
        for (auto b:bytes) crc=table[(crc^b)&255]^(crc>>8);
        return ~crc;
    }
    std::filesystem::path File(uint32_t id,bool lenient) const {
        return root_/(std::to_string(id)+(lenient?"-table.rca":".rca"));
    }
public:
    void Configure(std::filesystem::path root,uint64_t limit) { root_=std::move(root);limit_=limit; }
    bool Enabled() const { return !root_.empty() && limit_; }
    bool Read(uint32_t id,bool lenient,std::vector<uint8_t>& bytes) const {
        if (!Enabled() || !id) return false;
        try {
            const auto path=File(id,lenient);
            std::ifstream in(path,std::ios::binary|std::ios::ate);
            if (!in) return false;
            const auto length=in.tellg();
            uint32_t h[6]{};
            if (length<24 || uint64_t(length)>MaxFile+24) return false;
            in.seekg(0);
            if (!in.read(reinterpret_cast<char*>(h),sizeof(h)) || h[0]!=0x31414352 || h[1]!=1 || h[2]!=id
                || h[3]!=uint32_t(lenient) || h[4]!=uint64_t(length)-24) return false;
            std::vector<uint8_t> result(h[4]);
            if (!in.read(reinterpret_cast<char*>(result.data()),result.size()) || Crc(result)!=h[5]) return false;
            bytes.swap(result);
            return true;
        } catch (...) { return false; }
    }
    // Cache exhaustion or a read-only disk never prevents rendering a valid downloaded file.
    bool Write(uint32_t id,bool lenient,const std::vector<uint8_t>& bytes) const {
        if (!Enabled() || !id || bytes.empty() || bytes.size()>MaxFile) return false;
        std::filesystem::path temporary;
        try {
            std::filesystem::create_directories(root_);
            uint64_t used=0;
            for (const auto& entry:std::filesystem::directory_iterator(root_)) {
                if (entry.is_regular_file()) used+=entry.file_size();
                if (used>limit_) return false;
            }
            const auto target=File(id,lenient);
            const uint64_t old=std::filesystem::is_regular_file(target)?std::filesystem::file_size(target):0;
            if (used-old>limit_ || bytes.size()+24>limit_-(used-old)) return false;
            static std::atomic<uint64_t> serial{0};
            temporary=target;
            temporary+="."+std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count())
                +"."+std::to_string(serial++)+".tmp";
            uint32_t h[]={0x31414352,1,id,uint32_t(lenient),uint32_t(bytes.size()),Crc(bytes)};
            {
                std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
                if (!out.write(reinterpret_cast<char*>(h),sizeof(h))
                    || !out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()) || !out.flush()) {
                    out.close(); std::filesystem::remove(temporary); return false;
                }
            }
            // A previously valid file needs no replacement. Invalid entries are repairable.
            std::vector<uint8_t> existing;
            if (Read(id,lenient,existing)) { std::filesystem::remove(temporary); return true; }
            std::error_code ec;
            std::filesystem::remove(target,ec);
            std::filesystem::rename(temporary,target);
            return true;
        } catch (...) {
            std::error_code ec;
            if (!temporary.empty()) std::filesystem::remove(temporary,ec);
            return false;
        }
    }
};
}
