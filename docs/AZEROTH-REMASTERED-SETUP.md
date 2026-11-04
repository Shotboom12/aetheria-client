# Azeroth Remastered mode: setup guide

Azeroth Remastered (AR) mode is the renderer selector entry that carries Mado's graphics work (lamps, Lumenveil lighting, FSR 3.1, DLSS) on Aetheria's own Vulkan device.
Stock and standalone Lumenveil modes are not touched by anything below. This guide covers every part; each part is optional except 1 to 4.

Status: the pieces below were built, installed and tested on one machine (Windows 11, NVIDIA RTX 3080). The steps in this guide were not run again from a clean checkout.

## What you need

- Windows 10/11 x64, a Vulkan 1.1+ GPU (DLSS: NVIDIA RTX only), a World of Warcraft 3.3.5a (12340) client.
- Visual Studio 2022 (or Build Tools) with the C++ workload, CMake, Python 3.8+.
- glslang and SPIRV-Tools (spirv-dis, spirv-as, spirv-val) from the Vulkan SDK or their GitHub releases.
- Mado's Azeroth Remastered release pack (the `WoW-HD-2026-10-28.zip` or newer) and his source: https://github.com/Shotboom12/Mados-Azeroth-Remastered (hosted with his permission).
- Git. Do not commit game files or the generated shader folders.

## 1. Build the client

```
git clone https://github.com/Shotboom12/aetheria-client.git
cd aetheria-client
pwsh Tools/Setup-DlssSdk.ps1                       # only if you want DLSS (downloads NVIDIA's SDK from github.com/NVIDIA/DLSS)
cmake -S . -B build -A x64 -DAETHERIA_NATIVE_FSR=ON -DAETHERIA_NATIVE_DLSS=ON
cmake --build build --config Release
```

- Lamps and Lumenveil logic compile by default (WOWDECOMP_LUMEN is ON). FSR needs `-DAETHERIA_NATIVE_FSR=ON`; DLSS needs that and `-DAETHERIA_NATIVE_DLSS=ON`.
- Without the DLSS option the client builds and runs, the DLSS setting simply does not exist.

## 2. Translated shaders (VulkanShaders)

Run the translator against your own client (see Tools/VulkanShaders/README.md):

```
python Tools/VulkanShaders/translate_shaders.py --client "C:\Games\World of Warcraft 3.3.5a" --deploy
```

This fills `<client>\VulkanShaders\` (manifest.txt, index.txt, vertex\, pixel\).

## 3. Runtime libraries

Copy the folders from this repository's `Redist` into the game folder, next to the executable:

- `Redist\FSR\*.dll` -> `<client>\FSR\`
- `Redist\DLSS\nvngx_dlss.dll` -> `<client>\DLSS\` (NVIDIA RTX only)

(Licenses are in each Redist folder and in UPSTREAM-LICENSES.md.)

## 4. Data from Mado's pack

- `Engine/Source/Gx/CGxDeviceVulkan/hd/lumen/data/lampdata.bin` from Mado's source -> `<client>\Vulkan\lumen\lampdata.bin`. Not in this repository or in Mado's GitHub copy; get it from his source drop or ask him. Without it Lumen does not start.
- Mado's `CataWater` folder (Cataclysm water textures, liquid tables, ripples) -> `<client>\CataWater\`, same relative paths. Needed for the Cata water used in AR mode.
- The WaystoneHD menu: copy `Addons/WaystoneHD` from this repository (English) to `<client>\Interface\AddOns\WaystoneHD`. Mado's original French pages are in his repository (`Interface/AddOns/WaystoneHD`).

## 5. Lamp surfaces (G-buffer modules)

Lamp light on walls and ground needs the G-buffer shader modules. They are made from the "gbuf" variants in Mado's pack, with only the uniform offsets and register-array length changed to our layout:

```
python Tools/ArShaders/prepare_gbuffer_modules.py --pack WoW-HD-2026-10-28.zip --manifest "<client>\VulkanShaders\manifest.txt" --out "<client>\VulkanShaders\gbuffer"
```

It prints how many modules it prepared (about 2,100 on the tested set; a few dozen shaders have no G-buffer variant in the pack). No GPU is needed. Optional check: `spirv-val --target-env vulkan1.1` on a sample.
Without this folder AR mode still runs; lamps fall back to the reference lighting.

## 6. Full object motion (velocity variants)

Skinned characters and creatures need velocity variants of their vertex shaders so FSR and DLSS see their real motion (fewer ghost trails):

```
python Tools/ArShaders/velocity_variants.py --in "<client>\VulkanShaders\vertex" --manifest "<client>\VulkanShaders\manifest.txt" ^
  --out "<client>\VulkanShaders\velocity\own\vertex" --index "<client>\VulkanShaders\velocity\velocity-index.txt" --tools "<folder with spirv-dis/as/val>"
```

It generates a variant per skinned vertex module, validates each with spirv-val, and writes `velocity-index.txt` (about 350 modules on the tested set). The game reads the index at run time; the setting is `hdFsrObjectMotion`.

## 7. First run

Start the game and pick **Azeroth Remastered** in the graphics renderer selector. Then:

- `/hd` (or the gear button next to the micro menu) opens the English WaystoneHD menu; the **FSR / DLSS** tab has the upscaler, quality, sharpness and motion switches, the **Lamps** tab the lamp options.
- `/console farclip 2500`. A larger view distance (for example 10000) makes the world vanish in AR mode.
- Upscaling: `hdFsr 1` (FSR 3.1) or `hdDlss 1` (DLSS); `hdFsrQuality` 1 quality / 2 balanced / 3 performance / 4 ultra performance; `hdFsrSharpness` 0..1, `hdDlssSharpness` 0..2; `hdFsrObjectMotion 1` for full object motion; `hdRenderScale` for plain render scaling without an upscaler.
- Lamps: `hdLvLamps 1` (surface lighting), `hdLvFog`, `hdLvHalos`, `hdLvSpells`, `hdLvShafts`.
- Snow glints (small bright squares on sunlit snow): `surfaceCoverGlints` 0 off to 1 full (default 0.25 in the build; 0.06 looks natural).

## Troubleshooting

- `<client>\Logs\gx.log`: lines starting with "HD " are the FSR, DLSS and Lumen messages. "HD dlss: NGX started; Super Resolution available 1" means DLSS is up; "HD FSR object motion: velocity variants loaded" means step 6 worked.
- Status variables (read them with `/run print(GetCVar("name"))`): `hdFsrInfo`, `hdDlssInfo`, `hdFsrMotionInfo`, `hdLvInfo`, `hdLvLampInfo`.
- DLSS says unavailable: check `<client>\DLSS\nvngx_dlss.dll`, an RTX GPU and a current NVIDIA driver; FSR is used meanwhile.
- Lamps only glow in the haze and do not light surfaces: the `VulkanShaders\gbuffer` folder is missing or empty (step 5).
- Characters ghost with FSR/DLSS: step 6 not done, or `hdFsrObjectMotion` is 0.
