// SPDX-License-Identifier: GPL-3.0-or-later
// HD 2026-11-02, kit commun R1 lot L1: the native snapshot handed to Lumenveil R2 once per world frame.
// Reads only data the client already computed (DayNight, Forever fog, weather, Cata water plane); writes nothing back.
#pragma once
namespace HD::LumenBridge {
// Render thread, WorldPass, after FSR/DLSS chose the render size and the HD effects' Resize, before the world draw.
// Does nothing unless a hdLv* case is on (Client::Prepare returns at once when nothing is asked).
void Frame();
}
