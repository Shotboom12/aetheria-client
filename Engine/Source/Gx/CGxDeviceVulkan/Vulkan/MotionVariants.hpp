// SPDX-License-Identifier: GPL-3.0-or-later
// Aetheria FSR object motion, step 2: the velocity shader variants Mado's Azeroth Remastered pack ships (additive overlays on a translated
// manifest, keyed by the md5 of the original D3D9 shader bytes, the same key as manifest::Record::md5). They are read from
// <manifest folder>/velocity/velocity-index.txt, written when the variants are installed:
//
//   <kind> <md5> <relative path 1> <relative path 2>      (paths relative to the manifest folder, forward slashes)
//   kind: vel velbones skinbones wind windbones grass (Mado's, vertex) | velfrag velzero (Mado's, pixel)
//         ownvel: OUR velocity variant of one of our own skinned vertex modules (tools/velocity_variants.py, spirv-val clean): same register
//         layout, current clip position at location 16, previous clip position (from a second register file at set 2 binding 0) at 17.
//   The velocity fragment shader is built in (MotionVelocityFrag.spv.h).
//
// Nothing here creates a Vulkan object; shaders::Get(path) makes a module when a velocity pipeline asks for one. Render thread.
#pragma once
#include <cstdint>

namespace gxvk::vk::motionvariants
{
    enum class Kind : uint8_t { Rigid, Bones, SkinBones, Wind, WindBones, Grass, PixelVelocity, PixelZero, OwnVelocity, Count };

    struct Stats { uint32_t perKind[uint32_t(Kind::Count)] = {}; uint32_t lines = 0, skipped = 0; };

    /// Reads <folder>/velocity/velocity-index.txt (folder: manifest::Folder()). False when there is no index (nothing installed).
    bool Load(const char* manifestFolder);
    void Unload();
    bool Loaded();

    /// The velocity-bearing module for a shader: path 2 when the line has two (the velocity form), else path 1. Null when none.
    const char* Path(const char* md5, Kind kind);
    /// The line's first path (the plain G-buffer form), or null.
    const char* BasePath(const char* md5, Kind kind);

    Stats GetStats();
}
