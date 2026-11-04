# Aetheria

Aetheria is a Wrath of the Lich King 3.3.5a private-server project built on AzerothCore and the Esteria / wowdecomp custom client, with many added races, systems and graphics options. It is a collection of **integration overlays**, not a mirror of the game, client or server source.

Aetheria collects and integrates other people's races and tools. It does not claim to have made them; see [CREDITS.md](CREDITS.md) for who did.

Status words used below:

- **installed** - applied to the owner's local test client/server.
- **accepted** - the owner tested it in game and approved it.
- **staged** - prepared but not installed.
- **planned** - design only.

Installed does not mean accepted. A listed feature is not proof it passed a live test.

## What Aetheria adds

### Playable races

All races are installed in the owner's client unless marked otherwise. Credits for each are in [CREDITS.md](CREDITS.md). Race tooling is WXL, integrated into the client.

| Race | Credit | Notes |
| --- | --- | --- |
| Allied races (full set) | Medviten | installed |
| Centaur | Kalibros | installed |
| Slime Giant | Kalibros | installed |
| High Elf, Goblin | Medviten | installed |
| Kyrian | Aetheria | installed; flight and mid-air combat by the owner; retest pending |
| Venthyr | Aetheria | installed; retest pending |
| Ethereal (Darkweaver / Dreadnaught) | Aetheria, Kalibros framework | installed; partly reported working |
| Arakkoa | Yami, made for CoA | installed |
| Gnoll | Kalibros | installed |
| Vrykul | Kalibros | installed |
| Saberon | Kalibros | installed; **visage mode by the owner**: switches into Vulpera and back, like Worgen |
| Jinyu | Uncle Snowzo | installed |
| Sethrak | Kalibros | installed |
| Earthen, Haranir, Highmountain, Mechagnome, Pandaren, Vulpera, Skyborne | Ported by Kalibros | installed |
| Tarasek, Tuskarr | Kalibros | installed |
| Darkfallen | Kalibros (pack tool only) | not installed |
| Drakkin | Dex (female), Kalibros (male) | installed |
| Drakonid | Kalibros | installed |
| Sylvari face textures | FrolickingFauns | not installed yet |
| Dark Iron, Mag'har, Ogre | Medviten | installed |
| Worgen | Medviten | installed |
| Furbolg | Project Reforged | not installed yet |
| Naga animations | Sirus | installed |
| Dracthyr | Models fetched from Blizzard's CDN, not shipped. Kalibros first brought them to WotLK (without the dragon model); the owner and Kalibros fixed the visage form so the dragon appears. Creator shared credit: owner, Medviten and Kalibros | installed |
| Skyborne | Initial port by Kalibros. Models fetched from the Forever CDN, not shipped | installed |

Dual-form races (Saberon, Worgen, Dracthyr) share a creator and form-switching approach. It is credited jointly to the Aetheria owner, Medviten and Kalibros, whose ideas together produced it.

Many retail models are fetched through the CASC/CDN method from Kalibros rather than shipped.

Installed means installed in the owner's client; it is not the same as tested and accepted.

Racial abilities for all custom races except Kyrian and Venthyr were designed by Kalibros. Kyrian and Venthyr racials are Aetheria's own.

### Gameplay and client features

| Feature | What it does | Status |
| --- | --- | --- |
| Manastorm: speedflying | Retail-style speed flying | installed, testing pending |
| Manastorm: transmog | Collected appearances, Vulkan previews | installed, testing pending |
| Companion looting | Pets can loot | installed, testing pending |
| Mount Studio pack | 55 added mounts | installed, testing pending |
| Retroport toolkit and guide | Tools and notes for porting other retail races yourself. You fetch the files from Blizzard's CDN with the CASC method from Kalibros, so no Blizzard files are shipped. Tools in `tools/retroport/` are by Kalibros; guide: [docs/aetheria/RETROPORT-GUIDE.md](docs/aetheria/RETROPORT-GUIDE.md). | written |
| Graphics menu and renderer selector | Stock Vulkan, Lumenveil, Azeroth Remastered. In our client Azeroth Remastered does not work as well as Lumenveil, which is the better option here. Azeroth Remastered is by Madominator1 and is hosted here with his permission. | installed, not accepted |
| Forever water and lighting data | Water, fog, grading data | installed, not accepted |
| AI setup handoff | A brief any AI assistant can follow to help you set up, integrate and troubleshoot this client and server. See [docs/aetheria/AI-SETUP-HANDOFF.md](docs/aetheria/AI-SETUP-HANDOFF.md) | written |

### Server modules

See [docs/aetheria/Modules.md](docs/aetheria/Modules.md) for what each module does and who made it: account-bound, auctionsim, azeroth-friend, character-order, dungeon-clear, forever-dungeon-journal, holiday-control, individual-progression, llm-chatter, lost-legendaries, mana-loot-pet, mana-speedflying, open-world-party-scaling, playerbots (+ city-life, pvp-life), questie-bridge, raid-clear, retail druid/shaman/warlock forms, transmog and transmog-plus, worgoblin-high-elf.

## Requirements

You need your own legal 3.3.5a client. This repo does not include Blizzard files, MPQs or DBCs. The packages require the Aetheria/Esteria native-race, appearance and protocol framework and are not stock-client drop-in installers.

## Repository map

```
packages/<feature>/   one folder per feature: files, manifest, hashes, prerequisites, rollback
docs/                 design/, status/, integration/, images/
tools/                packers and verification scripts
CREDITS.md            everyone whose work is used
LICENSES/             upstream license texts
```

| Looking for | Go to |
| --- | --- |
| Who made what | CREDITS.md |
| How to integrate a feature | packages/<feature>/README.md |
| Race IDs and registry | docs/integration/races.md |
| What is accepted vs installed | docs/status/ |
| A planned feature | docs/design/ |

## Status

Server and client builds compile. Several races, the creator and the mounts are installed locally; gameplay and visual acceptance is still pending for the most recent work. Nothing here is a release.

## License

Each package follows its upstream's license, listed in [UPSTREAM-LICENSES.md](UPSTREAM-LICENSES.md) and `LICENSES/`. The server tree is GPL-2. Blizzard-derived assets are not distributed.

## Azeroth Remastered mode: setup

Lamps, Lumenveil, FSR 3.1 and DLSS on the native Vulkan device: see [docs/AZEROTH-REMASTERED-SETUP.md](docs/AZEROTH-REMASTERED-SETUP.md).
