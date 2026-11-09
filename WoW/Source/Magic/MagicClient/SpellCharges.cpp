#include "Spell_C.h"
#include "WoW/Common/ComponentCore/EsteriaAppearance.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/WorldClient/CustomTable.h"
#include "WoW/Source/Ui/ActionBarFrame.h"
#include "WoW/Source/Ui/ScriptEvents.h"
#include "WoW/Source/Ui/SpellBookFrame.h"
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/TimeManager.h"
#include <algorithm>
#include <tuple>
#include <unordered_map>

// Esteria: Conquest of Azeroth's spell charges, relocated to Esteria's opcode range with the same wire layout. State is
// keyed by SpellChargesCategory id; SpellCharges.dbc (col 0 spell, col 1 category) and SpellChargesCategory.dbc (col 0
// id, col 1 max charges, col 2 recharge ms) are read from the archives on first use. Times are OsGetAsyncTimeMs.
// Nothing here runs until one of these messages arrives: without them the map stays empty.
static const uint32_t COA_SPELL_CHARGES_CLEAR = 0x531;  // no payload: every entry dropped
static const uint32_t COA_SPELL_CHARGES_REMOVE = 0x532; // u32 category: that entry dropped
static const uint32_t COA_SPELL_CHARGES_LIST = 0x533;   // u32 n, n x {u32 category, u32 remaining ms, u8 used, u32 recovery ms}
static const uint32_t COA_SPELL_CHARGES_SET = 0x534;    // u32 category, u32 remaining ms, u32 used, u32 recovery ms (0 used drops it)

struct SpellChargeState {
    uint32_t used = 0;       // charges spent (the client shows max - used)
    uint32_t next = 0;       // when the next charge comes back
    uint32_t start = 0;      // when the running recharge started
    uint32_t duration = 0;   // the category's recharge, reused after each returned charge
};

struct SpellChargeCategory {
    uint32_t max = 0;
    uint32_t duration = 0;
};

static std::unordered_map<uint32_t, SpellChargeState> s_spellCharges;
static std::unordered_map<uint32_t, uint32_t> s_spellChargeLinks;   // spell -> category
static std::unordered_map<uint32_t, SpellChargeCategory> s_spellChargeCategories;

static void LoadSpellChargeTables() {
    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;
    char status[256] = {};
    CustomTable::Table table;

    if (CustomTable::LoadTable("DBFilesClient\\SpellChargesCategory.dbc", 3, table, status, sizeof(status))) {
        for (uint32_t row = 0; row < table.Rows(); row++) {
            uint32_t maximum = table.U(row, 1);
            uint32_t recovery = table.U(row, 2);
            if (maximum && maximum <= 20 && recovery && recovery <= 86400000) {
                s_spellChargeCategories[table.U(row, 0)] = { maximum, recovery };
            }
        }
    }

    EsteriaTrace("spellcharges: SpellChargesCategory.dbc %s", status);
    table = CustomTable::Table();

    if (CustomTable::LoadTable("DBFilesClient\\SpellCharges.dbc", 2, table, status, sizeof(status))) {
        for (uint32_t row = 0; row < table.Rows(); row++) {
            s_spellChargeLinks[table.U(row, 0)] = table.U(row, 1);
        }
    }

    EsteriaTrace("spellcharges: SpellCharges.dbc %s", status);
}

// A category with a row and a non-zero max, as CoA requires before it keeps an entry.
static const SpellChargeCategory* FindSpellChargeCategory(uint32_t category) {
    LoadSpellChargeTables();
    auto it = s_spellChargeCategories.find(category);
    return it != s_spellChargeCategories.end() && it->second.max ? &it->second : nullptr;
}

int32_t Spell_C_GetSpellChargeCount(int32_t spellId) {
    LoadSpellChargeTables();
    auto link = s_spellChargeLinks.find(static_cast<uint32_t>(spellId));
    const SpellChargeCategory* category = link == s_spellChargeLinks.end() ? nullptr : FindSpellChargeCategory(link->second);

    if (!category) {
        return -1;
    }

    auto state = s_spellCharges.find(link->second);
    uint32_t used = state == s_spellCharges.end() ? 0 : state->second.used;
    return static_cast<int32_t>(category->max - std::min(category->max, used));
}

static uint32_t SpellChargeBytesLeft(CDataStore* msg) {
    return msg->m_read <= msg->m_size ? msg->m_size - msg->m_read : 0;
}

// SPELL_UPDATE_CHARGES (CoA's event) while the game UI exists; the stock cooldown events too, so the stock action bars
// and spell book redraw the charge cooldown Spell_C_GetSpellCooldown now reports.
static void SpellChargesUpdated() {
    uint32_t event = sExtensionMgr->GetFrameScriptEventIndex("SPELL_UPDATE_CHARGES");

    if (event == UINT32_MAX) {
        return;
    }

    FrameScript_SignalEvent(event, nullptr);
    CGSpellBook::UpdateCooldowns();
    for (uint32_t slot = 0; slot < 144; ++slot) {
        int32_t petSpell;
        int32_t spellId = CGActionBar::GetSpell(slot, petSpell);
        if (!spellId || petSpell || CGActionBar::GetItem(slot) || Spell_C_GetSpellChargeCount(spellId) < 0) {
            continue;
        }
        CGActionBar::m_slotItemCount[slot] = Spell_C_GetSpellChargeCount(spellId);
        FrameScript_SignalEvent(SCRIPT_EVENT_ACTIONBAR_SLOT_CHANGED, "%d", slot + 1);
    }
    CGActionBar::SignalUpdateCooldown();
}


static int32_t Packet_COA_SPELL_CHARGES(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t id = static_cast<uint32_t>(msgId);

    if (id == COA_SPELL_CHARGES_CLEAR) {
        s_spellCharges.clear();
    } else if (id == COA_SPELL_CHARGES_REMOVE) {
        if (SpellChargeBytesLeft(msg) < 4) {
            return 1;
        }

        uint32_t category;
        msg->Get(category);
        s_spellCharges.erase(category);
    } else if (id == COA_SPELL_CHARGES_LIST) {
        if (SpellChargeBytesLeft(msg) < 4) {
            return 1;
        }

        uint32_t count;
        msg->Get(count);

        // CoA stops when the packet is used up; a cut-off entry is not read.
        for (; count && SpellChargeBytesLeft(msg) >= 13; count--) {
            uint32_t category;
            uint32_t remaining;
            uint8_t used;
            uint32_t recovery;
            msg->Get(category);
            msg->Get(remaining);
            msg->Get(used);
            msg->Get(recovery);
            const SpellChargeCategory* row = FindSpellChargeCategory(category);

            if (!row || !remaining || remaining > 86400000 || !recovery || recovery > 86400000 || used > row->max) {
                continue;
            }

            uint32_t now = OsGetAsyncTimeMs();
            SpellChargeState& state = s_spellCharges[category];
            state.used = used;
            state.next = now + static_cast<uint32_t>(remaining);
            state.start = state.next - recovery;
            state.duration = recovery;
        }
    } else {
        if (SpellChargeBytesLeft(msg) < 16) {
            return 1;
        }

        uint32_t category;
        uint32_t ms;
        uint32_t used;
        uint32_t recovery;
        msg->Get(category);
        msg->Get(ms);
        msg->Get(used);
        msg->Get(recovery);
        const SpellChargeCategory* row = FindSpellChargeCategory(category);

        // CoA signals nothing for an unknown category or a zero time.
        if (!row || !ms || ms > 86400000 || !recovery || recovery > 86400000 || used > row->max) {
            return 1;
        }

        uint32_t now = OsGetAsyncTimeMs();
        auto it = s_spellCharges.find(category);

        if (!used) {
            s_spellCharges.erase(category);
        } else if (it == s_spellCharges.end()) {
            SpellChargeState& state = s_spellCharges[category];
            state.used = used;
            state.next = now + ms;
            state.start = state.next - recovery;
            state.duration = recovery;
        } else {
            // The server supplies both progress and the actual interval, including cooldown modifiers.
            it->second.used = used;
            it->second.next = now + ms;
            it->second.duration = recovery;
            it->second.start = it->second.next - recovery;
        }
    }

    SpellChargesUpdated();

    return 1;
}

// Every idle tick in game: an entry whose recharge has ended gets a charge back, and the last one removes it.
void Spell_C_UpdateSpellCharges() {
    uint32_t now = OsGetAsyncTimeMs();

    for (auto it = s_spellCharges.begin(); it != s_spellCharges.end();) {
        if (static_cast<int32_t>(now - it->second.next) < 0) {
            ++it;
            continue;
        }

        uint32_t elapsed = now - it->second.next;
        uint32_t recovered = it->second.duration ? 1 + elapsed / it->second.duration : it->second.used;
        if (recovered >= it->second.used) {
            it = s_spellCharges.erase(it);
        } else {
            it->second.used -= recovered;
            it->second.next += recovered * it->second.duration;
            it->second.start = it->second.next - it->second.duration;
            ++it;
        }

        SpellChargesUpdated();
    }
}

// The recharge of a spell whose charges are all spent; false while it has one left or has no charge state.
static bool GetSpentSpellCharges(int32_t spellId, uint32_t& start, uint32_t& duration) {
    auto link = s_spellChargeLinks.find(static_cast<uint32_t>(spellId));

    if (link == s_spellChargeLinks.end()) {
        return false;
    }

    auto it = s_spellCharges.find(link->second);
    auto category = s_spellChargeCategories.find(link->second);

    if (it == s_spellCharges.end() || category == s_spellChargeCategories.end() || it->second.used < category->second.max) {
        return false;
    }

    start = it->second.start;
    duration = it->second.next - it->second.start;
    return true;
}

// CoA's GetSpellCharges(spell): charges left, max charges, recharge start and duration (seconds). Without an entry: max,
// max, 0 and the category's recharge; 0, 0, 0, 0 for a non-number or a spell without charges.
static int32_t Script_GetSpellCharges(lua_State* L) {
    if (lua_isnumber(L, 1)) {
        LoadSpellChargeTables();
        auto link = s_spellChargeLinks.find(static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 1))));
        auto category = link != s_spellChargeLinks.end() ? s_spellChargeCategories.find(link->second) : s_spellChargeCategories.end();

        if (category != s_spellChargeCategories.end()) {
            int32_t max = static_cast<int32_t>(category->second.max);
            auto it = s_spellCharges.find(category->first);
            uint32_t duration;

            if (it == s_spellCharges.end()) {
                lua_pushnumber(L, max);
                lua_pushnumber(L, max);
                lua_pushnumber(L, 0.0);
                duration = category->second.duration;
            } else {
                lua_pushnumber(L, max - static_cast<int32_t>(it->second.used));
                lua_pushnumber(L, max);
                lua_pushnumber(L, static_cast<double>(it->second.start) * 0.001);
                duration = it->second.next - it->second.start;
            }

            // CoA converts the duration as unsigned.
            lua_pushnumber(L, static_cast<double>(duration) * 0.001);
            return 4;
        }
    }

    for (int32_t i = 0; i < 4; i++) {
        lua_pushnumber(L, 0);
    }

    return 4;
}

// Startup (after the extensions load, before any FrameScript state): the Lua function and event are recorded once and
// put into every game state the client builds.
void Spell_C_RegisterSpellChargeScripts() {
    sExtensionMgr->RegisterLuaFunction("GetSpellCharges", Script_GetSpellCharges, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("SPELL_UPDATE_CHARGES", FRAMESCRIPT_CONTEXTS_GAME);
}

bool Spell_C_GetSpentSpellChargeCooldown(int32_t spellId, uint32_t& start, uint32_t& duration) {
    return GetSpentSpellCharges(spellId, start, duration);
}

void Spell_C_RegisterSpellChargeMessageHandlers() {
    sExtensionMgr->RegisterMessageHandler(COA_SPELL_CHARGES_CLEAR, Packet_COA_SPELL_CHARGES, nullptr);
    sExtensionMgr->RegisterMessageHandler(COA_SPELL_CHARGES_REMOVE, Packet_COA_SPELL_CHARGES, nullptr);
    sExtensionMgr->RegisterMessageHandler(COA_SPELL_CHARGES_LIST, Packet_COA_SPELL_CHARGES, nullptr);
    sExtensionMgr->RegisterMessageHandler(COA_SPELL_CHARGES_SET, Packet_COA_SPELL_CHARGES, nullptr);
}

void Spell_C_ClearSpellCharges() {
    s_spellCharges.clear();
}



