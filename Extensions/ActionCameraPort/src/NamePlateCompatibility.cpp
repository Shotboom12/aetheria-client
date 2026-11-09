#include "WoW/Source/Extension/ScriptDefines/LuaScript.h"

#include "Engine/Source/lua-5.1/src/lua.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/Ui/NamePlateFrame.h"
#include "WoW/Source/Ui/ScriptEvents.h"

namespace {

int GetNamePlateForUnit(lua_State* state) {
    const char* token = lua_type(state, 1) == LUA_TSTRING ? lua_tostring(state, 1) : nullptr;
    CGUnit_C* unit = token && *token ? GetUnitFromName(token) : nullptr;
    CGNamePlateFrame* plate = unit ? unit->m_namePlateFrame : nullptr;
    if (!plate || !plate->m_isVisible) {
        lua_pushnil(state);
        return 1;
    }

    if (!plate->m_luaRegistered) {
        plate->RegisterScriptObject(nullptr);
    }
    lua_rawgeti(state, LUA_REGISTRYINDEX, plate->m_luaObjectRef);
    return 1;
}

class NamePlateCompatibility : public LuaScript {
public:
    NamePlateCompatibility() : LuaScript("ActionCameraNamePlateCompatibility") {}

    void OnFrameScriptInitialized(lua_State* state, FrameScriptContext context) override {
        if (context != FRAMESCRIPT_CONTEXT_GAME) {
            return;
        }

        lua_getglobal(state, "C_NamePlate");
        if (lua_isnil(state, -1)) {
            lua_pop(state, 1);
            lua_newtable(state);
            lua_pushvalue(state, -1);
            lua_setglobal(state, "C_NamePlate");
        }
        if (lua_istable(state, -1)) {
            lua_pushliteral(state, "GetNamePlateForUnit");
            lua_rawget(state, -2);
            const bool missing = lua_isnil(state, -1);
            lua_pop(state, 1);
            if (missing) {
                lua_pushliteral(state, "GetNamePlateForUnit");
                lua_pushcfunction(state, GetNamePlateForUnit);
                lua_rawset(state, -3);
            }
        }
        lua_pop(state, 1);
    }
};

}

void AddActionCameraNamePlateCompatibility() {
    new NamePlateCompatibility();
}
