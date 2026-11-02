// Lists retail race customization from the pinned build's DB2 tables.
// Usage: RetailRaceProbe <RetailCasc.ini> [chrRaceId sex]
#include "Engine/Source/Retail/Appearance.h"
#include "Engine/Source/Retail/Db2.h"
#include "Engine/Source/Retail/SourceContext.h"
#include "Engine/Source/Retail/CascSource.h"
#include "Engine/Source/Retail/RetailModel.h"
#include "Engine/Source/Retail/RaceGeometry.h"
#include "Engine/Source/Retail/Compose.h"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <fstream>
#include <iomanip>
#include <set>

namespace app = Retail::Db2::appearance;

int main(int argc, char** argv) {
    const bool forever = argc > 1 && std::string(argv[1]) == "--forever";
    if (forever) { --argc; ++argv; }
    Retail::SourceScope scope(forever ? Retail::SourceKind::Forever : Retail::SourceKind::Retail);
    if (argc == 3 && std::string(argv[2]) == "human-hair-audit") {
        _putenv_s("WOW_RETAIL_CONFIG", argv[1]);
        if (!app::Status().loaded) return 1;
        uint32_t profiles = 0, pairs = 0, failures = 0;
        std::set<uint32_t> textureFiles;
        const uint32_t black = app::ChoiceSwatchColor(156);
        for (uint32_t i = 0; i < app::ChoiceCount(16); ++i) {
            app::ChrChoice style{}; app::ChoiceAt(16, i, style);
            if (!app::CreationChoiceAllowed(2, style.id)) continue;
            uint32_t count = 0, nearest = 0, distance = UINT32_MAX;
            for (uint32_t j = 0; j < app::ChoiceCount(17); ++j) {
                app::ChrChoice color{}; app::ChoiceAt(17, j, color);
                if (!app::CreationChoiceAllowed(2, color.id) || !app::ChoiceDependencyAllows(color.id, style.id)) continue;
                app::Recipe recipe; const uint32_t selected[] = {style.id, color.id};
                bool ok = app::BuildForCharacter(1, 1, selected, 2, recipe);
                bool material = false;
                for (const auto& layer : recipe.layers) if (layer.textureType == 6) {
                    const auto file = app::MaterialFileDataId(layer.materialResourcesId, 0);
                    material = file != 0;
                    if (file) textureFiles.insert(file);
                }
                if (!ok || !material) ++failures;
                const uint32_t swatch = app::ChoiceSwatchColor(color.id);
                uint32_t squared = 0;
                for (unsigned shift = 0; shift < 24; shift += 8) {
                    const int delta = int((black >> shift) & 255) - int((swatch >> shift) & 255);
                    squared += uint32_t(delta * delta);
                }
                if (squared < distance) { distance = squared; nearest = color.id; }
                ++count; ++pairs;
            }
            if (!count) ++failures;
            std::printf("%s: %u compatible colors, closest black choice %u\n", app::ChoiceName(style.id).c_str(), count, nearest);
            ++profiles;
        }
        if (app::ChoiceDependencyAllows(156, 4980) || app::ChoiceDependencyAllows(156, 4981)) ++failures;
        for (const auto file : textureFiles) {
            std::vector<uint8_t> bytes; Retail::Image image; std::string error;
            if (!Retail::Source().Inspect(Retail::FileName(file, ".blp").c_str(), bytes, error) ||
                !Retail::DecodeBlp(bytes, image, error)) {
                ++failures; std::printf("FAIL original palette file %u: %s\n", file, error.c_str());
            }
        }
        std::printf("Original palette textures decoded and cached: %zu\n", textureFiles.size());
        std::printf("Human hair audit: %u styles, %u valid style/color recipes, %u failures\n", profiles, pairs, failures);
        return failures ? 1 : 0;
    }
    if (argc == 3 && std::string(argv[2]) == "hair-dependencies") {
        _putenv_s(forever ? "WOW_FOREVER_CONFIG" : "WOW_RETAIL_CONFIG", argv[1]);
        static constexpr Retail::Db2::Field fields[] = {{"ChrCustomizationChoiceID"}};
        static constexpr Retail::Db2::Definition def{"chrcustomizationreqchoice",
            "DBFilesClient\\chrcustomizationreqchoice.db2", 0xF925BC6Fu, fields};
        Retail::Db2::Table table; std::string error;
        if (!table.Load(def, &error)) { std::fprintf(stderr, "%s\n",error.c_str()); return 1; }
        for (const auto& row : table.Rows()) if (row.parentId == 41 || row.parentId == 42) {
            const auto id = table.Value(row,"ChrCustomizationChoiceID");
            std::printf("requirement %u choice %u %s\n",row.parentId,id,app::ChoiceName(id).c_str());
        }
        return 0;
    }
    if (argc == 3 && std::string(argv[2]) == "primalist-audit") {
        _putenv_s(forever ? "WOW_FOREVER_CONFIG" : "WOW_RETAIL_CONFIG", argv[1]);
        if (!app::Status().loaded) return 1;
        uint32_t profiles = 0, checked = 0, failed = 0;
        for (uint32_t race = 1; race < 120; ++race) for (uint32_t sex = 0; sex < 2; ++sex) {
            const uint32_t model = app::ChrModelForRace(race, sex);
            if (!model) continue;
            std::vector<app::ChrOption> options;
            std::vector<uint32_t> defaults;
            for (uint32_t i = 0; i < app::OptionCount(model); ++i) {
                app::ChrOption option{}; app::OptionAt(model, i, option);
                options.push_back(option);
                uint32_t first = 0;
                for (uint32_t j = 0; j < app::ChoiceCount(option.id) && !first; ++j) {
                    app::ChrChoice choice{}; app::ChoiceAt(option.id, j, choice);
                    if (app::CreationChoiceAllowed(model, choice.id)) first = choice.id;
                }
                defaults.push_back(first);
            }
            bool hasPrimalist = false;
            for (size_t i = 0; i < options.size(); ++i) for (uint32_t j = 0; j < app::ChoiceCount(options[i].id); ++j) {
                app::ChrChoice choice{}; app::ChoiceAt(options[i].id, j, choice);
                const std::string name = app::ChoiceName(choice.id);
                if (name.find("Primalist - ") != 0 || !app::CreationChoiceAllowed(model, choice.id)) continue;
                hasPrimalist = true;
                const uint16_t expected = name == "Primalist - Earth" ? 1702 : name == "Primalist - Fire" ? 1703
                    : name == "Primalist - Air" ? 1704 : name == "Primalist - Water" ? 1705 : 0;
                auto selected = defaults; selected[i] = choice.id;
                app::Recipe recipe;
                bool ok = app::BuildForCharacter(race, sex, selected.data(), uint32_t(selected.size()), recipe)
                    && std::find(recipe.geosets.begin(), recipe.geosets.end(), expected) != recipe.geosets.end();
                struct Visibility {
                    std::set<uint32_t> effects;
                    void SetGeometryVisible(uint32_t a, uint32_t b, int show) {
                        for (uint32_t id = a; id <= b; ++id) if (show) effects.insert(id); else effects.erase(id);
                    }
                } visible;
                for (bool optionalBlue : {false, true}) {
                    Retail::ApplyRaceEyeGeometry(visible, optionalBlue, expected);
                    ok = ok && visible.effects == std::set<uint32_t>{expected};
                }
                Retail::ApplyRaceEyeGeometry(visible, false, 1700);
                ok = ok && visible.effects.empty();
                std::vector<uint8_t> body, skin; std::string error; Retail::ModelFiles files;
                auto& source = Retail::Source();
                ok = ok && source.Inspect(Retail::FileName(recipe.modelFileDataId, ".m2").c_str(), body, error)
                    && Retail::InspectModel(body, files, error) && !files.skins.empty()
                    && source.Inspect(Retail::FileName(files.skins.front(), ".skin").c_str(), skin, error);
                bool authored = false;
                if (ok && skin.size() >= 36) {
                    uint32_t count, offset; std::memcpy(&count, skin.data() + 28, 4); std::memcpy(&offset, skin.data() + 32, 4);
                    if (offset <= skin.size() && uint64_t(count) * 48 <= skin.size() - offset)
                        for (uint32_t k = 0; k < count; ++k) {
                            uint16_t id; std::memcpy(&id, skin.data() + offset + size_t(k) * 48, 2);
                            authored = authored || id == expected;
                        }
                }
                ok = ok && authored;
                ++checked;
                if (!ok) { ++failed; std::printf("FAIL race %u sex %u choice %u %s expected %u %s\n", race, sex, choice.id, name.c_str(), expected, error.c_str()); }
            }
            if (hasPrimalist) ++profiles;
        }
        std::printf("Primalist audit: %u race/sex profiles, %u palettes, %u failures\n", profiles, checked, failed);
        return failed ? 1 : 0;
    }
    if (argc == 4 && std::string(argv[2]) == "find-choice") {
        _putenv_s(forever ? "WOW_FOREVER_CONFIG" : "WOW_RETAIL_CONFIG", argv[1]);
        if (!app::Status().loaded) return 1;
        const auto* table = app::Get("chrcustomizationchoice");
        for (const auto& row : table->Rows()) {
            const auto name = app::ChoiceName(row.id);
            if (name.find(argv[3]) == std::string::npos) continue;
            const auto option = table->Value(row, "ChrCustomizationOptionID");
            std::printf("choice %u %s; option %u %s; requirement %u\n", row.id, name.c_str(), option,
                app::OptionName(option).c_str(), table->Value(row, "ChrCustomizationReqID"));
        }
        return 0;
    }
    if (argc == 4 && std::string(argv[2]) == "choices") {
        _putenv_s(forever ? "WOW_FOREVER_CONFIG" : "WOW_RETAIL_CONFIG", argv[1]);
        if (!app::Status().loaded) return 1;
        const uint32_t option = uint32_t(std::strtoul(argv[3], nullptr, 10));
        for (uint32_t i = 0; i < app::ChoiceCount(option); ++i) {
            app::ChrChoice choice{};
            if (!app::ChoiceAt(option, i, choice)) return 1;
            std::printf("%u %u %s\n", i, choice.id, app::ChoiceName(choice.id).c_str());
        }
        return 0;
    }
    if (argc == 4 && std::string(argv[2]) == "catalog") {
        _putenv_s(forever ? "WOW_FOREVER_CONFIG" : "WOW_RETAIL_CONFIG", argv[1]);
        if (!app::Status().loaded) return 1;
        std::ofstream out(argv[3]);
        out << "[";
        bool comma = false;
        for (uint32_t race = 1; race <= 120; ++race) for (uint32_t sex = 0; sex < 2; ++sex) {
            const auto model = app::ChrModelForRace(race, sex);
            if (!model) continue;
            std::vector<uint32_t> defaults;
            if (comma) out << ",";
            comma = true;
            out << "\n{\"race\":" << race << ",\"sex\":" << sex << ",\"chrModel\":" << model << ",\"options\":[";
            for (uint32_t i = 0; i < app::OptionCount(model); ++i) {
                app::ChrOption option{};
                if (!app::OptionAt(model, i, option)) return 1;
                if (i) out << ",";
                out << "{\"id\":" << option.id << ",\"name\":" << std::quoted(app::OptionName(option.id)) << ",\"choices\":[";
                for (uint32_t j = 0; j < app::ChoiceCount(option.id); ++j) {
                    app::ChrChoice choice{};
                    if (!app::ChoiceAt(option.id, j, choice)) return 1;
                    if (!j) defaults.push_back(choice.id);
                    if (j) out << ",";
                    out << choice.id;
                }
                out << "]}";
            }
            app::Recipe recipe;
            if (!app::BuildForCharacter(race, sex, defaults.data(), uint32_t(defaults.size()), recipe)) return 1;
            out << "],\"model\":" << recipe.modelFileDataId << ",\"layout\":" << recipe.textureLayoutId << ",\"geosets\":[";
            for (size_t i = 0; i < recipe.geosets.size(); ++i) { if (i) out << ","; out << recipe.geosets[i]; }
            out << "],\"attached\":" << recipe.attached.size() << ",\"collections\":[";
            for (size_t i = 0; i < recipe.attached.size(); ++i) {
                if (i) out << ",";
                out << "{\"file\":" << recipe.attached[i].fileDataId << ",\"geoset\":" << recipe.attached[i].geoset << "}";
            }
            out << "]}";
        }
        out << "\n]\n";
        return out.good() ? 0 : 1;
    }
    if (argc == 5 && std::string(argv[2]) == "audit") {
        _putenv_s(forever ? "WOW_FOREVER_CONFIG" : "WOW_RETAIL_CONFIG", argv[1]);
        if (!app::Status().loaded) return 1;
        const uint32_t race = uint32_t(std::strtoul(argv[3], nullptr, 10));
        const uint32_t sex = uint32_t(std::strtoul(argv[4], nullptr, 10));
        const uint32_t model = app::ChrModelForRace(race, sex);
        if (!model) return 1;
        std::vector<app::ChrOption> options;
        std::vector<uint32_t> defaults;
        for (uint32_t i = 0; i < app::OptionCount(model); ++i) {
            app::ChrOption option{};
            app::ChrChoice choice{};
            if (!app::OptionAt(model, i, option) || !app::ChoiceAt(option.id, 0, choice)) return 1;
            options.push_back(option);
            defaults.push_back(choice.id);
        }
        uint32_t total = 0, failures = 0, attached = 0, missingMaterials = 0;
        std::set<std::pair<uint32_t, uint16_t>> collections;
        for (size_t i = 0; i < options.size(); ++i) {
            uint32_t withAttached = 0;
            for (uint32_t j = 0; j < app::ChoiceCount(options[i].id); ++j) {
                app::ChrChoice choice{};
                if (!app::ChoiceAt(options[i].id, j, choice)) return 1;
                auto picks = defaults;
                picks[i] = choice.id;
                app::Recipe recipe;
                if (!app::BuildForCharacter(race, sex, picks.data(), uint32_t(picks.size()), recipe)
                    || !recipe.modelFileDataId || !recipe.textureLayoutId) ++failures;
                if (!recipe.attached.empty()) { ++withAttached; ++attached; }
                for (const auto& part : recipe.attached) collections.emplace(part.fileDataId, part.geoset);
                for (const auto& layer : recipe.layers)
                    if (!app::MaterialFileDataId(layer.materialResourcesId, 0)) ++missingMaterials;
                ++total;
            }
            std::printf("option %u %s: %u choices, %u recipes with attached models\n", options[i].id,
                app::OptionName(options[i].id).c_str(), app::ChoiceCount(options[i].id), withAttached);
        }
        std::printf("race %u sex %u: %u individual-choice recipes, %u failures, %u with attached models, %u unresolved materials\n",
            race, sex, total, failures, attached, missingMaterials);
        for (const auto& part : collections)
            std::printf("collection file %u geoset %u\n", part.first, part.second);
        return failures || missingMaterials ? 1 : 0;
    }
    if (argc >= 4 && std::string(argv[2]) == "dump") {
        // RetailRaceProbe <ini> dump <table> [field value]: prints rows, optionally those with field == value.
        _putenv_s(forever ? "WOW_FOREVER_CONFIG" : "WOW_RETAIL_CONFIG", argv[1]);
        app::Status();
        if (std::string(argv[3]) == "texturefiledata") app::MaterialFileDataId(0);
        const Retail::Db2::Table* table = app::Get(argv[3]);
        if (!table) { std::fprintf(stderr, "unknown table\n"); return 1; }
        const auto names = table->FieldNames();
        int shown = 0;
        for (const auto& row : table->Rows()) {
            if (argc >= 6) {
                const std::string field = argv[4];
                const std::string want = argv[5];
                const uint32_t have = field == "@parent" ? row.parentId : field == "@id" ? row.id : table->Value(row, field);
                if (have != uint32_t(std::strtoul(want.c_str(), nullptr, 10))) continue;
            }
            std::printf("id %u parent %u:", row.id, row.parentId);
            for (const auto& name : names) {
                std::printf(" %s=", name.c_str());
                for (size_t e = 0; e < table->ElementCount(name); ++e) std::printf("%s%d", e ? "," : "", int(table->Value(row, name, e)));
            }
            std::printf("\n");
            if (++shown >= 200) break;
        }
        return 0;
    }
    if (argc != 2 && argc != 4) {
        std::fprintf(stderr, "Usage: RetailRaceProbe <RetailCasc.ini> [chrRaceId sex|audit chrRaceId sex]\n");
        return 2;
    }
    _putenv_s(forever ? "WOW_FOREVER_CONFIG" : "WOW_RETAIL_CONFIG", argv[1]);
    const app::StoreStatus& status = app::Status();
    std::printf("loaded=%d failed=%d models=%u options=%u choices=%u elements=%u layouts=%u\n",
        status.loaded, status.failed, status.chrModels, status.options, status.choices,
        status.elements, status.textureLayouts);
    if (!status.error.empty()) std::printf("note: %s\n", status.error.c_str());
    if (!status.loaded) return 1;
    for (unsigned race = 1; race < 120; ++race) {
        for (unsigned sex = 0; sex < 2; ++sex) {
            if (argc == 4 && (race != unsigned(std::atoi(argv[2])) || sex != unsigned(std::atoi(argv[3])))) continue;
            const uint32_t model = app::ChrModelForRace(race, sex);
            if (!model) continue;
            const uint32_t options = app::OptionCount(model);
            std::printf("race %u sex %u -> ChrModel %u layout %u, %u options\n", race, sex, model,
                app::LayoutForModel(model), options);
            if (argc != 4) continue;
            std::vector<uint32_t> first;
            for (uint32_t i = 0; i < options; ++i) {
                app::ChrOption option{};
                if (!app::OptionAt(model, i, option)) continue;
                const uint32_t choices = app::ChoiceCount(option.id);
                std::printf("  option %u \"%s\" type %u: %u choices\n", option.id, app::OptionName(option.id).c_str(), option.optionType, choices);
                app::ChrChoice choice{};
                if (choices && app::ChoiceAt(option.id, 0, choice)) first.push_back(choice.id);
            }
            app::Recipe recipe;
            // Match a runtime WOW_RETAIL_CHOICES repro without changing cached DB2 data.
            if (const char* selected = std::getenv("WOW_RETAIL_CHOICES")) {
                first.clear();
                for (const char* p = selected; *p;) {
                    char* end = nullptr;
                    const auto choice = uint32_t(std::strtoul(p, &end, 10));
                    if (end == p) break;
                    first.push_back(choice);
                    p = *end == ',' ? end + 1 : end;
                }
            }
            const bool ok = app::BuildForCharacter(race, sex, first.data(), uint32_t(first.size()), recipe);
            std::printf("recipe(first choices) ok=%d model=%u skeleton=%u layout=%u geosets=%zu layers=%zu attached=%zu\n",
                ok, recipe.modelFileDataId, recipe.skeletonFileDataId, recipe.textureLayoutId, recipe.geosets.size(),
                recipe.layers.size(), recipe.attached.size());
            for (const auto& layer : recipe.layers)
                std::printf("  layer type %u order %u blend %u mask %d material %u -> file %u (usage0) %u (usage1)\n",
                    layer.textureType, layer.layer, layer.blendMode, int(layer.sectionMask), layer.materialResourcesId,
                    app::MaterialFileDataId(layer.materialResourcesId, 0), app::MaterialFileDataId(layer.materialResourcesId, 1));
            for (uint16_t geoset : recipe.geosets) std::printf("  geoset %u\n", geoset);
            for (const auto& part : recipe.attached)
                std::printf("  collection file %u geoset %u%s\n", part.fileDataId, part.geoset, part.geoset % 100 ? "" : " (hidden)");
        }
    }
    return 0;
}
