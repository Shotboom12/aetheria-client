# Retroport guide

How to bring retail races to this 3.3.5a client and server using your own game files. **No Blizzard files are in this repo.** You fetch them yourself.

## Credit
The retroporting method and the race-pack tools in `tools/retroport/` were made by **Kalibros** (STRHercules) for Esteria and given to the Aetheria owner. They are included here with his permission, as a snapshot. For the newest versions, use his repository (EsteriaWoW). Race credits are in [CREDITS.md](../../CREDITS.md).

## What is included
`tools/retroport/` holds Python scripts that build, validate and install a race pack (client patch plus server data) from a manifest:
- `retroported_race_pack.py` is the main tool. It builds a pack, checks the staged output, and installs by backing up and hash-verifying your live patch archives first.
- `*_race_pack.py` files are per-race packs (for example arakkoa, centaur, drakkin, drakonid, gnoll, jinyu, saberon, sethrak, skyborne, vulpera).
- `test_*.py` files are the tests for those tools.
- `data/retroported-races/` holds the centaur and slime giant faction manifests.

## What you must add yourself
- **The retail files.** Fetch them from a CDN with the CASC method Kalibros published. They are Blizzard's and stay on your machine.
- **Dependencies not copied here.** The scripts import `cars_mount_pack`, `wotlkconv` (the BLP image library) and `modules/mod-classless-wildcard/client-patch`. Get them from Kalibros' repository and place them where the scripts expect (see the `sys.path` lines at the top of each script). Python packages such as Pillow are also needed.
- **The native race framework** in the client and server, and your own base race records.

## Typical steps
1. Fetch the race's retail models and textures with CASC into a working folder.
2. Run the race's `*_race_pack.py` (or `retroported_race_pack.py` with its manifest) to build the pack in a staging folder. Nothing is installed at this stage.
3. Run the tests and review what was built.
4. Close the game and server. Install the pack with the tool, which backs up your live archives first.
5. Apply the matching server data updates, then test the race in game and report what you see.

## Limits
- These tools were written against the Esteria layout. Expect to adjust paths for a different fork.
- Having these scripts here does not mean each race works in your client. Test every race yourself.
