// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria: the native owner of Azeroth Remastered's Lumenveil logic (hd/lumen) and of the lamp frame (hd/lamp).
// Azeroth Remastered only (Visuals::AzerothRemastered): in Stock and standalone Lumenveil every call returns at once.
// Render thread, called from WorldFrame at the same boundaries as the other HdNative passes. Nothing here starts the game or a build.
#pragma once
namespace HD::LumenNative {
// Start of the world frame, before anything is drawn: the lamp frame (HD::Lamp::Native::BeginFrame) and the Lumen snapshot
// (HD::LumenBridge::Frame -> Client::Prepare). Starts hd/lumen lazily once the native context is live.
void BeginWorld();
// The opaque world is complete (hd/lumen point AfterOpaqueWorld: prepares the frame; runs the geometry replay that serves the G-buffer).
void AfterOpaque();
// Lamp surfaces / spell lights asked for the G-buffer: WorldFrame keeps the opaque draw capture on and AfterOpaque runs the six-colour geometry replay.
bool GBufferWanted();
// The world is complete, spells not yet drawn (point AfterWorldBeforeSpells: Lumen fog, shafts and lamp halos, composited into the colour).
void World();
// After the spells, before bloom and grade (point AfterWorldBloom: spell lights).
void Bloom();
// Did hd/lumen compose this frame's fog / sun shafts (hdLvFog / hdLvShafts)? Then the reference passes skip theirs.
bool ComposedFog();
bool ComposedShafts();
// Device teardown: stages unregistered, GPU work waited for, images retired. False if a serial is still outstanding.
bool Shutdown();
}
