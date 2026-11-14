# Esteria Inventory

Native digital carried inventory for the matched Esteria AzerothCore and wowdecomp client.
The window follows the supplied five-column, dark inventory reference. It replaces bag opening
after a successful native client/server handshake. It keeps twenty reusable item buttons while
the server searches and paginates the entire collection.

Discovery first sends `ESTERIADI / HELLO:1` through the stock addon whisper channel to the character.
Only a matching core returns `READY:1`. The addon sends no custom inventory opcode until that reply
arrives from the character's own whisper channel. Older cores can echo HELLO safely and retain normal
bags. This also covers manual `/inventory` requests, zoning, discovery timeouts and UI reloads.

## Current delivery

The native client/core builds and isolated database/UI checks passed. The character migration is
applied, and the rebuilt live worldserver is running with digital inventory enabled. The matched
executable is `G:\3.3.5a - Dev\WowClientRetailInventory.exe`; the addon is installed in that client.
Startup and ownership audits passed. **In-game acceptance is underway.** The source distribution
still defaults to disabled; the live configuration explicitly enables it.

The deployment record and pre-activation character database/configuration backups are under
`.agents/plans/esteria-digital-inventory/`. The Docker Compose override pins the active image tag,
so an ordinary service restart preserves the deployed build.

The integration is a core subsystem rather than a module-only hook: carried counts, consumption,
character login/save/delete, packet registration, and character dump imports need coordinated changes.
Client networking is the discoverable `DigitalInventory` extension in the adjacent wowdecomp checkout.

## Behavior

- `/inventory` or `/inv` opens the inventory. Normal bag keys open it after negotiation.
- `/inventory legacy` toggles ordinary bag opening; `/inventory 0.8` sets the preferred window scale.
- Search item names or numeric IDs; filter equipment, consumables, materials, other, or favorites.
- Sort by name, quality, count, or category. Mouse wheel and arrow buttons change twenty-item pages.
- Left-click a digital stack to withdraw it into an empty physical action slot.
- Right-click to materialize and use/equip it through stock server validators; at an open merchant,
  right-click requests a sale validated against the actual merchant GUID.
- Alt-click toggles a stored stack's favorite; Shift-click splits a stored stack.
- Drag a stored item to withdraw it, then drag its carried icon into trade, mail, socketing, or auction UI.
  Carried item buttons retain the native bag handlers, including large-bag slot numbers through 237.
- Deposit All moves eligible carried stacks; Stack combines identical instances up to ordinary stack limits.
- Expansion purchases virtual positions at the server-supplied price. The server checks the offered price,
  current revision, funds and capacity before charging. Defaults: 256 positions, +256 for 100 gold, up to 10,000.

Capacity counts virtual stacks. Ordinary bag slots remain an internal action reserve and are also visible
in the same grid. Equipment and bank contents are outside this window. Favorites follow stored stack
instances and are not retained when a stack leaves virtual storage.

The server includes digital items in carried-item, unique-item, quest and crafting reagent checks and
consumption. The native client receives a separate aggregate index so crafting availability, reagent
counts and profession tools work independently of the visible page.

Automatic deposits run after newly acquired ordinary inventory has settled, outside combat/casting/trade.
Recently withdrawn items are pinned in physical storage for thirty seconds. Timed, conjured, refundable,
temporarily tradeable, wrapped, looted, map/zone/holiday-limited, currency, bag, temporary-enchantment,
and on-obtain-effect items stay physical and remain visible. This avoids pausing their special lifecycles.
Items bound to action bars retain one physical stack; the server refills it from virtual storage as needed.

## Persistence and recovery

`item_instance` retains full GUID, enchantment, charge, binding, random-property, durability and text state.
`character_digital_storage` stores per-character capacity/revision; `character_digital_storage_item` stores
the item-to-character mapping and favorite flag. A move removes one location and creates the other in the
same transaction. Materialization selects an empty slot and preserves the selected item's GUID.

All explicit digital changes enter the existing character database writer queue and wait for a successful
commit before replying. **Exactly one character database worker is required.** This is AzerothCore's
default. The feature refuses activation with any other worker count, preventing older queued inventory
or money saves from committing after a newer digital transaction. This first implementation can stall
the requesting world/map update while a slow database transaction completes.

Failed ownership commits disconnect the player and suppress inventory/gold/logout saves so a reconnect
loads the last durable state. Login checks competing physical, mail, auction and guild-bank locations,
ownership, and missing instances. Conflicts refuse login for investigation; they are not cloned, discarded
or automatically repaired. Character deletion, individual item deletion, faction conversion, GM item
queries, and character dump GUID remapping include digital ownership.

Before disabling or rolling back the feature, recover stored items into ordinary inventory/mail with
an audited migration. Disabling the feature does not withdraw stored items. Retain the two new tables
and matching source until that recovery is complete. Reverting binaries alone strands virtual items.

## Activation prerequisites

Native builds were explicitly authorized. Live activation requires these steps:

1. Apply `data/sql/updates/pending_db_characters/rev_20261018030000000_digital_inventory.sql`
   before starting the rebuilt worldserver: it prepares queries against these tables even when disabled.
2. Build the modified core and the existing matched wowdecomp configuration with
   `WOWDECOMP_EXTENSION_DIGITALINVENTORY=ON`. Preserve the live Satchel container/quest/class layout.
3. Install the addon and copy the supplied `conf/dist/digital_inventory.conf.dist` settings into
   the active worldserver configuration. Keep `CharacterDatabase.WorkerThreads = 1`.
4. Use an isolated test character and complete native acceptance before enabling normal play.

## Verification

SQL tests execute the actual prepared statements in an isolated SQLite harness, covering mapping
ownership, instance preservation, rollback, upgrade debit rollback, conflict detection and cleanup.
The MySQL validation script also passed against a disposable MySQL 8.4 container with no network:
it prepared eighteen actual queries and checked the migration, transaction failures, ownership,
instance metadata, upgrade debit rollback, conflicts, faction conversion and withdrawal.
These tests do not execute the C++ manager in a running game server.

```powershell
rtk proxy python -m unittest tools.test_digital_inventory_queries tools.test_stage_esteria_inventory -v
rtk proxy npm exec --yes --package=fengari-node-cli -- fengari tools/tests/esteria_inventory_ui.lua client-customization/EsteriaInventory/EsteriaInventory.lua
rtk proxy npm exec --yes --package=fengari-node-cli -- fengari tools/tests/esteria_inventory_ui.lua client-customization/EsteriaInventory/EsteriaInventory.lua legacy
```

The Lua harness executes the actual addon and checks the handshake/fallback, twenty-button grid bounds,
high physical slots, search, pagination, requests, upgrade quote, Satchel coexistence, zoning reactivation
and refusal to replay timed-out mutations. Both normal and legacy modes passed under Fengari and the
client's actual patched Lua 5.1.1 library, using `Extensions/DigitalInventory/tests/LuaHarness.cpp`.
The host supplies FrameXML fixtures; the real game UI still needs in-game verification.
An additional `oldserver` harness mode verifies zero custom opcodes on login, echoed discovery,
timeout, manual requests and zoning against a core without the extension.

Scoped repository codestyle checks passed for the new manager/header and character migration.
Repository-wide checks encounter existing violations, a binary file decoded as text by the C++
linter, and the SQL linter's unavailable `origin/master` comparison. They are not clean CI results.

Use `ACCEPTANCE.md` for the remaining native gameplay checks and an ownership audit query.

Remaining game acceptance includes equip/use failure paths, targeted/ground-targeted item spells,
action-bar reserve/refill, tooltip details, controller focus, merchant buyback,
rapid loot when the physical reserve is full, disconnects, relogs, crash recovery, MySQL failures,
quest rewards, profession crafting, unique socketed gems, legacy addons, and character dump round trips.
Targeted/ground-targeted item spells and mail/trade/socket/auction cursor flows may require explicit
withdrawal and the ordinary native UI; they are not a fully virtual rewrite of those systems.

## Settings and variable pages

`/inventory config` (or Interface Options > AddOns > Esteria Inventory) edits: tile scale, tile spacing,
max tiles per page (1-255), window scale and opacity, position and size locks, per-tab and per-sort
visibility, bag-key takeover, item names on large tiles, quality borders, sounds, tooltip hints and
remembering the last category/sort. `/inventory reset` restores every default.

Drag the bottom-right grip to resize (tiles refit; the page size follows the window); Shift-drag scales.
Browse requests carry the wanted tile count in the existing `count` field (1-255; 0 means 20), so the
wire protocol is unchanged. A server that predates this always answers with 20 rows; the addon detects
that and stops asking for more.

## Stacks, tags, menu, gold and currencies (protocol v2)

- Stored stacks merge automatically up to 1000, whatever the item's normal stack limit. Withdrawing
  carves out at most one ordinary stack. Splitting a stack marks both halves "split": automatic
  merging skips them until the **Stack** button is pressed, which merges everything and clears the marks.
- Right-click menu (off by default; enable in options): Equip/Use, Withdraw, Favorite, Split, Set tag,
  Disenchant, Socket gems, Link in chat, Destroy (and Sell at a merchant). Disenchant, Socket and Destroy
  withdraw the stack first, then act on the carried copy. WotLK has no gem-removal mechanic, so there is no Unsocket.
- Tags: free text up to 24 characters, stored per stack on the server. Sort by Tag groups tagged stacks
  alphabetically ahead of untagged ones; the search box also matches tags.
- Gold (on by default) and any ticked currencies (Interface Options > Esteria Inventory > Currencies)
  show above the capacity bar.
- Empty and filled tiles sit on `Textures/SlotBackdrop.tga`. With DragonUI, the backpack button takes
  `Textures/MenuIcon.tga` and the four bag slots and fold arrow are hidden (option: single bag button).
- Server migration: `rev_20261018140000000_digital_inventory_tags.sql` adds `tag` and `split` columns.
  Apply it before starting a core built from this source.

## Account bank (protocol v3)

- One bank per account on the realm, stored in `account_bank` / `account_bank_item` (items keep `item_instance`
  data with owner 0). Initial size 512 stacks; size increases cost 100g, rising 25% each time, up to 100,000.
  Remote access costs 1,000g once per account; bankers are always free. All values are `DigitalInventory.Bank*`
  config options.
- The banker opens an independent Bank window (stock bank frame is suppressed). The bag window's Bank tab shows
  the same storage remotely once unlocked. Left-click in the bank moves a stack to your bags; right-click a bag
  item with the banker open (or "Send to bank" in the item menu) deposits it.
- Items still in the stock bank slots (and bank bags' contents) move into the account bank on first login.
- Protocol v3: requests carry a scope byte (0 bags, 1 bank); snapshots carry scope, unlock fee and a flags byte.
- Apply `rev_20261018150000000_account_bank.sql` before starting a core built from this source.

## Guild bank window

The stock guild bank frame is replaced by a third window in the same style (it is loaded at login and its events
are removed, so it never opens). Item contents, permissions, withdraw limits, logs and tab text still use the
server's stock guild bank packets, so rank rights, per-tab view/deposit rights and daily limits behave exactly as
before. Tabs are real guild tabs (unviewable ones are disabled).

- **Size:** every tab now has 128 slots. The guild master can expand a tab (Expand Tab) in steps of 16 up to 250
  slots; each step costs more than the last (`Guild.BankTabExpandCost` 250g, `Guild.BankTabExpandGrowth` 1.25,
  `Guild.BankTabExpandSlots`, `Guild.BankTabMaxSlots`). 250 is the ceiling because stock packets carry slot IDs
  in one byte and 255 means "no slot". Tab purchases cost the stock price times `Guild.BankTabCostMultiplier` (3.0).
- **Rename:** the guild master renames the selected tab with Rename Tab.
- **Log, Money Log, Info:** buttons under the tabs show the item log, the money log and the editable tab info.
- Search, sort and paging run on the client over the selected tab; left-click picks up or drops, right-click
  withdraws, Shift-click splits; right-click a bag item with the vault open to deposit it into the selected tab.
- Tags and 1000-stacks do not apply to the guild bank. Turn the window off with the "Esteria guild bank window"
  option and a /reload to get the stock frame back.
- Apply `rev_20261019100000000_guild_bank_slots.sql` (adds `guild_bank_tab.slots`, default 128).
