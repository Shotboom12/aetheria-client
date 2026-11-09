// HD: hdCataRenderPerf hooks for the event loop and the present. Engine code cannot call the WoW-side
// recorder (other programs link Engine without WoW), so the recorder fills these in; all null = off.
#pragma once
#include <cstdint>

enum EvtPerfPhase : uint32_t {
    EVTPERF_WAIT,    // scheduler sleep before the pass (FPS cap, idle time)
    EVTPERF_UPDATE,  // EVENT_ID_UPDATE handlers
    EVTPERF_TIMER,   // timers
    EVTPERF_INPUT,   // input events
    EVTPERF_POLL,    // EVENT_ID_POLL handlers (network, sound, async files, textures)
    EVTPERF_QUEUE,   // queued events
    EVTPERF_IDLE,    // EVENT_ID_IDLE handlers (world update, UI, spells)
    EVTPERF_PAINT,   // EVENT_ID_PAINT (UI and world drawing, present included)
    EVTPERF_PRESENT, // GxScenePresent
    EVTPERF_COUNT
};

struct EvtPerfHooks {
    uint64_t (*begin)();
    void (*end)(uint32_t phase, uint64_t start);
    void (*loop)(); // start of a main-thread scheduler pass
};

extern EvtPerfHooks g_evtPerf;

inline uint64_t EvtPerfBegin() {
    return g_evtPerf.begin ? g_evtPerf.begin() : 0;
}

inline void EvtPerfEnd(uint32_t phase, uint64_t start) {
    if (start && g_evtPerf.end) {
        g_evtPerf.end(phase, start);
    }
}
