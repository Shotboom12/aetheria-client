# Upstream license tracker (internal, must be completed before any push)

Rule: do not publish a file whose license is unknown or forbids redistribution. Link to the upstream instead, or ask the author. Permission from the author is recorded in the last column.

| Upstream | Used for | License | Source verified | Author OK to redistribute |
| --- | --- | --- | --- | --- |
| AzerothCore | Server core | AGPL/GPL (server tree LICENSE says GPL-2; confirm) | local LICENSE read | n/a |
| Kalibros / STRHercules: EsteriaWoW, wowdecomp | Frameworks, Esteria races | Not visible on GitHub pages; sidebar suggests GPL-2.0 | NO | Owner says Kalibros asked for this repo |
| Medviten | WHE module, Dark Iron, Mag'har, Ogre | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Dex | Drakkin female | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Yami | Arakkoa | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Uncle Snowzo | Jinyu | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| FrolickingFauns | Sylvari face textures | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Furioz / Corruption / Eunoia team | unknown items | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Project Reforged | Furbolg | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Sirus | Naga animations | unknown (Sirus is a commercial server; assets may not be redistributable) | NO | owner reports yes (2026-11-02); no written proof on file; confirm Sirus specifically covers redistribution |
| WXL / Dokman | Race tooling | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Saty | x64 decompilation | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Lumenveil | Renderer | GPL-2 only (hub note); client GPL-3 compatibility unresolved | partly | n/a |
| Azeroth Remastered (Madominator1) | Graphics | unknown | NO | owner reports EXPRESS permission to post/host (2026-11-02); credit stays with Madominator1, Aetheria only hosts download and updates; no written proof on file |
| Forever | Water/lighting data | unknown | NO | owner reports yes (2026-11-02); no written proof on file |
| Easy Controller - Forever | Wheel art | MIT | via Kalibros commit note | n/a |
| Morfium-G: wxl-seyris-tools | F9 toolkit, ported to x64 (Extensions/WxlHost/src/seyris) | no LICENSE file in the repo | NO | owner reports yes, port to x64 as we see fit (2026-11-03); no written proof on file |
| WarcraftXL (WXL SDK header, event ids) | WXL extension API implemented by Extensions/WxlHost | GPL-3.0-or-later (header notice) | header read | n/a; GPL-3 vs the client's GPL-2 constraint is the same open question as Lumenveil |
| Dear ImGui (Omar Cornut) | F9 overlay, vendored v1.91.9 | MIT | LICENSE.txt in Extensions/WxlHost/src/imgui | n/a |

## Server modules without a LICENSE file (as found 2026-11-02)

mod-auctionsim, mod-azeroth-friend, mod-mana-loot-pet, mod-mana-speedflying, mod-questie-bridge. Owner to say who wrote each; add a license or author permission before publishing. Other modules carry their own LICENSE files, which must stay in the published tree.

## Client source

wowdecomp / Esteria client source (Kalibros fork), built on Saty's x64 decompilation. License of the client source tree not confirmed from GitHub pages; check the LICENSE in the checkout and Saty's/Kalibros' terms before publishing the client repo.

Owner decision 2026-11-02: Allied races and other race model files that Medviten and Kalibros have already posted publicly will be included. For each such file, record the public URL where the author posted it (provenance), so the repo can link back to the original.

mod-auctionsim keeps its data file (`auctionsim.dat`, price data scraped from a live realm per its README), by owner decision 2026-11-02, because the module needs it. Provenance of the scrape is the module author's; no separate permission on file.

Highest risk items: Sirus animations and any race assets taken from other servers or paid packs. Default for those is "link and instructions only", no files in the repo.

## Azeroth Remastered native port (2026-11-04)

| Component | Use | License | Note |
|---|---|---|---|
| Azeroth Remastered 2026-11-03 source (Madominator1): hd/lamp, hd/lumen, hd/fsr, hd/dlss, WaystoneHD addon | Ported to Aetheria's native Vulkan device (Azeroth Remastered mode only) | GPL-3.0-or-later (source headers) | Lumenveil parts keep their own license files under hd/lumen/licences; the GPL-2-only question above still applies |
| AMD FidelityFX SDK headers (ThirdParty/FidelityFX) | FSR 3.1 upscaling; the runtime DLLs are NOT included and are loaded from <exe>/FSR | MIT (ThirdParty/FidelityFX/license.md) | |
| NVIDIA DLSS / NGX SDK 310.9.16 | DLSS Super Resolution (hd/HdNativeDlss) | NVIDIA RTX SDK license | NOT included. Build with AETHERIA_NATIVE_DLSS=ON only after placing the SDK in ThirdParty/DLSS yourself; nvngx_dlss.dll is loaded from <exe>/DLSS |
| lumen/data/lampdata.bin | Lumenveil lamp table | not redistributed | NOT included |
