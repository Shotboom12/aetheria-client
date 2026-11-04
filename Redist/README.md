# Redistributable runtime libraries (Azeroth Remastered mode)

These are the only third-party binaries the upscalers need at run time. Copy the folders next to the game executable:

- `Redist/FSR/*.dll` -> `<game folder>\FSR\` (AMD FidelityFX FSR 3.1, binary redistribution under AMD's license: Redist/FSR/LICENSE-AMD-FidelityFX.md)
- `Redist/DLSS/nvngx_dlss.dll` -> `<game folder>\DLSS\` (NVIDIA DLSS 310.9.16 Super Resolution, redistributed as part of this application under the NVIDIA RTX SDK license: Redist/DLSS/LICENSE-NVIDIA-RTX-SDK.txt; needs an NVIDIA RTX GPU)

This software contains software provided by NVIDIA Corporation and Advanced Micro Devices, Inc. Both libraries are unmodified copies from the vendors' public releases. Their licenses apply to them, not the repository's GPL license.

## Building with DLSS

The DLSS SDK headers and import library (`nvsdk_ngx_d.lib`) are NOT stored in this repository: NVIDIA's license only allows redistributing the SDK's runtime in an application, not the SDK itself. They are free to download from NVIDIA's own repository. Run once:

    pwsh Tools/Setup-DlssSdk.ps1

then configure with `-DAETHERIA_NATIVE_FSR=ON -DAETHERIA_NATIVE_DLSS=ON`. The FidelityFX headers (MIT) are in ThirdParty/FidelityFX.
