#include "Engine/Source/Gx/GxSeam.h"

// HD: see GxSeam.h. Filled by the Vulkan device (CGxDeviceVulkan/wxl/compat/src/Seams.cpp).
GxSeam g_gxSeams[GxSeams_Last] = {};

int32_t (*g_gxShaderKnown)(const void* code, uint32_t size) = nullptr;

// HD: the Cata water reflection plane (GxSeam.h).
GxCataPlane g_gxCataPlane = {};

// HD: the Cata water material's inputs (GxSeam.h).
GxCataWater g_gxCataWater = {};

// HD: the sky for the Cata water's reflections (GxSeam.h).
GxCataSky g_gxCataSky = {};
GxCataRipples g_gxCataRipples = {};   // HD: tranche 2
