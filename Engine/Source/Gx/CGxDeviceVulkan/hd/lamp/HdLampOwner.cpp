// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: Mado's hd/lamp owner published the "lamp delta" (an isolated real lamp in the material shaders, read by the
// local-shadow receiver) through wxl-vulkan-api's G-buffer and render-pass points. Aetheria's device has neither, so Mado's own
// compiled-out branch (WXL_VK_LAMP_DELTA_CONTRACT undefined) is what runs: no-ops. The lamp frame (hd/lamp/HdLampNative:
// Track, Repair, BeginFrame, Forget) is real and is driven by HdLumenNative, Azeroth Remastered only.
#include "HdLampOwner.h"
namespace HD::Lamp::Owner {bool Init(){return true;}void Prepare(){}bool Shutdown(){return true;}}
