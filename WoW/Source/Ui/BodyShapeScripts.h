#ifndef WOW_UI_BODY_SHAPE_SCRIPTS_H
#define WOW_UI_BODY_SHAPE_SCRIPTS_H

// Lua entry points for the body sliders. Included by both script tables (in-game UI and glue/creator), so each
// file registers the same names: SetBodyShape, GetBodyShape, GetBodyShapeNames, GetBodyShapeSupport,
// GetBodyShapeRange, ResetBodyShape, BodyShapeRandomize, BodyShapeProbe, BodyShapeBoneInfo, BodyShapeRegion,
// BodyShapeMeshStatus.

#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Retail/BodyShape.h"
#include "Engine/Source/Retail/BodyShapeMesh.h"
#include "Engine/Source/Os/OsClipboard.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Body sliders. Values run -1..1; 0 leaves the model untouched. They edit the creator preview or the local player.
static int32_t Script_SetBodyShape(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: SetBodyShape(\"name\", value)");
        return 0;
    }

    lua_pushboolean(L, BodyShape::Set(lua_tolstring(L, 1, nullptr), static_cast<float>(lua_tonumber(L, 2))));
    return 1;
}

static int32_t Script_GetBodyShape(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: GetBodyShape(\"name\")");
        return 0;
    }

    lua_pushnumber(L, BodyShape::Get(lua_tolstring(L, 1, nullptr)));
    return 1;
}

// GetBodyShapeNames(["body" | "face"]) -> slider names, optionally one group only.
static int32_t Script_GetBodyShapeNames(lua_State* L) {
    const char* group = lua_isstring(L, 1) ? lua_tolstring(L, 1, nullptr) : nullptr;
    int32_t count = 0;

    for (const BodyShape::Slider& slider : BodyShape::Sliders()) {
        if (group && (slider.group == BodyShape::FaceGroup) != (strcmp(group, "face") == 0)) {
            continue;
        }

        lua_pushstring(L, slider.name);
        ++count;
    }

    return count;
}

// BodyShapeRegion("Brow") -> the model bone indices that face region moves (top-most bones only).
static int32_t Script_BodyShapeRegion(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        return 0;
    }

    CM2Model* model = BodyShape::Edited().model;

    if (!model) {
        return 0;
    }

    BodyShape::State& state = BodyShape::For(model);
    auto it = state.regions.find(lua_tolstring(L, 1, nullptr));

    if (it == state.regions.end()) {
        return 0;
    }

    for (const BodyShape::RegionBone& bone : it->second.bones) {
        lua_pushnumber(L, bone.index);
    }

    return static_cast<int32_t>(it->second.bones.size());
}

// GetBodyShapeSupport("name") -> true / false, or nil while no model has loaded yet.
static int32_t Script_GetBodyShapeSupport(lua_State* L) {
    int32_t support = lua_isstring(L, 1) ? BodyShape::Supported(lua_tolstring(L, 1, nullptr)) : -1;

    if (support < 0) {
        lua_pushnil(L);
    } else {
        lua_pushboolean(L, support);
    }

    return 1;
}

// GetBodyShapeRange("name") -> min, max.
static int32_t Script_GetBodyShapeRange(lua_State* L) {
    int32_t found = lua_isstring(L, 1) ? BodyShape::FindSlider(lua_tolstring(L, 1, nullptr)) : -1;

    if (found < 0) {
        return 0;
    }

    lua_pushnumber(L, BodyShape::Sliders()[found].minValue);
    lua_pushnumber(L, 1.0);
    return 2;
}

// BodyShapeMeshStatus() -> searched, profileName, vertexCount, active. For diagnosing the breast/butt sliders.
static int32_t Script_BodyShapeMeshStatus(lua_State* L) {
    CM2Model* model = BodyShape::Edited().model;
    BodyShape::State empty;
    BodyShape::State& state = model ? BodyShape::For(model) : empty;
    lua_pushboolean(L, state.meshSearched);
    lua_pushstring(L, BodyShapeMesh::Name(state.meshProfile));
    lua_pushnumber(L, static_cast<double>(state.bind.size() / 6));
    lua_pushboolean(L, model && model->m_bodyShapeVertices != nullptr);
    return 4;
}

// JiggleSet(name, value) -> value. Names: enabled (0/1), breastHz, breastZeta, breastGain, breastMax, breastSide, breastRotate, breastDetune, breastUp, worldMix, turnMix,
// buttHz, buttZeta, buttGain, buttMax. JiggleSet() alone lists nothing and returns the enabled flag.
// JiggleStatus() -> enabled, breastBonesFound (0/1), breastBoneA, breastBoneB, pelvisFound (0/1) for the model last driven.
static int32_t Script_JiggleSet(lua_State* L) {
    JigglePhysics::Settings& cfg = JigglePhysics::Config();
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushnumber(L, cfg.enabled ? 1.0 : 0.0);
        return 1;
    }

    const char* name = lua_tolstring(L, 1, nullptr);
    const float value = static_cast<float>(lua_tonumber(L, 2));
    struct Entry { const char* name; float* target; };
    const Entry entries[] = {
        { "breastHz", &cfg.breastHz }, { "breastZeta", &cfg.breastZeta }, { "breastGain", &cfg.breastGain },
        { "breastMax", &cfg.breastMax }, { "breastSide", &cfg.breastSide }, { "worldMix", &cfg.worldMix }, { "turnMix", &cfg.turnMix }, { "breastRotate", &cfg.breastRotate }, { "breastDetune", &cfg.breastDetune }, { "breastUp", &cfg.breastUp }, { "buttHz", &cfg.buttHz },
        { "buttZeta", &cfg.buttZeta }, { "buttGain", &cfg.buttGain }, { "buttMax", &cfg.buttMax },
    };
    if (!strcmp(name, "enabled")) {
        cfg.enabled = value != 0.0f;
        lua_pushnumber(L, cfg.enabled ? 1.0 : 0.0);
        return 1;
    }
    for (const Entry& entry : entries) {
        if (!strcmp(name, entry.name)) {
            *entry.target = value;
            lua_pushnumber(L, value);
            return 1;
        }
    }
    luaL_error(L, "Unknown jiggle setting %s", name);
    return 0;
}

static int32_t Script_JiggleStatus(lua_State* L) {
    CM2Model* model = BodyShape::Edited().model;
    JigglePhysics::State empty;
    JigglePhysics::State& state = model ? BodyShape::For(model).jiggle : empty;
    lua_pushboolean(L, JigglePhysics::Active());
    lua_pushboolean(L, state.hasBreasts);
    lua_pushnumber(L, state.hasBreasts ? static_cast<double>(state.breastBone[0]) : -1.0);
    lua_pushnumber(L, state.hasBreasts ? static_cast<double>(state.breastBone[1]) : -1.0);
    lua_pushboolean(L, state.hasPelvis);
    return 5;
}

// CapeSet(name, value) -> value, for live tuning of CapePhysics.h. Names: enabled (0/1), zeta, gain, heightPower, maxBack, maxForward, maxSide,
// windMix, windMax, worldMix, turnMix, flare, customFlare, hz1..hz5. CapeSet() alone returns the enabled flag.
// CapeStatus() -> active, chainFound (0/1), first cape bone index for the player model last driven (-1 if none).
static int32_t Script_CapeSet(lua_State* L) {
    CapePhysics::Settings& cfg = CapePhysics::Config();
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2)) {
        lua_pushnumber(L, cfg.enabled ? 1.0 : 0.0);
        return 1;
    }

    const char* name = lua_tolstring(L, 1, nullptr);
    const float value = static_cast<float>(lua_tonumber(L, 2));
    struct Entry { const char* name; float* target; };
    const Entry entries[] = {
        { "zeta", &cfg.zeta }, { "gain", &cfg.gain }, { "heightPower", &cfg.heightPower }, { "maxBack", &cfg.maxBack },
        { "maxForward", &cfg.maxForward }, { "maxSide", &cfg.maxSide }, { "windMix", &cfg.windMix }, { "windMax", &cfg.windMax },
        { "worldMix", &cfg.worldMix }, { "turnMix", &cfg.turnMix }, { "flare", &cfg.flare }, { "customFlare", &cfg.customFlare },
        { "hz1", &cfg.hz[0] }, { "hz2", &cfg.hz[1] }, { "hz3", &cfg.hz[2] }, { "hz4", &cfg.hz[3] }, { "hz5", &cfg.hz[4] },
    };
    if (!strcmp(name, "enabled")) {
        cfg.enabled = value != 0.0f;
        lua_pushnumber(L, cfg.enabled ? 1.0 : 0.0);
        return 1;
    }
    for (const Entry& entry : entries) {
        if (!strcmp(name, entry.name)) {
            *entry.target = value;
            lua_pushnumber(L, value);
            return 1;
        }
    }
    luaL_error(L, "Unknown cape setting %s", name);
    return 0;
}

static int32_t Script_CapeStatus(lua_State* L) {
    CM2Model* model = BodyShape::Edited().model;
    auto it = model ? BodyShape::CapeStates().find(model) : BodyShape::CapeStates().end();
    const bool found = it != BodyShape::CapeStates().end() && it->second.found;
    lua_pushboolean(L, CapePhysics::Active());
    lua_pushboolean(L, found);
    lua_pushnumber(L, found ? static_cast<double>(it->second.bone[0]) : -1.0);
    return 3;
}

static int32_t Script_ResetBodyShape(lua_State* L) {
    BodyShape::Reset();
    return 0;
}

// BodyShapeProbe(index, mirror, sx, sy, sz, tx, ty, tz): scale and move one raw bone (and its mirror partner).
// index < 0 turns the probe off.
static int32_t Script_BodyShapeProbe(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: BodyShapeProbe(index, mirror, sx, sy, sz, tx, ty, tz)");
        return 0;
    }

    float scale[3] = { 1.0f, 1.0f, 1.0f };
    float move[3] = { 0.0f, 0.0f, 0.0f };

    for (int32_t i = 0; i < 3; ++i) {
        if (lua_isnumber(L, 3 + i)) {
            scale[i] = static_cast<float>(lua_tonumber(L, 3 + i));
        }
        if (lua_isnumber(L, 6 + i)) {
            move[i] = static_cast<float>(lua_tonumber(L, 6 + i));
        }
    }

    BodyShape::SetProbe(static_cast<int32_t>(lua_tonumber(L, 1)), lua_toboolean(L, 2) != 0, scale, move);
    return 0;
}

// BodyShapeBoneInfo(index) -> keyId, parentIndex, x, y, z; with no argument -> boneCount. Nothing past the end.
static int32_t Script_BodyShapeBoneInfo(lua_State* L) {
    CM2Model* model = BodyShape::Edited().model;

    if (!model || !model->IsLoaded()) {
        return 0;
    }

    if (!lua_isnumber(L, 1)) {
        lua_pushnumber(L, model->GetBoneCount());
        return 1;
    }

    int32_t key, parent;
    C3Vector pivot;

    if (!model->GetBoneInfoByIndex(static_cast<uint32_t>(lua_tonumber(L, 1)), key, parent, pivot)) {
        return 0;
    }

    lua_pushnumber(L, key);
    lua_pushnumber(L, parent);
    lua_pushnumber(L, pivot.x);
    lua_pushnumber(L, pivot.y);
    lua_pushnumber(L, pivot.z);
    return 5;
}

// BodyShapeLog("text"): appends a line to Logs\BodyShape.log, so the creator Lua can report what it sees.
static int32_t Script_BodyShapeLog(lua_State* L) {
    if (lua_isstring(L, 1)) {
        BodyShape::Log(lua_tolstring(L, 1, nullptr));
    }

    return 0;
}

// BodyShapeRandomize(): random values for every slider the current model supports.
static int32_t Script_BodyShapeRandomize(lua_State* L) {
    BodyShape::Randomize();
    return 0;
}

// BodyShapeCopyCode(): copy a bounded, versioned slider string to the system clipboard.
static int32_t Script_BodyShapeCopyCode(lua_State* L) {
    char code[4096] = "BS1;";
    size_t used = 4;

    for (BodyShape::Slider const& slider : BodyShape::Sliders()) {
        if (!BodyShape::Supported(slider.name)) {
            continue;
        }

        int32_t written = std::snprintf(code + used, sizeof(code) - used, "%s%s=%.3f",
            used == 4 ? "" : ";", slider.name, BodyShape::Get(slider.name));
        if (written < 0 || static_cast<size_t>(written) >= sizeof(code) - used) {
            lua_pushboolean(L, false);
            return 1;
        }

        used += static_cast<size_t>(written);
    }

    lua_pushboolean(L, OsClipboardPutString(code) != 0);
    return 1;
}

// BodyShapePasteCode(): import a versioned clipboard string; BodyShape::Set validates slider names/ranges.
static int32_t Script_BodyShapePasteCode(lua_State* L) {
    char* code = OsClipboardGetString();
    if (!code) {
        lua_pushinteger(L, 0);
        return 1;
    }

    int32_t applied = 0;
    size_t const length = std::strlen(code);
    if (length <= 4096 && !std::strncmp(code, "BS1;", 4)) {
        char* cursor = code + 4;
        char* const end = code + length;
        while (cursor < end) {
            char* const separator = std::strchr(cursor, ';');
            char* const tokenEnd = separator ? separator : end;
            char* const equals = static_cast<char*>(std::memchr(cursor, '=', static_cast<size_t>(tokenEnd - cursor)));
            if (equals && equals > cursor && equals - cursor < 64) {
                char name[64];
                size_t const nameLength = static_cast<size_t>(equals - cursor);
                std::memcpy(name, cursor, nameLength);
                name[nameLength] = '\0';
                char* valueEnd = nullptr;
                float const value = std::strtof(equals + 1, &valueEnd);
                if (valueEnd == tokenEnd && BodyShape::Set(name, value)) {
                    ++applied;
                }
            }
            cursor = separator ? separator + 1 : end;
        }
    }

    OsClipboardFreeString(code);
    lua_pushinteger(L, applied);
    return 1;
}

#define BODY_SHAPE_SCRIPT_ENTRIES \
    { "SetBodyShape", &Script_SetBodyShape }, \
    { "GetBodyShape", &Script_GetBodyShape }, \
    { "GetBodyShapeNames", &Script_GetBodyShapeNames }, \
    { "ResetBodyShape", &Script_ResetBodyShape }, \
    { "BodyShapeRandomize", &Script_BodyShapeRandomize }, \
    { "GetBodyShapeSupport", &Script_GetBodyShapeSupport }, \
    { "BodyShapeRegion", &Script_BodyShapeRegion }, \
    { "GetBodyShapeRange", &Script_GetBodyShapeRange }, \
    { "BodyShapeMeshStatus", &Script_BodyShapeMeshStatus }, \
    { "JiggleSet", &Script_JiggleSet }, \
    { "JiggleStatus", &Script_JiggleStatus },     { "CapeSet", &Script_CapeSet },     { "CapeStatus", &Script_CapeStatus }, \
    { "BodyShapeProbe", &Script_BodyShapeProbe }, \
    { "BodyShapeBoneInfo", &Script_BodyShapeBoneInfo }, \
    { "BodyShapeLog", &Script_BodyShapeLog }, \
    { "BodyShapeCopyCode", &Script_BodyShapeCopyCode }, \
    { "BodyShapePasteCode", &Script_BodyShapePasteCode },

#endif
