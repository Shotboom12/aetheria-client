#ifndef WOW_COMMON_COMPONENTCORE_NPCGEOMETRY_H
#define WOW_COMMON_COMPONENTCORE_NPCGEOMETRY_H

#include <cstdint>
#include <initializer_list>

// Baked NPCs retain their authored Wrath appearance and clothing. Imported
// character models also contain optional customization groups beyond Wrath's
// visibility reset; those must not all remain enabled on the NPC instance.
template <class Model>
void ApplyBakedNpcGeometry(Model& model, bool bakedNpc, bool retailModel,
    bool bootsEquipped, bool hasClosedFoot) {
    if (!bakedNpc || retailModel) return;
    model.SetGeometryVisible(2001, 65535, 0);
    for (uint16_t base : {2001, 2201, 2301, 3202, 3301, 5101})
        model.SetGeometryVisible(base, base, 1);
    if (bootsEquipped && hasClosedFoot) {
        model.SetGeometryVisible(2001, 2001, 0);
        model.SetGeometryVisible(2002, 2002, 1);
    }
}

#endif
