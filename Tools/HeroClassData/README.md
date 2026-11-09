# Hero class DBC foundation

These tools prepare a paired Wrath 3.3.5a class-ID-10 data slice from the effective DBCs of the intended client. They do not make Hero playable by themselves.

## Inputs and outputs

- `extract_effective_hero_dbcs.py` extracts the current effective client tables from the selected client archives using the locally approved StormLib helper. The selected client `ChrClasses.dbc` must match the pinned hash in the generator.
- `build_hero_dbcs.py` appends class 10 to `ChrClasses.dbc`, adds class 10 to each race represented in effective `CharBaseInfo.dbc`, and initializes relevant class-indexed game-table slots from Paladin while preserving all other records.
- `package_hero_dbc_mpq.py` packages only generated DBCs into a new standalone patch MPQ and writes per-entry/archive hashes. It refuses to overwrite output directories.
- Paired server foundation: `server/data/sql/updates/pending_db_world/rev_20261105010000000_hero_class_data.sql`. It adds the `chrclasses_dbc` SQL override plus class stats, race start rows, and selected class-indexed GT overrides. It is pending SQL only and has not been applied to Acore2.

## Current prepared data

- Effective client DBC inputs: `work/hero-classless-20261105/client-baseline/DBFilesClient/`; receipt: `work/hero-classless-20261105/client-baseline/effective-client-dbc-inputs.json`.
- Generated override DBCs: `work/hero-classless-20261105/client-hero-dbc-patch/DBFilesClient/`; receipt: `work/hero-classless-20261105/client-hero-dbc-patch/hero-dbc-receipt.json`.
- Standalone MPQ: `work/hero-classless-20261105/client-hero-mpq/Patch-zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz-AetheriaHeroData.MPQ`; receipt: `work/hero-classless-20261105/client-hero-mpq/hero-dbc-mpq-receipt.json`.

The pinned CoA charge catalog now has a matching generator: `build_spell_charge_dbcs.py`. It takes the server's `CoASpellChargesData.h` plus the prepared Hero `DBFilesClient` directory and writes a *new combined directory* containing the existing Hero DBCs plus `SpellCharges.dbc` and `SpellChargesCategory.dbc`. It refuses to overwrite outputs or accept conflicting recovery-root definitions. The generated rows are sourced from Kalibros' `STRHercules/EsteriaWoW` commit `bdad58028fb6847b9d9a4fd8cf990bd662c9fc62`; 162 spell links map to 55 shared recovery roots. The DBC link key is the shared root, matching the server packet key; the source category field remains available in server metadata. Use the combined directory as `--dbc-dir` when preparing a later standalone MPQ. This has not been run and is not part of the staged MPQ yet.

The DBC patch and SQL use class ID 10, Paladin-compatible initial progression, mana as the resource type, and a unique `SpellClassSet` 16. These are scaffolding choices pending the Hero-specific CoA/classless rules and UI asset work. No active client MPQ or live server file/database was changed.

## Still required before Hero can be considered ready

**Owner scope invariant:** the Classless/CoA system is exclusively for Hero class ID 10. Every classless spell acquisition/eligibility path, player lifecycle and resource hook, command, NPC/addon protocol, world-drop, pet, and spell effect must reject or ignore non-Hero players before any side effect. A global module enable flag is not a substitute for these per-player gates. Existing Classic classes and their gameplay remain available and retain their current behavior.

1. Port Kalibros' classless/CoA mechanics with every hook gated to Hero so classic classes retain stock behavior.
2. Add matching, reviewed spell/talent/action-bar data and define Hero stats/resources rather than treating the Paladin donor rows as final balance.
3. Confirm starting-zone behavior against the current live world DB before applying the pending SQL.
4. Review the staged 11-choice creator layout, Hero card art and tooltip visually in-game, then check remaining client/server class-ID consumers.
5. Run the required source checks/builds later under the project guardrails, apply matched client/server data only through the approved workflow, then obtain gameplay acceptance.

No build, test, install, live DB operation, game/server launch, or publication was performed for this foundation.

## Hero creator picker and card art (2026-11-05 continuation)

Staged the 11th class-capacity change in the creator patch source and both staged locale Lua files. The custom class picker now plans six columns, so 11 class buttons occupy two rows; active-client XML confirms button 11 exists. Generated a separate Hero emblem in the visual style of the existing Aetheria class-card icons and converted it to BLP2 raw BGRA32 at 128x128 with a full mip chain matching existing card dimensions/mip sizes.

- Source image: `work/hero-classless-20261105/hero-class-card-source.png`
- BLP payload: `work/hero-classless-20261105/client-hero-art-payload/Interface/Glues/CharacterCreate/AetheriaCreatorClass-v1-HERO.blp`
- BLP SHA-256: `ff330b6da2a5c5f12cab5276bade907d2263be0d24d89a90faea4c91686cfbec`
- Source SHA-256: `79000a98f09e4a48b6b9e00cb1a5db2aad4c04c0e672036bdb0335a935d50010`
- Conversion script: `client/Tools/HeroClassData/build_hero_class_art.ps1`
- Receipt: `work/hero-classless-20261105/hero-class-art-receipt.json`

The creator picker and art are staged only. Hero flavor/details and icon-panel mapping remain to be connected. The new asset is not in the standalone DBC MPQ yet, and no MPQ/client archive was changed.

Current integrated client package: `work/hero-classless-20261105/client-hero-ui-mpq-v3/` contains the generated DBC overrides, the new Hero BLP card art, and staged `Interface/GlueXML/CharacterCreate.lua`. It has 13 entries and is receipt-hash verified. Its archive SHA-256 is `e54a023f73838f8fba0de78d03f13f88e97e357f0faed39795f5bb025337fbaf`. Earlier DBC-only and UI package revisions are preserved as prior drafts. This v3 package is still not a usable release because the server-side CoA/classless system and Hero-only gating are not implemented.

The staged creator has the 11-class limit, two-row layout, Hero card art path, classless tooltip description, and all-role labels. The active client's frame 11 was verified in its XML. The client `CharacterCreateClassIcon`/details widgets are optional and the new path does not assume they exist; Hero's description is supplied through the current class tooltip and info-text globals.
