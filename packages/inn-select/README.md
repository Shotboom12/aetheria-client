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
- Rotation: `WOW_INN_ROTATE_FIRST` (30 s), `WOW_INN_ROTATE_MIN/MAX` (30 and 45 s), `WOW_INN_STAY_FIRST` (6), `WOW_INN_STAY_NAMES` (comma-separated character names that never leave), `WOW_INN_COME_WALK_SEC` (3, then they run), `WOW_INN_RUN_MULT` (2).
- Walking: `WOW_INN_WALKERS` (0 = old standing line-up), `WOW_INN_WALK_SPEED/PAUSE_MIN/PAUSE_MAX/MAX/LONG_MIN/LONG_MAX/AWAY_MIN/AWAY_MAX/WANDER_W/STAND_W/SPREAD/RANGE`, `WOW_INN_SIT_SIDE` and `WOW_INN_SIT_SIDE_<raceId>`.
- Camera zoom on click: `WOW_INN_CAST_ZOOM/FRAME/FRAME_HEAD/FRAME_PITCH`, per race `WOW_INN_CAST_FRAME_RACE_<raceId>` and `WOW_INN_CAST_BOUNDS_DIV_RACE_<raceId>`.
- See-through: `WOW_INN_FADE` (1 on), `WOW_INN_FADE_MODE` (1 fade, 2 hide), `WOW_INN_FADE_WALLS`, `WOW_INN_FADE_RADIUS`, `WOW_INN_FADE_TARGET_CLEAR`, `WOW_INN_FADE_DOODAD_MAX/SHRINK`, `WOW_INN_FADE_TIME`, `WOW_INN_FADE_TRI_MAX`.

The full list with defaults is in `WoW/Source/Glue/CharacterSelection.cpp` (search for `WOW_INN_`). The walking map format (`WP`, `LINK`, `SPOT`, entrance spots) is explained at the top of `config/InnWalk.cfg`.

## Rollback

- Turn it off with `WOW_INN_CAST=0` (no rebuild).
- Drag reorder: delete the overlay archive you added. Delete `InnCharOrder.cfg` to forget the saved order.
- Source: revert the seven files.

## Known limits

- The room is the Goldshire inn only. The placement numbers in `InnCast.cfg` were tuned for it.
- Per-race zoom values were tuned by eye for the races in this repository.
- "Recently played" uses the `lastCharacterIndex` setting and list order. Favorites are the names you list in `WOW_INN_STAY_NAMES`.
- A character who has just arrived can be picked to leave again soon after.

## Credits

Design, testing and tuning by the Aetheria owner. Built on the client framework credited in the repository's `CREDITS.md`. The Goldshire inn and the character models belong to Blizzard Entertainment and are read from the player's own game data.
