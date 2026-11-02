#include "Engine/Source/Retail/PackedMounts.h"
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <locale>
#include <sstream>
#include <utility>

namespace Retail {
namespace {
std::atomic<PackedReader> reader{nullptr};
bool Unsigned(const std::string& text, uint32_t& value, int base = 10) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return !text.empty() && parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
}
struct State {
    CascSource source{SourceKind::MountStudio};
    PackedMountRegistry registry;
    std::once_flag initialized;
};
State& Mounts() {
    static State state;
    std::call_once(state.initialized, [&] {
        std::vector<uint8_t> bytes;
        uint32_t size = 0;
        std::string error;
        const auto result = ReadPackedFile("EsteriaMounts\\mounts.csv", &bytes, size, error);
        if (result == PackedResult::Missing) return; // No pack: accepted mounts stay unchanged.
        if (result != PackedResult::Ready || !ParsePackedMounts(bytes, state.registry, error) ||
            !state.source.ConfigurePacked(state.registry.product, state.registry.build, state.registry.locale, error)) {
            state.registry = {};
            std::fprintf(stderr, "MountStudio: %s\n", error.c_str());
        }
    });
    return state;
}
}
void SetPackedReader(PackedReader value) { reader.store(value); }
PackedResult ReadPackedFile(const char* name, std::vector<uint8_t>* bytes, uint32_t& size, std::string& error) {
    const auto callback = reader.load();
    return callback ? callback(name, bytes, size, error) : PackedResult::Missing;
}
bool ParsePackedMounts(const std::vector<uint8_t>& bytes, PackedMountRegistry& output, std::string& error) {
    error.clear();
    output = {};
    auto fail = [&] { error = "Invalid, duplicate or mixed-build MountStudio registry"; return false; };
    if (bytes.empty() || bytes.size() > 1024 * 1024) return fail();
    PackedMountRegistry candidate;
    std::istringstream input(std::string(bytes.begin(), bytes.end()));
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fields;
        std::istringstream row(line);
        std::string field;
        while (std::getline(row, field, ';')) fields.push_back(field);
        if (fields.size() != 9 || line.back() == ';') return fail();
        uint32_t display = 0, locale = 0;
        PackedMount mount;
        if (!Unsigned(fields[0], display) || !display || fields[1] != "wow" ||
            fields[2].size() != 32 || fields[2].find_first_not_of("0123456789abcdef") != std::string::npos ||
            !Unsigned(fields[3], locale, 16) || !locale || !Unsigned(fields[4], mount.model) || !mount.model)
            return fail();
        for (unsigned i = 0; i < 3; ++i) if (!Unsigned(fields[5 + i], mount.textures[i])) return fail();
        // Locale-independent decimal parsing; reject NaN, infinities and trailing text.
        std::istringstream scale(fields[8]);
        scale.imbue(std::locale::classic());
        if (!(scale >> mount.scale) || !scale.eof() || !std::isfinite(mount.scale) || mount.scale <= 0 || mount.scale > 100)
            return fail();
        if (candidate.mounts.empty()) {
            candidate.product = fields[1]; candidate.build = fields[2]; candidate.locale = locale;
        } else if (candidate.product != fields[1] || candidate.build != fields[2] || candidate.locale != locale) return fail();
        if (!candidate.mounts.emplace(display, mount).second || candidate.mounts.size() > 4096) return fail();
    }
    if (candidate.mounts.empty()) return fail();
    output = std::move(candidate);
    return true;
}
bool GetPackedMount(uint32_t display, PackedMount& mount) {
    const auto& state = Mounts();
    const auto found = state.registry.mounts.find(display);
    if (found == state.registry.mounts.end()) return false;
    mount = found->second;
    return true;
}
CascSource& MountStudioSource() { return Mounts().source; }
}
