# Inn character select: guide for AI coding assistants

Read this together with [README.md](README.md) and [FILES.md](FILES.md). It tells you how to add the inn select to an Aetheria / wowdecomp client, how the parts fit together, how to check it, and every problem we hit while building it with the fix.

## Rules (same as `docs/aetheria/AI-SETUP-HANDOFF.md`)

1. Never ask for, store or print passwords, tokens or credentials.
2. Never download or run unknown executables. Never upload Blizzard files, MPQs or DBCs.
3. Do not start the game or end a running game process. Ask the user to close it normally before you install anything.
4. Back up the executable and any archive before you replace it. Keep the old copy so the user can roll back.
5. A successful build does not mean it works. Ask the user to look at it in game and report what they see. Record "built", "installed" and "tested in game" separately.
6. Change one thing at a time. Preserve behavior the user has already accepted.
7. The feature must stay off unless the user switches it on (`WOW_INN_CAST` above 0).

## What you are adding

- Seven source files under `WoW/Source/Glue/` (see FILES.md). Everything is behind `WOW_INN_CAST`; with it unset or 0 the normal select screen runs.
- Two config files next to the executable (`InnCast.cfg`, `InnWalk.cfg`). `InnCast.cfg` is re-read about every half second, so most tuning needs no rebuild.
- Optionally one changed Lua file (`Interface\GlueXML\CharacterSelect.lua`) delivered in a patch archive that loads last, for drag reorder.
- The inn itself comes from the user's own game data: `World\wmo\Azeroth\Buildings\GoldshireInn\GoldshireInn.wmo` and the doodad models it names. Do not copy retail files into the repository.

## Integration order

1. Check which side owns what before editing: C++ (client), Lua (GlueXML), config (game folder), data (the user's MPQs).
2. Apply `patches/inn-select-client.patch` (`git apply --whitespace=fix`). If it does not apply, your `CharacterSelection.cpp` has diverged from the published one; merge by hand using the seven-file table in FILES.md and keep the hook points listed below.
3. Build the client with the project's normal build steps. Expect enum and one number-conversion warning; there should be no errors.
4. Put `config/InnCast.cfg` and `config/InnWalk.cfg` in the game folder. Check that `WOW_INN_CAST` is above 0.
5. If drag reorder is wanted, apply `lua/inn-drag-CharacterSelect.patch` to the Lua file your client actually loads (read it back from the archive stack; do not use an older copy) and deliver it in a one-file patch archive that sorts after every other archive containing that file.
6. Ask the user to start the game and open the character select. Ask what they see. Read `Logs\Extensions.log` for lines that start with `InnCast`, `InnWmo` and `InnWalkers`.

## Hook points in the C++

- `CCharacterSelection::ShowCharacter` detaches all child models, so the cast is re-attached afterwards (`InnCastShow`). If your fork changes ShowCharacter, keep that re-attach.
- `InnCastTick` runs every frame from the glue loop. It builds characters, drives the walkers, runs the camera and rotation, and draws the room.
- `InnCastSlot` gives `CharacterSelectionDisplay.cpp` the position of the selected character.
- `UpdateCharacterList` calls `InnCastApplySavedOrder` right after `EnumerateCharacters` so the saved order is applied to the list.
- `CharacterSelection_Scripts.cpp` registers the Lua function `MoveCharacter(from, to)`, which returns the new selected index and whether anything moved.
- The room is drawn by `InnWmoBackdrop.h` as extra batches; doodads are ordinary scene M2 models.

## How it fits together

- **Cast.** The character list index is the walker index. Characters shown at once are limited by `WOW_INN_CAST` (at most 24). The list can hold more; the rest wait outside (list entries up to 100 are tracked).
- **Plan.** At open, the stay group (the selected character, `lastCharacterIndex`, the first `WOW_INN_STAY_FIRST` entries, and `WOW_INN_STAY_NAMES`) goes in first, then the lowest indices up to capacity. The first `WOW_INN_CAST_START` are built at once, the rest are built one at a time through the entrance points.
- **Rotation.** After `WOW_INN_ROTATE_FIRST` seconds and then every `WOW_INN_ROTATE_MIN` to `_MAX` seconds, one character outside the stay group walks to the nearest entrance node and is freed, and the one who has been away longest walks in.
- **Click on an outside character.** The character is built, enters through the nearest door, walks (runs after `WOW_INN_COME_WALK_SEC`), and the camera zoom and greeting wait until they arrive.
- **Walking map.** `InnWalk.cfg` holds points (`WP`), links (`LINK`) and spots (`SPOT`: drink, lean, sit, circle, away, entrance) in the inn's own coordinates. Entrance spots are the three doors.
- **See-through.** A line from the camera to the zoomed character fades furniture it passes through and skips wall triangles on it (`WOW_INN_FADE*`).
- **Drag reorder.** The Lua drag code calls `MoveCharacter`. C++ swaps the two entries in the list and remaps every per-index array. The order is saved in `InnCharOrder.cfg` by lower-case name and re-applied the next time the list loads.

## Checks to run

- `WOW_INN_CAST=0`: the normal select screen appears and nothing inn related is drawn.
- `WOW_INN_CAST=<n>`: the inn appears, `Logs\Extensions.log` shows `InnCast plan: listed=... capacity=...`, and furniture reports `failed=0`.
- With more characters than capacity, `InnCast swap:` and `InnCast left: ... walked out through a door` lines appear every 30 to 45 seconds. The client must not crash.
- Click a character who is not in the hall: an `InnCast click` line appears, the character walks or runs in, then the camera zooms.
- Drag a name: the list shows the new order at once; leaving and re-entering the screen keeps it; `InnCharOrder.cfg` exists.
- After any change, check for a new `Errors\*Crash.txt`.

## Problems we hit and how we fixed them

| Symptom | Cause | Fix |
| --- | --- | --- |
| Inn looks far too big or characters look tiny | The select scene's units are about 0.44 of a yard (characters stand 1 unit apart). | The room is scaled to match (anchor matrix scale 0.451 is applied by the backdrop). Tune only with `WOW_INN_WMO_*`; do not rescale the characters. |
| Inn does not follow the camera or flickers away | The room must be positioned relative to the camera each frame. | `Draw` takes the camera position and places the room per frame. Keep the camera-relative placement. |
| Furniture missing or frozen | Doodad models need per-frame `SetAnimating` and the 0x8 flag, and the right doodad set. | `WOW_INN_WMO_DOODAD_SET` (2 for the Goldshire inn) and the per-frame calls in the backdrop. Do not skip them. |
| Cast disappears when a character is selected | `ShowCharacter` detaches all children. | `InnCastShow` re-attaches the cast after every `ShowCharacter`. |
| Selected character jitters after the bow or salute | The idle pose was re-applied every frame after the emote ended. | `s_innEmoteIdled`: the idle pose is set once when the emote finishes and reset when a new emote starts. |
| Models without a head bone are framed wrongly on zoom | Body height was guessed from the bounding box. | Per race `WOW_INN_CAST_BOUNDS_DIV_RACE_<id>` (height = bounding box / divisor) and `WOW_INN_CAST_FRAME_RACE_<id>` (distance multiplier). Tune per race by eye. |
| Some races sit beside the chair instead of on it | Their sit animation sits the body to one side of the chair seat. | `WOW_INN_SIT_SIDE` and `WOW_INN_SIT_SIDE_<raceId>` (worgen, race 22, has its own default). |
| Railings and furniture hide the zoomed character | Geometry between the camera and the character. | The see-through line (`WOW_INN_FADE*`). Use mode 2 to hide instead of fade. |
| Client crashed (access violation, read of 0x48) a few minutes after the screen opened | A character reached a door and was freed (`m_component` set to null) while a per-frame loop still used its "ready" flag from before the removal. | Every loop that reads `entry.m_component` must null-check it after removals. The animation-test loop and the placement loop in `InnCastWalkers` now do. When you add a loop over `s_characterList`, check the component first. |
| Drag works but the list does not redraw until you click another name | The list on screen is the Esteria roster (`ECS_Integrate.lua`), which draws the rows from its own copy of the data and only rebuilds it on selection or list update, not when the stock rows are refreshed. | After a drop, call `ECS.Integrate.PostUpdate()` (done in `CharacterSelect_RedrawEcs`, once at once and again three frames later). If your client has no ECS roster, the stock `CharacterSelect_RefreshRows()` is enough. |
| Editing `Patch-ZY.mpq` with the append tool wrote nothing | That archive is MPQ version 1; the append tool only handles version 0. | Do not edit it. Create a new one-file archive that sorts after every other archive containing `CharacterSelect.lua`. The client sorts patch archives by name and opens them in reverse order with rising priority, so the last name wins. |
| The Lua patch step stopped on a hash check | The patch was made from an older copy of `CharacterSelect.lua` than the one in the live archive. | Always read the member back from the live archive stack and build the patch from that, then verify the hash before and after. |
| A character picked to leave turns back or never reaches the door | The stay logic can cancel a leave, or a walker has no path to an entrance. | A leaving character that does not reach a door within 60 seconds is removed anyway. If it happens often, check `InnWalk.cfg` links to the entrance spots. |

## Known open items

- The conversation circle rarely reaches four people; a stronger pull or a longer circle stay may help.
- Some seats (fireplace floor sits, stool legs) are not perfectly aligned.
- The bar counter can be walked through in places.
- A character who has just arrived can be chosen to leave again soon after; a minimum stay time is not implemented.
- Not built from this published tree. A cold-start crash seen once during development was not proven fixed.

If you fix one of these, record what you changed, the log lines that show it works, and what the user saw in game.
