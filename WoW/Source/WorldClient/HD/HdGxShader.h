#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDGXSHADER_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDGXSHADER_H

// Waystone HD: what the HD materials (HdTerrain, HdMapObj, HdDetailDoodad, HdSky) need from the graphics device when
// they put their own shader code into a CGxShader, on D3D9 and on the Vulkan device (gxApi vulkan, CGxDeviceVulkan).
//   - Vulkan draws DX9 bytecode through modules translated offline (Vulkan\shaders\translated\): a material swaps
//     its code in only when the device knows it (ShaderKnown), so an untranslated one keeps Blizzard's shader.
//   - The translated pixel modules read c0..c12 only; an HD pixel shader's c13 and up are read from vertex rows
//     64 + (N - 13) instead (tools/bls_tools/hd_blobs.py), which PixelConstantsSet fills too under Vulkan.
//   hdShaderDump   1 = write every HD shader the materials compile to Vulkan\hd-blobs\ (the translator's input)

#include <cstddef>
#include <cstdint>

class CGxShader;

namespace HD {
    void ShaderCVars();

    // The device is the Vulkan one (gxApi vulkan).
    bool VulkanDevice();

    // The device runs the HD materials' code: D3D9, D3D9Ex, or Vulkan.
    bool ShaderSwapDevice();

    // The device can draw this code (always on D3D9; on Vulkan, when its translated module exists).
    bool ShaderKnown(const uint8_t* code, size_t size);

    // Under Vulkan a CGxShader's API object is the translator's manifest record, owned by the device: nothing to
    // release. Returns true when the caller must not release the old object.
    bool ShaderObjectOwnedByDevice();

    // ShaderConstantsSet(GxSh_Pixel, ...), plus the vertex-row copy of c13 and up under Vulkan.
    void PixelConstantsSet(uint32_t first, const float* data, uint32_t count);

    // hdShaderDump 1: Vulkan\hd-blobs\<material>-<vs|ps>-<a>-<b>.bin. Safe on the compile threads.
    void ShaderDump(const char* material, bool pixel, int32_t a, int32_t b, const uint8_t* code, size_t size);
}

#endif
