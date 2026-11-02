# AI setup handoff

Give this file to an AI coding assistant, together with your own checkout of `aetheria-server` and `aetheria-client` and a description of what you want. It explains how the project is organized, what order to do things in, and what the assistant must not do.

## What this project is
A Wrath of the Lich King 3.3.5a server (AzerothCore-based, with modules) and a custom x64 client (Esteria / wowdecomp, built on Saty's decompilation). Together they add many races, dual-form races, flight and mounts, transmog, and graphics options. This repo contains source, data updates and tools. It does not contain the game or any Blizzard files.

## Rules the assistant must follow
1. Never ask for, store or print passwords, tokens or database credentials. Config files with credentials stay on the user's machine.
2. Never download or run unknown executables. Fetch only what the guides name, from the sources they name.
3. Never upload Blizzard client files, MPQs or DBCs.
4. Before editing, work out which side owns the problem: client, server, DBC or database data, models and skins, animation, UI, cache, or patch load order.
5. Keep server, client and data changes matched. A client change that needs a server change (or the reverse) must be made and tested together.
6. Back up files and affected database rows before changing them. Apply database changes in a transaction and read them back.
7. Never start the game or server, or end a running process. Ask the user to close it.
8. A successful build does not mean the feature works. Ask the user to test it in game and record the result separately from "installed".
9. Preserve behavior the user has already accepted. Change one thing at a time and say how to undo it.
10. Keep `CREDITS.md` intact when editing docs.

## Layout
- `aetheria-server`: AzerothCore tree. Custom work is in `modules/` (see `docs/aetheria/Modules.md`), `data/sql/updates/pending_db_*` (new database updates), `src/` (core changes) and `tools/`.
- `aetheria-client`: client source (`WoW/`, `Engine/`), `Addons/`, `Tools/` and `docs/`. It builds the executable; it is not a playable client by itself.
- Race IDs are set in the shared race framework (for example Naga 54, Kyrian 60, Venthyr 61, Saberon 64, Drakonid 70, Tarasek 72). Check the race registry before adding or reusing an ID.

## Setup order
1. **Prerequisites.** A legal 3.3.5a client of your own, a C++ toolchain for each tree (Visual Studio for the client, the AzerothCore requirements for the server), MySQL and Git.
2. **Server.** Build following AzerothCore's normal steps. Create the databases, then apply `data/sql/updates/pending_db_*` in order. Copy `conf/dist/*.conf.dist` to real `.conf` files and set your own database login. Enable only the modules you want.
3. **Client.** Build the executable from `aetheria-client`. The native race, appearance and protocol framework must be present before any race is added.
4. **Models and textures.** Models for retail-derived races are not in this repo. Follow the retroport guide to fetch them from a CDN with the CASC method, and install them as patches in `Data/`.
5. **Data patches.** Race, spell and appearance data are matched pairs of client DBC rows and server database rows. Apply both sides, in the order the package notes give.
6. **Verify.** Race appears in the creator, correct model and scale, animations play, character logs in, relog works, then ask the user to test and report.

## Common problems
- **Race missing or blank in the creator:** client and server race rows do not match, a patch is missing, or the patch load order is wrong. Later-named patches in `Data/` override earlier ones.
- **Wrong size or framing on character select:** the race's display scale in the creature display data is wrong (client and server rows both need to agree).
- **Missing animations or invisible armor:** the required model and skin files were not fetched or installed; check the file names the framework expects.
- **Lua error in the creator:** a form or appearance block is out of scope or mismatched with the client build. Re-check that the client build and the UI scripts come from the same version.
- **Stale look after a change:** clear the client cache and relog.
- **Database import errors:** an update was applied twice or out of order. Check what has already been applied before re-running anything.
- **Server crash on start:** a missing module dependency or a database that is not reachable. Check the server log; do not guess.

## Credits
See `CREDITS.md`. When you help a user, keep the credits and license notices in place.
