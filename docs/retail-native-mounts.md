# Native Retail mounts

Mastercraft Gravewing is the first additive mount pack. Original Blizzard files are
acquired with CascLib from the same pinned Retail build as the Arakkoa sword pack.
The installed asset store contains original decoded CASC bytes. The engine decodes
those files into runtime memory; no converted M2, SKIN, animation or BLP is emitted.

## Identity

| Namespace | Retail | Esteria |
| --- | --- | --- |
| Mount | 803 | Collection through summon spell |
| Learning item | 186479 | 1000001 |
| Item display | — | 160001 |
| Summon spell | 215545 | 300000 |
| Creature | — | 4500000 |
| Creature display | 100108 | 200000 |
| Creature model | 12614 | 200000 |
| Spell icon | Original icon FileDataID 3852563 | 600000 |
| SkillLineAbility | — | 400000, skill line 777 |

The resolved recipe is `config/mastercraft-gravewing.json`. Build-matched Mount,
MountXDisplay, CreatureDisplayInfo and CreatureModelData records identify model
3852506 and brown body/saddle textures 3852567/3852551. The original icon is also
downloaded. Eleven original files include normal and Retail LOD skins, fixed textures
and the selected replaceable textures. Only normal advertised profiles are opened
by this engine; unused LOD originals remain preserved.

## Integration

`native_mount.py` reserves IDs against client providers, server DBCs, SQL overlays
and persistent SQLite allocations. It clones the running server's flying-carpet
spell 61309, adds a new mounted creature/display/model and mount collection row,
and creates a consumed learning item. Target gameplay is +100% ground / +280% flight,
with a level-one learning item for testing and no Retail covenant restriction.

The client merges seven supplemental tables through its normal database loader.
The server receives four uniquely named DBC continuations plus guarded additive
item/creature SQL. Base archives and base server DBCs are preserved. SpellIcon is
client metadata and has no corresponding server store.

D3D11 handles the source's large bone palette with a larger constant buffer. The
native material path supports its third saddle glow texture, preserving stock
reflection, lighting, fog and shadow permutations. Constant camera FOV tracks and
single-texture ribbon tracks are decoded in memory. Shared SKELs, chunked external
animations, animated camera FOV and Retail particle layouts remain unsupported.

## Validation

`RetailAssetTests` checks bounded parsing and additive mount table merges, including
preservation of existing Esteria spell IDs above thirteen million. `RetailShaderProbe`
extends and compiles all sixteen stock reflection permutations through the actual
D3D11 shader translator. `check_server_install.py` exercises installation, rerun and
collision rollback in disposable MySQL tables. `verify_install.py` verifies original
asset hashes, SQL definitions, every server continuation and all seven mounted tables.

The installed pack's `receipt.json` records the current evidence state. Learning,
preview rendering and actual mounted-world rendering are distinct checks. Live
acceptance also includes rider fit, movement, flight, dismounting and persistence.

On 2026-10-17, the D3D11 world test showed the full summoned mount beneath Kekelol
in Valley of Trials, with its saddle, wings and rider attachment visible. The current
D3D11 log compiled `Retail_Opaque_Reflection_Glow:4` successfully. Learning and
collection registration were also confirmed. Movement, flight and persistence still
need separate acceptance; the login overlay alone was never evidence for those.
The subsequent world test exposed intermittent stretched scene geometry even while
dismounted, also visible in the minimap. Full visual acceptance remains pending that
renderer defect. `WOW_D3D11_DEBUG=1` records driver validation warnings and errors in
the current `Logs/d3d11.log` for diagnosis.

The normal launcher is `LaunchRetailNative.cmd` beside the client executable. It
clears diagnostic preview variables and passes `-d3d11` explicitly. Without that
argument, the target defaults to D3D9, whose 256 vertex constants cannot hold this
mount's 104-bone section palette. That mismatch leaves the rider and ribbons visible
while the body fails to render. The isolated preview already selected D3D11, which
explains why it could render while the normal world launch could not. Renderer
diagnosis must use the current launch's `gx.log`, not a stale `d3d11.log`.
A screen preview is diagnostic and does not
substitute for a server mount. Existing learned items/spells must be considered
before removing a pack; IDs are never recycled automatically.

## The Writhing Brood (Midnight 12.1)

`config/writhing-brood.json` (Retail mount 3058, display 145412, model 7551444) is the first
ground mount and the first model using chunked `.anim` files. Additions it required:

- Model-only chunked animation files: an `AFM2` chunk wraps the legacy `.anim` payload; the header
  is dropped when the file is opened (originals stay untouched on disk). Shared-skeleton animations
  can also contain `AFSB` bone and `AFSA` attachment payloads. Those files retain their chunks, and
  validation/runtime relocation select the corresponding payload base for each track category.
- Particle emitters use a newer layout and are **omitted** (count cleared); mesh, skinning,
  ribbons and animation are unaffected.
- Explicit shader 0x15 (`Diffuse_EdgeFade_T1_T2` / `Combiners_Mod_Mod`) maps to
  `Diffuse_T1_T2` / `Combiners_Mod_Mod` without the edge fade (no EdgeFade vertex shader in 3.3.5).
- The batch geoset index is not used by the legacy renderer and is no longer range-checked.
- Historical implementation used a Fly/Swim ID swap. The current direction-aware runtime mapping below replaces that swap; keep original source IDs intact. The plain mount spell visual keeps the rider seated.
- The mount is a ground mount: its donor spell is a +100% ground mount, and the creature
  display scale is 1.6 as in Retail.

## Flying mount preparation — required before first installation

### Confirmed repair, 2026-10-18

User confirmed both Hexed Vilefeather Eagle and Void-Corrupted Hex Eagle: “They work perfectly.” Skyfriend and Gravewing were already reported good after the shared mapping correction. Accepted client EXE SHA256: `3BC38EB44A211D072EC8C462475EE47444924E08D8E63E47DF2A3C0A01A7B02A`. These confirmations concern flight behavior; they do not resolve Gul'dan Wolf's separate mesh binding/face-effect issues.

### Preparation and diagnosis steps

1. Inspect the original model's sequence IDs, durations, alias chains, variation chains, probabilities, and required external AFID files. WotLK requests SwimIdle (41)/Swim (42) while airborne. Retail models may author aerial motion under FlyStand (229), FlyRun (234), FlyWalkBackwards (242), FlyFly (364), or Fly (135). Do not assume one family's presence from another mount.
2. Keep source sequence IDs and AFID filenames intact. Resolve airborne requests at the unit animation boundary instead of swapping IDs inside the prepared model. Ground movement and real swimming must continue using their normal animation families.
3. Resolve every animation refresh, including cycle-end callbacks. Verify that already resolved aerial IDs do not pass through generic tier fallback and become an inappropriate locomotion sequence. For supported eagle displays 200003/200004, resolve direction before tier fallback AND at final SetKeyBoneSequence playback: actual backward movement ->242; forward/strafe/ascent/descent ->234; stationary hover ->229. Use actual movement flags, not the incoming animation's name, to decide direction.
4. Restrict corrections to the intended native model/displays and known locomotion IDs while flying. Preserve takeoff, landing, attacks, spells, emotes, other mounts, and non-flight behavior. Verify required clips exist before adopting this mapping for another model; 364 or another authored sequence may be necessary.
5. Inspect random variations separately. Eagle234 has variation0 (1600ms) and variation1 (3200ms); explicit variation0 disables random swaps for its moving flight. Variation locking alone did NOT fix the observed bug. Do not infer a random variation solely from a visible pose change. Keep authored glides when appropriate and desired.
6. If a switch remains, capture bounded request/cycle diagnostics: display/model, requested and active sequence, variant/index/random flag, duration, speed, and movement flags. Compare normal flight with the exact transition. The decisive eagle trace showed234 variant0/random0 changing to242 while forward+flying flags remained83000001. This proved a direction-selection conflict despite the working variation lock. Repair the evidenced selection path before changing source animations or shaders.
7. Build and check installed file hashes. Request game closure only when replacement needs it. This client-only repair required no server restart, relearning, SQL, shaders, asset modifications or new MPQ. Preserve the applicable known-good rollback.
8. Obtain sustained in-game confirmation over multiple cycles, not just takeoff or brief flight. Include forward travel, hover, backward/turning behavior and landing when confirming full coverage; record exactly what the user reports. Never claim unobserved subcases passed. Both eagle appearances are now user-confirmed working. The later accepted all-mount flight build and shared terrain fix are recorded below; use that section as the current state.

Implementation: `WoW/Source/Object/ObjectClient/Unit_C.cpp`, `ResolveAnimForModel`, and `SetKeyBoneSequence`; the former `ResolveEagleFlightMovement` helper is now the shared, tested `NativeMountFlight::Resolve` in `WoW/Source/Object/ObjectClient/NativeMountFlight.h`. Evidence and receipt: workspace `eagle-direction-fix-20261018/`.

### Hippogryph/Drake extension installed and user confirmed (2026-10-18)

User reported the same backward-flight switch on Argent Hippogryph (display200006, original model124410) and Azure Drake (display200008, model234564). Original sequence inspection showed different families: Hippogryph uses hover229/forward234/backward242; Azure Drake has no such family and uses hover41/forward42/backward45. Azure42 has authored variants0/1 (1666/4000ms); preserve both rather than assuming the alternate loop is the backward clip. Drake45 aliases534, whereas both forward variations resolve to42.

The shared resolver now scopes profiles to these displays and the accepted eagle pair. Use actual direction before tier fallback and final primary playback, check selected clips exist, and keep variation0 locking eagle-only. Direction regression checks exercise wrong incoming backward clips during forward/strafe/vertical motion, actual backward input, hover, takeoff, swimming and unlisted mounts. Builds and checks passed. Installed EXE `69739dc83e8d893a07e6f742484eb21ba13bbe7a2d793ed93bac0cbe6d86982f`, preserving the accepted glaive orientation correction. User confirmed: "The correction is successful for the two mounts." This accepts the reported flight correction; backward input and hover were not separately reported. Evidence: workspace `native-mount-flight-direction-20261018/`. No new MPQ, raw asset, shader, registry or server/database changes.

### Silvermoon ceiling cause and all-native strafes, installed and user-confirmed (2026-10-18)

The user's stock Bronze Drake comparison exposed an incomplete earlier diagnosis: `CGUnit_C::UpdateFlightStatus` requests backward45 and exertion sound every2000ms when near the ADT flight ceiling. Silvermoon tiles have an exact MFBO upper9=3/lower9=-1000, so low normal flight is already above that old no-fly ceiling. Eight providers across four local ADTs confirmed this. Do not mask this shared issue with more per-mount direction exceptions or by silencing normal sounds.

`LegacyFlightBounds.h`/`CMapArea::ProcessIffChunks` expand only that exact placeholder, in memory, to encoded upper32767 while preserving the lower surface. Every known chunk area must be fly-enabled and in the previously authorized old-world flight scope (continents0/1 or the seven map530 starting-zone roots). Other/unknown areas and real/sloped ceilings stay intact. Collision facets and warning queries use the same normalized data. No MPQ edits.

All seven added flying profiles now preserve authored strafe43/44: Gravewing200000 (forward135), Skyfriend200002 (234), both eagles200003/200004 (234), Hippogryph200006 (234), Azure200008 (42), Sunreaver200011 (42). All source models were inspected; their lateral clips are distinct from forward and from each other, and none has272/273. `NativeMountFlight::Resolve` uses Q/E also on diagonal travel, instead of collapsing them into forward. Keep per-model hover/backward families and eagle-only variation locking. Original model IDs, aliases, glides and raw assets are preserved.

Installed EXE `f6bfa4972049c508c2d926a55b9f51f5e5feceeaa4bd5aec6ee05eee10090c80`; builds, direction/ceiling regression checks and file readback passed. User confirmed: “They're perfect.” This accepts the reported flight behavior; do not infer additional unreported cases. Evidence: workspace `mount-flight-strafe-ceiling-20261018/`. The accepted glaive Z-rotation remains. No shader/registry/server/database changes or Kali deliverable. The maintained end-to-end import and issue-fix workflow is `RETAIL-CASC-ASSET-IMPORT-GUIDE.md` at the workspace root.

## Communication preference

Keep internal implementation notes for continuity. Prepare findings, reports, or packages for Kali only when the user explicitly asks. Nothing has been sent to Kali.
