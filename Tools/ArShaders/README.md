# Azeroth Remastered shader tools (Aetheria)

Full walk-through: docs/AZEROTH-REMASTERED-SETUP.md.

- prepare_gbuffer_modules.py: builds VulkanShaders/gbuffer (the G-buffer modules lamp surfaces need) from Mado's release pack and your own translated manifest. Python 3 only.
- velocity_variants.py: builds VulkanShaders/velocity/own/vertex (the velocity variants of the skinned vertex modules, for full object motion) and writes velocity-index.txt. Needs Python 3 and SPIRV-Tools (spirv-dis, spirv-as, spirv-val).
- velocity.frag: the velocity fragment shader (RGBA16F, rg = previous UV - current UV, a = coverage); already compiled into Vulkan/MotionVelocityFrag.spv.h.
- compose.comp: composes the replayed object motion into FSR's motion image; already compiled into hd/HdFsrCompose.spv.h.

The generated modules are install assets, not part of this repository.
