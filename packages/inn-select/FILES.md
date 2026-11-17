# What to take from this repository

Everything lives in the normal client tree. To add only the inn select to another client, take these.

## Source (all under `WoW/Source/Glue/`)

| File | Change |
| --- | --- |
| `InnWmoBackdrop.h` | new: draws the inn WMO and its doodads, and the see-through fade |
| `InnWalk.h` | new: reads `InnWalk.cfg` (the walking map) |
| `InnWalkers.h` | new: the walkers (walk, sit, lean, chat, leave and arrive through doors) |
| `CharacterSelection.cpp` | cast placement, camera, rotation, list reordering; about 1,500 added lines |
| `CharacterSelection.h` | five new static functions: `InnCastSlot`, `InnCastShow`, `InnCastTick`, `InnCastReset`, `InnCastUserFacing` |
| `CharacterSelection_Scripts.cpp` | Lua function `MoveCharacter(from, to)`; `SetCharacterSelectFacing` turns the selected character in place while the inn is on |
| `CharacterSelectionDisplay.cpp` | uses `InnCastSlot` for the selected character's position |

The three new `.h` files are header-only (no CMake change; they are included from `CharacterSelection.cpp`).

`patches/inn-select-client.patch` contains all seven as a `git diff` against the client as published before the inn select (commit `7293221`). It was checked to reproduce the tested files exactly. This repository stores text with LF line endings, so if your files use CRLF apply it with `git apply --whitespace=fix` (or `patch -p1 -l`).

## Engine fix for silent freezes (recommended)

`patches/m2-alias-chain-guard.patch` bounds the four animation alias-chain walks in `Engine/Source/Model2/M2Shared.cpp` and `M2Model.cpp` to one pass over the model's sequence count. Some retail-converted models have chains that never loop back to their start, and the engine loader then spins forever; the inn triggers it because it plays sit, lean, drink and emote animations loaded from their own files. It is an engine fix that also protects the world. See the problem table in `AI-AGENTS.md`.

## Game folder (next to the executable)

- `config/InnCast.cfg`: all settings (see the README).
- `config/InnWalk.cfg`: the walking map of the Goldshire inn.
- `InnWatchdog.log` is written by the game only if the select screen stalls for 6 seconds (inn on); attach it to a freeze report.
- `InnCharOrder.cfg` is created by the game when you drag names. It holds character names, so do not share it.

## Lua (optional, drag reorder)

`lua/inn-drag-CharacterSelect.patch` changes `Interface\GlueXML\CharacterSelect.lua` as used by this client:

- drag handlers on the list rows (hold and move more than 14 px, drop on a row; the list scrolls at its edges),
- a call to the native `MoveCharacter(from, to)`,
- a rebuild of the Esteria roster (`ECS.Integrate.PostUpdate`) right after a drop, because that roster draws the rows from its own copy of the list. If your client has no such roster the call is skipped.

Put the changed file in a patch archive that loads last (an MPQ whose name sorts after every other archive that contains `CharacterSelect.lua`).

## Needs

- The Aetheria client's WMO and M2 rendering (`InnWmoBackdrop.h` uses this tree's scene model and WMO loaders).
- The retail `GoldshireInn.wmo` and its doodad models in your game data.
