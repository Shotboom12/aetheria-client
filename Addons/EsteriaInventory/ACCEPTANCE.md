# In-game acceptance

Run these checks on an isolated character with the matched native client and rebuilt core.
Apply the character migration first, then enable `DigitalInventory.Enable` with exactly one
character database worker. Start with disposable items and 256 virtual positions.

| Check | Expected result |
|---|---|
| Open bags with B, then `/inv` | Both open the five-column inventory after negotiation; twenty buttons are reused across pages. |
| Search, clear search, select each category, change sort, page with the wheel | Results agree with the character's collection; displayed capacity does not change with filters. |
| Deposit enchanted, random-property and damaged equipment, then relog | GUID, enchantments, random stats, binding, text and durability are unchanged. |
| Withdraw a stack while another compatible stack is carried | The selected GUID occupies an empty action slot; the other stack is unchanged. |
| Split, favorite and stack matching materials | Counts are conserved, ordinary maximum stack sizes apply, and favorites survive a stored-stack merge. |
| Attempt to combine equipment or materials with different instance metadata | Distinct instances remain distinct. |
| Equip stored gear; attempt an invalid equip | Stock level, class and combat restrictions apply; a rejected action leaves the item carried. |
| Use a stored potion; attempt use during cooldown | Stock charges and cooldowns apply without duplicate consumption. |
| Bind a consumable to an action bar, exhaust its physical stack | A replacement physical stack is refilled from virtual storage when an item action is permitted. |
| Craft using only digitally stored reagents and profession tools | Client availability and server consumption agree; counts decrease exactly once. |
| Complete a quest requiring stored items, then take an item reward | Quest detection and consumption work; the new reward appears and settles into storage. |
| Try to acquire a second unique item already stored | Ordinary unique-item limits apply. Include socketed unique gems. |
| Sell stored gear at a merchant, then buy it back | The selected instance is sold once; ordinary buyback preserves its instance state. |
| Withdraw, then drag to mail, trade, auction and socket UI | Native carried-item cursor flows work with the materialized GUID. |
| Purchase expansion; attempt with insufficient funds | Capacity and gold change together; a rejected purchase changes neither. |
| Fill virtual capacity, then loot with and without free physical slots | No item is lost; native inventory-full rules apply when both locations are full. |
| Zone, reload UI, relog, disconnect during a request | Negotiation resumes; mutations are never replayed automatically; each GUID has one location. |
| `/inventory legacy`, ordinary bags and Satchel bank bags | Legacy carried bags remain available; bank bags retain their native behavior. |

Targeted or ground-targeted item spells and readable/openable item flows may require withdrawal
followed by the ordinary item button. Validate these explicitly for the items used by Esteria.

On an isolated server/database, also test interrupted transactions, database write failures and
process termination during transfers. Reconnect must load the last committed inventory. A failed
ownership transaction should disconnect the character and prevent a later logout save from
overwriting that durable state. Export/import a character dump into a different character GUID,
then verify every digital item mapping points to the newly assigned item GUID.

Ownership audit after each recovery check:

```sql
SELECT d.guid, d.item, i.owner_guid, c.item AS carried_item
FROM character_digital_storage_item AS d
LEFT JOIN item_instance AS i ON i.guid = d.item
LEFT JOIN character_inventory AS c ON c.item = d.item
WHERE i.guid IS NULL OR i.owner_guid <> d.guid OR c.item IS NOT NULL;
```

The audit must return no rows. Mail, auction and guild-bank conflicts are also checked by the
native login loader. Investigate a refused login instead of deleting or duplicating mappings.
