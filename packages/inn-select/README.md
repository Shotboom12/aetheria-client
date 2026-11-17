# Inn character select (optional)

An alternative character select screen: your characters stand, walk, chat, drink, lean and sit in the Goldshire inn. Click a character and the camera moves in on them. It is **off by default**, so the normal select screen is unchanged until you switch it on.

What it does:

- The room is the retail Goldshire inn, drawn from the game's own data (`World\wmo\Azeroth\Buildings\GoldshireInn\GoldshireInn.wmo`). **No game assets are included in this package.**
- Characters walk between points on a hand-made walking map (`config/InnWalk.cfg`), sit on chairs and stools, lean on walls, drink at the bar and chat in small circles.
- The hall holds a limited number of characters (`WOW_INN_CAST`, at most 24). With more characters than that, others walk out through a door about every 30 to 45 seconds and someone else walks in. The first 6 in the list, the last character you played and any names in `WOW_INN_STAY_NAMES` always stay.
- Clicking a character who is not in the hall makes them walk in from a door (they run if it takes too long), and the camera zooms once they arrive.
- Anything between the camera and the selected character (railings, furniture, wall pieces) fades out.
- Drag a name in the list to move it. The order is saved in `InnCharOrder.cfg` next to the game and applied the next time the list loads. The first 6 positions are the characters that always stay in the inn.

AI coding assistants: read [AI-AGENTS.md](AI-AGENTS.md). It has the integration order, the hook points, checks to run, and every problem we hit with its fix.

Status: written and tested by the Aetheria owner on the Aetheria x64 client (build 12340). Not built from this published tree. It needs the Aetheria client framework (the Lua natives, the ECS roster and the race models of this repository); it is not a drop-in for a stock client.

## Use it

1. Take the source change (see [FILES.md](FILES.md)): apply `patches/inn-select-client.patch` to your client source, or copy the seven files listed there, and build the client.
2. Copy `config/InnCast.cfg` and `config/InnWalk.cfg` into the game folder next to the executable. `InnCast.cfg` is re-read about every half second while the game runs, so most settings can be changed live. Leave the character select and come back to rebuild the layout.
3. Optional, for drag reorder: apply `lua/inn-drag-CharacterSelect.patch` to `Interface\GlueXML\CharacterSelect.lua` and put the result in a patch archive that loads **after** every archive that already contains that file (for example a one-file archive named so that it sorts last). Without this step everything else works; you just cannot drag names.
4. Start the game and open the character select.

## Switch it on and off

- On: `WOW_INN_CAST=<number of characters the hall holds>` in `InnCast.cfg` (the shipped file uses 18).
- Off (normal select screen): `WOW_INN_CAST=0`, or remove the line, or delete `InnCast.cfg`. The environment variable of the same name does the same.

## Settings

All in `InnCast.cfg`, as `NAME=value`; `#` starts a comment.

- Layout and room: `WOW_INN_CAST`, `WOW_INN_CAST_START` (built at once, 15), `WOW_INN_CAST_TRICKLE_MS`, `WOW_INN_WMO` (the room), `WOW_INN_WMO_X/Y/Z/YAW/SCALE/BRIGHT/DOODADS`, `WOW_INN_CAM_*` (room camera), `WOW_INN_CAST_PER_ROW/SPACING/ROW_SPACING`.
- Rotation: `WOW_INN_ROTATE_FIRST` (30 s), `WOW_INN_ROTATE_MIN/MAX` (30 and 45 s), `WOW_INN_STAY_FIRST` (6), `WOW_INN_STAY_NAMES` (comma-separated character names that never leave), `WOW_INN_COME_WALK_SEC` (3, then they run), `WOW_INN_RUN_MULT` (2), `WOW_INN_STAY_MIN` (60: a character who walked in cannot be picked to leave for this many seconds; 0 = off).
- Backdrop: the room is placed in the select scene's backdrop model, so it is pinned to the Orc backdrop (`Interface\Glues\Models\UI_Orc\UI_Orc.m2`) whatever the first listed character is. `WOW_INN_CAST_BACKGROUND=<full model path>` overrides it. The value must be a full model path, not a race name.
- Walking: `WOW_INN_WALKERS` (0 = old standing line-up), `WOW_INN_WALK_SPEED/PAUSE_MIN/PAUSE_MAX/MAX/LONG_MIN/LONG_MAX/AWAY_MIN/AWAY_MAX/WANDER_W/STAND_W/SPREAD/RANGE`, `WOW_INN_SIT_SIDE` and `WOW_INN_SIT_SIDE_<raceId>`. Talking circle (four standing seats): `WOW_INN_CIRCLE_W` (30, chance weight to start joining), `WOW_INN_CIRCLE_W_STARTED` (90, once somebody is in it), `WOW_INN_CIRCLE_EXTRA` (2, joining a started circle may exceed the walking cap by this many). With rotation, door arrivals and leavers count as walking, so a low `WOW_INN_WALK_MAX` (4) kept the circle from filling; 6 to 7 works.
- Camera zoom on click: `WOW_INN_CAST_ZOOM/FRAME/FRAME_HEAD/FRAME_PITCH`, per race `WOW_INN_CAST_FRAME_RACE_<raceId>` and `WOW_INN_CAST_BOUNDS_DIV_RACE_<raceId>`.
- See-through: `WOW_INN_FADE` (1 on), `WOW_INN_FADE_MODE` (1 fade, 2 hide), `WOW_INN_FADE_WALLS`, `WOW_INN_FADE_RADIUS`, `WOW_INN_FADE_TARGET_CLEAR`, `WOW_INN_FADE_DOODAD_MAX/SHRINK`, `WOW_INN_FADE_TIME`, `WOW_INN_FADE_TRI_MAX`.

The full list with defaults is in `WoW/Source/Glue/CharacterSelection.cpp` (search for `WOW_INN_`). The walking map format (`WP`, `LINK`, `SPOT`, entrance spots) is explained at the top of `config/InnWalk.cfg`.

## Use a different inn model

No inn model is included. The room is whatever `WOW_INN_WMO` points at, read from the game's data. Two ways to change it:

- **A better-looking copy of the same inn (recommended).** Put the replacement root and group files at the same path (`World\wmo\Azeroth\Buildings\GoldshireInn\`) in a patch archive that loads after every archive holding the old ones, together with any textures your data lacks. Nothing else changes. The classic origin and layout must be kept: the walking map (`InnWalk.cfg`), seats, leans and camera are placed in the inn's own coordinates. Check the group boxes in the root (`MOGI`) against the classic inn before you start; the same hall and upstairs should line up. The doodad sets (`MODS`) should be the classic ones; `WOW_INN_WMO_DOODAD_SET=2` is the furniture the map was built around. One high-detail copy of the Goldshire inn was tested this way: 9 groups, 86,226 vertices, 37 materials, 422 doodads (global set plus set 2), retail-derived textures, and the classic furniture models. It needed no setting changes and the existing map fitted.
- **A retail WMO read directly.** If the client's retail reader is set up (`RetailCasc.ini`, your own retail access), `WOW_INN_WMO=retail\<FileDataID>.wmo` loads a retail WMO. Tested with the Shadowlands-era Goldshire inn remake (file ID 3718980): it has the same footprint, but only one doodad set (use `WOW_INN_WMO_DOODAD_SET=0`), far fewer props (141 against 426, so it does not look like the classic bar), baked colours that need `WOW_INN_WMO_BRIGHT` around 3.0, and its newer light list (`MNLD`) is not read, so dark corners stay dark. A few doodads can stay pending if their models need features the retail model reader does not support. Decoded retail files stay on your machine; do not put them in a repository.

After any change, leave the select screen and re-enter it. `Logs\Extensions.log` shows `InnWmo: <path> groups=N/N vertices=... materials=... doodads=... ok=1` and the `InnWmo furniture ... failed=` line.

## Rollback

- Turn it off with `WOW_INN_CAST=0` (no rebuild).
- Drag reorder: delete the overlay archive you added. Delete `InnCharOrder.cfg` to forget the saved order.
- Source: revert the seven files.

## Known limits

- The room is the Goldshire inn only. The placement numbers in `InnCast.cfg` were tuned for it.
- Per-race zoom values were tuned by eye for the races in this repository.
- "Recently played" uses the `lastCharacterIndex` setting and list order. Favorites are the names you list in `WOW_INN_STAY_NAMES`.
- Right-dragging on the screen turns the selected character in place; the turn is kept until you select someone else. Mirrored forms are not turned in the inn.
- With the inn on, a small watchdog writes `InnWatchdog.log` in the game folder if the select screen stops ticking for 6 seconds, with the phase it was last in. If you report a freeze, include that file and `Logs\Extensions.log`. A known cause of silent freezes is an engine animation-loader loop on broken animation chains of some retail-converted models; see the problem table in `AI-AGENTS.md` for the fix (`M2Shared.cpp`, `M2Model.cpp`).

## Credits

Design, testing and tuning by the Aetheria owner. Built on the client framework credited in the repository's `CREDITS.md`. The Goldshire inn and the character models belong to Blizzard Entertainment and are read from the player's own game data.
