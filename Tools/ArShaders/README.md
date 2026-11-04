# Azeroth Remastered shader tools (Aetheria)

- velocity_variants.py: generates the "own velocity" vertex variants (current + previous clip position from a second register file) for the skinned vertex
  modules of the translated-shader set; run it on a VulkanShaders/vertex folder and manifest. Needs Python 3 and SPIRV-Tools (spirv-val) to check the output.
- velocity.frag: the velocity fragment shader (RGBA16F, rg = previous UV - current UV, a = coverage); compiled with glslang into Vulkan/MotionVelocityFrag.spv.h.
- compose.comp: composes the replayed object motion into FSR's motion image; compiled into hd/HdFsrCompose.spv.h.

The generated modules themselves are install assets (VulkanShaders/velocity/), not part of this repository.
