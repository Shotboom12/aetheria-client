#pragma once
#include <windows.h>
#include <cstdio>

// Bounded diagnostic for the two imported eagle displays. No UI or animation
// changes: record requests and completed cycles to identify the observed switch.
inline void EagleFlightTrace(const char* event, unsigned display, CM2Model* model,
    unsigned requested, unsigned variation, unsigned movement, float speed) {
    if ((display != 200003 && display != 200004) || !model) return;
    static unsigned entries = 0;
    if (entries >= 1024) return;
    ++entries;
    char path[MAX_PATH]{};
    if (!GetModuleFileNameA(nullptr, path, MAX_PATH)) return;
    char* slash = strrchr(path, '\\');
    if (!slash) return;
    strcpy_s(slash + 1, MAX_PATH - (slash - path) - 1, "EsteriaEagleFlight.log");
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") || !file) return;
    fprintf(file, "tick=%llu event=%s display=%u model=%s request=%u variation=%u movement=%08X speed=%.3f",
        static_cast<unsigned long long>(GetTickCount64()), event, display,
        model->m_shared ? model->m_shared->m_fileName : "<pending>",
        requested, variation, movement, speed);
    if (model->IsLoaded(0, 0) && model->m_bones && model->m_shared && model->m_shared->m_data) {
        const auto& bone = model->m_bones[0];
        unsigned index = bone.m_sequence.m_sequenceIndex;
        fprintf(file, " active=%u activeVariation=%u index=%u random=%u playingSpeed=%.3f",
            bone.m_sequenceId, bone.m_variationIndex, index,
            unsigned(bone.m_sequence.m_pickRandomVariation), bone.m_sequence.m_speed);
        if (index < model->m_shared->m_data->m_sequences.m_count) {
            const auto& seq = model->m_shared->m_data->m_sequences[index];
            fprintf(file, " source=%u sourceVariation=%u duration=%u flags=%08X next=%u",
                unsigned(seq.m_id), unsigned(seq.m_variationIndex), seq.m_duration,
                seq.m_flags, unsigned(seq.m_variationNext));
        }
    }
    fprintf(file, "\n");
    fclose(file);
}
