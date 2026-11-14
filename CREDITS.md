# Credits

Aetheria is built on the work of many people. Aetheria collects and integrates other people's races, tools and assets and does **not** claim to have made them. Each one belongs to the creator named below. Aetheria's own work is limited to the integration, the Saberon visage mode, Kyrian flight and mid-air combat, and the documentation. Each entry says who made what, as stated by the Aetheria owner; license and source links are tracked in [UPSTREAM-LICENSES.md](UPSTREAM-LICENSES.md) and must be confirmed before publication.

## Races and race content

| Race / content | Credit |
| --- | --- |
| Drakkin (female) | **Dex** |
| Drakkin (male) | **Kalibros** |
| Drakonid | **Kalibros** |
| Esteria races/framework | **Kalibros** |
| Arakkoa | **Yami**, made for **CoA** |
| Jinyu | **Uncle Snowzo** |
| Hozen (race 112) and Sylvari (race 110) | **Ilusixn**, author of Custom-Race-3.3.5a (files given to the Aetheria owner, used with his permission) |
| Sylvari (face textures, not installed yet) | **FrolickingFauns** |
| Gnoll, Vrykul | **Kalibros** |
| Saberon, Sethrak | **Kalibros** |
| Earthen, Haranir, Highmountain, Mechagnome, Pandaren, Vulpera (ported) | **Kalibros** |
| Tarasek, Tuskarr | **Kalibros** |
| High Elf, Worgen, Goblin, Dark Iron, Mag'har, Ogre | **Medviten** |
| Centaur, Slime Giant, Ethereal | **Kalibros** |
| Dracthyr (initial WotLK port, without the dragon model) | **Kalibros** |
| Dracthyr visage form fix (dragon model now appears) | The Aetheria owner and **Kalibros** |
| Skyborne (initial port) | **Kalibros** |
| Dracthyr and Skyborne models | Blizzard Entertainment, fetched by each user from the CDN (Forever CDN for Skyborne); not distributed here |
| Allied races | **Medviten** |
| Retroport tooling and CASC/CDN method | **Kalibros** (method and tools given to the owner) |
| Furbolg | **Project Reforged** |
| Naga animations | **Sirus** |
| WXL race tooling (integrated into our client) | **Furioz, Corruption, the Eunoia team and Ithorgrim**, together with **WXL / Dokman** |
| Retail female body models (Human, Blood Elf, Night Elf, Draenei, Nightborne, Void Elf, Lightforged, Haranir, Dracthyr visage, Skyborne) and the Sylvari female body | **Rwise** ("Idealistic Physics" models), used and redistributed by the Aetheria owner with Rwise's permission; Rwise asks that no one else redistribute them (packages/aetheria-models) |
| Jiggle and cloak physics (JigglePhysics.h, CapePhysics.h) | Aetheria, jiggle parameters modelled on Rwise's physics setup |
| Dual-form races (Saberon, Worgen, Dracthyr): creator and form switching | Shared credit: the Aetheria owner, **Medviten** and **Kalibros**, whose ideas together produced it |
| Saberon visage mode (Vulpera and back) | The Aetheria owner |
| Kyrian flight and mid-air combat | The Aetheria owner |
| Kyrian, Venthyr, Ethereal | Aetheria, built on the frameworks below |

## Collaborators

- **Claude** (Anthropic's AI model, used through Claude Code) - collaborator on documentation and packaging help. Commits it contributes to carry a `Co-Authored-By: Claude` line.

## Racial abilities

- **Kalibros** designed the racials for all of the custom races except Kyrian and Venthyr.
- Kyrian and Venthyr racials are Aetheria's own.

## Frameworks, tools and engine

- **Kalibros** (GitHub: STRHercules) - Esteria server and wowdecomp client frameworks, technical guidance, and continued help. Recent upstream: EsteriaWoW and wowdecomp.
- **Medviten** - help, and the **WHE module**.
- **Saty** - the **x64 decompilation work** the custom client source is built on. The client in this repo derives from the wowdecomp / Esteria client source, which builds on Saty's decompilation, via Kalibros' fork.
- **WXL / Dokman** - race tooling.
- **AzerothCore** contributors - the server core (see its AUTHORS file).

## Other upstream projects

- **Azeroth Remastered by Madominator1** - graphics work. All credit belongs to Madominator1; Aetheria only hosts the download and updates, with his permission.
- **Lumenveil** - the renderer, taken from the wowdecomp client source (STRHercules / Kalibros' project, `Engine/Source/Lumenveil`). Its license is GPL-2 only; authorship per its source notices.
- **Forever** - water and lighting data (fetched from its CDN, not shipped).
- Easy Controller - Forever (MIT) - wheel art used by Kalibros' hot-swap.
- **DBCTool by Foe** (MIT) - the DBC tool bundled with the WHE module.
- **wxl-seyris-tools by Morfium-G** (https://github.com/Morfium-G/wxl-seyris-tools) - the shared WXL toolkit (cdbc reader/writer, table editor, settings and the F9 panel launcher), ported to our x64 client as `Extensions/WxlHost/src/seyris` with the author's permission.
- **WarcraftXL** (GPL-3.0-or-later) - the extension API (`PluginApi.h`) that `Extensions/WxlHost` implements in-client for ported WXL modules; the WXL core itself is not part of this client.
- **Dear ImGui by Omar Cornut and contributors** (MIT) - the F9 overlay, vendored (v1.91.9) in `Extensions/WxlHost/src/imgui`.
- **mod-playerbots** and **iCore**'s city-life and pvp-life modules, **mod-transmog** and the other AzerothCore modules listed in [Modules.md](Modules.md); each module's author is credited in its own README and LICENSE.

Original World of Warcraft assets belong to Blizzard Entertainment. No client files, MPQs or DBCs from the game are distributed here. The archives in packages/aetheria-models are Aetheria's own replacement-model archives (credited to Rwise above), not Blizzard files.

If a credit is missing or wrong, tell the Aetheria owner and it will be fixed.

## Azeroth Remastered native port (2026-11-04)

Lamps, Lumenveil lighting, FSR 3.1 and DLSS for Azeroth Remastered mode, plus the English WaystoneHD menu (Addons/WaystoneHD), build on Madominator1's Azeroth Remastered 2026-11-03 source (graphics work, credit stays with him) and on AMD FidelityFX and NVIDIA NGX. The native device glue, object-motion replay, G-buffer replay and the settings integration are Aetheria's.
