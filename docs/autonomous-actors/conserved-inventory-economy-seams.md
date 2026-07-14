# Conserved Actor Inventory and Economy Seams

Date: 2026-07-14  
Wayfinder bead: `central-dcq7.9`  
Source state: [`bcf3473671d7f3967a0727e70b883c9254d97cd3`](https://github.com/thunderbump/bump-EQEmu/tree/bcf3473671d7f3967a0727e70b883c9254d97cd3), verified against `origin/master` on 2026-07-14

## Decision

Reuse the existing gameplay rules, item-instance representation, Bot Gear Value, Bot equipment storage, merchant lists, temporary merchant stock, and currency denominations. Do not expose the current packet handlers, `Client` inventory helpers, or Bot database helpers as Actor actions. Those paths combine useful validation with connected-client assumptions and sequential persistence that cannot prove conserved custody or currency after a partial failure.

The missing seam is a small, session-independent transaction authority shared by Client and Actor callers. It must accept identities and intent, resolve all mutable facts from authoritative live state, validate the complete operation before mutation, commit guarded custody and wallet deltas, and emit an Actor-shaped receipt only after the committed state is re-read. Strategy code may choose an objective or recommend a disposition, but it may not choose authoritative item instances, prices, balances, merchant stock, loot rights, or mutation order.

This is not a new parallel economy. Actors should use the same corpse, equipment, merchant, stock, pricing, lore, class, level, race, slot, and tradability rules as players. The new code is an authority boundary that lets those rules operate without pretending a Bot is a connected `Client`.

## Existing seams and reuse limits

| Capability | Existing authority | Reuse | Limit for Actors |
| --- | --- | --- | --- |
| Corpse access and loot | `Corpse::MakeLootRequestPackets` and `Corpse::LootCorpseItem` | Corpse/entity resolution, range and lock checks, looter contention, loot entitlement, lore/augment checks, quest vetoes, and exact `ItemInstance` reconstruction | Requires a live `Client`; coin is paid to its wallet; destination is its character inventory; source deletion follows destination insertion without one durable transaction |
| Character inventory | `Client::AutoPutLootInInventory`, `PutLootInInventory`, and `DeleteItemInInventory` | Slot, stack, lore, equip, level, race/class, attune, cursor, and inventory-capacity behavior | Persists by `CharacterID()` and optionally sends packets; it is not Bot custody or a session-independent transfer service |
| Bot equipment | `bot_inventories`, `BotDatabase::LoadItems`, `SaveItemBySlot`, and owner-to-Bot trade | Existing Bot equipment slots and item-instance fields; ordinary Bot equip legality and replacement behavior | Runtime load/save is equipment-only; no carried inventory or wallet; save replaces a slot with separate delete/insert operations; direct helpers bypass custody |
| Bot Gear Value | `BuildRequestForSuccessfulLoot` and scoring helpers | Deterministic, item-instance-aware comparison across eligible group Bots and valid equipment slots | Advisory only: it neither transfers an item nor proves inventory capacity, merchant value, custody, or successful equip |
| Merchant purchase/sale | `Handle_OP_ShopPlayerBuy` and `Handle_OP_ShopPlayerSell` | Merchant identity/range, list and temporary stock, faction/rule pricing, quantity, tradability, lore, inventory capacity, and player-facing event precedents | Connected `Client` opcode paths; sequential money, inventory, and stock mutation can leave partial outcomes; player events are account/character-shaped |
| Currency | `Client::TakeMoneyFromPP` and `AddMoneyToPP` | Existing copper-denominated arithmetic and character persistence | Stored in a Client profile and character row; Bots have no wallet; a reserved owner row is not automatically Actor money |
| Player trade, Bazaar, and Barter | `Client::FinishTrade`, `BuyTraderItem`, and `SellToBuyer` | Later reference for No Drop/lore checks, serials, transaction caps, offer state, compensation, and ordinary market visibility | Requires connected Clients, trade-window or trader/buyer state, packets, character inventories/wallets, repositories, and world messages; too broad for the first Actor economy slice |

The corpse path demonstrates both the valuable rules and the coupling: it establishes one active looter and player loot rights in [`Corpse::MakeLootRequestPackets`](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/corpse.cpp#L1219-L1497), and that list/request phase may also pay corpse coin into the Client wallet before any item is selected. It then validates and reconstructs the instance in [`Corpse::LootCorpseItem`](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/corpse.cpp#L1499-L1777). The item path inserts into Client inventory before removing the corpse item in the [successful-loot tail](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/corpse.cpp#L1756-L1858). The new seam should extract or share these rules; it should not reproduce them in an Actor-only implementation, and `ClaimCorpseItem` must not implicitly claim coin.

The destination side has the same distinction. [`Client::PutItemInInventory` and `PutLootInInventory`](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/inventory.cpp#L1054-L1147) are character persistence helpers, while [`AutoPutLootInInventory`](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/inventory.cpp#L1197-L1277) contains useful placement policy. Bot persistence accepts equipment slots in [`LoadItems`](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/bot_database.cpp#L973-L1037) and [`SaveItemBySlot`](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/bot_database.cpp#L1126-L1190), but the latter deletes and inserts separately. `BotTradeAddItem`, `AddBotItem`, and `RemoveBotItemBySlot` are low-level mutation helpers, not safe Actor commands ([Bot mutation helpers](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/bot.cpp#L3822-L3839), [direct add helpers](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/bot.cpp#L4171-L4246)). In particular, `AddBotItem` creates/persists destination state; it does not transfer custody from a conserved source.

The existing owner-to-Bot trade is the best compatibility oracle for equipping. [`Bot::FinishTrade` and `PerformTradeWithClient`](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/bot.cpp#L4345-L4775) enforce ownership, live trade state, combat, lore, class/race/level, slot, dual-wield/two-hand, and return-space rules. It still cannot be the production Actor API: it requires the owner `Client`, returns replaced gear to that Client, and continues through separately persisted mutations even when an individual save reports failure. The cursor-based No Drop command is a player convenience, not permission for Actors to bypass ordinary custody or market restrictions.

Bot Gear Value is already correctly separated from mutation. [`BuildRequestForSuccessfulLoot`](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/common/bot_loot_request.cpp#L1199-L1288) compares an exact looted instance with group Bot snapshots and returns the best positive request. It is suitable for ranking `equip` candidates and explaining why. It is not a vendor-price function, market-price function, inventory authority, or transaction result. An item with no positive equipment score may become a `hold`, `vendor`, or later `offer` candidate only after the strategy considers the whole party and the relevant authority says that disposition is legal.

Merchant code should likewise be split at the rule/transaction boundary. The [purchase path](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/client_packet.cpp#L14126-L14372) resolves ordinary stock and price but debits money before all destination failures are excluded. The [sale path](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/zone/client_packet.cpp#L14374-L14569) validates the item and merchant, then credits money, updates temporary stock, and deletes the item sequentially. Actor transactions should use the same quote and eligibility rules through a common service, not call these handlers or send synthetic packets.

## Custody, wallet, and conservation model

Authoritative ownership is deliberately explicit:

- A corpse owns its exact loot-list entry and item-instance fields until a committed transfer removes it.
- A Bot owns equipped items by `bot_id` and equipment slot in `bot_inventories`.
- No existing Bot-owned general inventory exists. Replaced equipment, held goods, consumables, and later market goods therefore have no accepted Actor custody location yet.
- A Client owns character inventory and currency through `CharacterID()` and its profile. The reserved owner character remains an ownership/lifecycle compatibility record; treating its bags or purse as the Actor's assets would be a separate design decision, not an implementation shortcut.
- A merchant owns content-list and temporary-stock availability under the existing merchant rules. NPC purchases and sales are intentional currency/item faucets or sinks only to the extent the existing rules define them.

Every transaction must maintain these invariants:

1. **Single custody:** each conserved item instance is in exactly one source or destination after commit, and remains in its original custody after a rejected or failed operation.
2. **Instance fidelity:** item ID, charges, stack count, augments, custom data, attunement, ornamentation, and any serial/fingerprint used for stale detection survive the move unless an ordinary rule explicitly transforms them.
3. **Balanced currency:** player/Actor-to-player transfers use equal and opposite copper deltas. Merchant faucets/sinks use the ordinary authoritative quote and stock semantics; strategy-supplied prices never become ledger facts.
4. **Preflight then guarded commit:** resolve live source, destination, principal, range, entitlement, capacity, lore, equip legality, merchant, stock, quote, and balance before mutation. Commit must reject stale versions or fingerprints.
5. **Idempotency:** one action generation/transaction key may claim and commit at most once. A retry returns the prior receipt or a stable rejection; it never repeats a debit, credit, insertion, or deletion.
6. **Confirmed postcondition:** success means an authoritative re-read proves the expected before/after custody and wallet state. Queue acceptance, a gear score, an in-memory insertion, or a database helper returning alone is not success.

The smallest useful internal boundary is a transaction coordinator over narrow authority adapters, not a general Actor inventory abstraction. Its first explicit operations should be `ClaimCorpseItem`, `EquipFromCustody`, `MerchantSell`, and `MerchantBuy`. Client and Actor adapters should supply principals and presentation, while the operations share authoritative custody, wallet, validation, guarded commit, and durable idempotent-receipt behavior. The first implementation needs only corpse custody, an empty Bot equipment slot, and an Actor-shaped receipt; merchant stock/quote and a deliberate wallet/carrying-custody choice come afterward. This keeps the first proof small without making the Client path and Actor path separate economies.

The current schema is not itself a concurrency guard. The bootstrap gives `bot_inventories` a surrogate primary key and a non-unique `bot_id` index, but no uniqueness constraint on `(bot_id, slot_id)` ([bootstrap definition](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/utils/sql/bot_tables_bootstrap.sql#L286-L313)). Any durable equip commit must therefore enforce or add an equivalent guarded single-slot invariant rather than assuming the table prevents duplicate slot rows.

## Session and identity assumptions

`bot_id` remains the gameplay body and equipment owner. `actor_id` remains the durable autonomous identity. `owner_character_id` remains the reserved owner record required by current Bot ownership/save/spawn behavior. None of these implies a connected client session.

A real reserved-owner `Client` plus ordinary Bot trade is valuable as a gameplay baseline in a disposable harness: it shows what the current rules accept and what events are visible. It is not the production architecture. The current project boundary keeps synthetic owner Clients harness-only, and the Actor persistence model explicitly avoids turning `character_data` or `player_event_logs` into Actor identity ([reserved-owner boundary](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/docs/autonomous-actors/reserved-owner-character-seam.md#L11-L75), [persistence reuse limits](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/docs/autonomous-actors/actor-persistence-event-schema-spike.md#L14-L55)).

Loot entitlement needs an explicit answer in the first proof. Passing the reserved owner's character ID into `CanPlayerLoot` is legitimate only if ordinary kill/group logic actually granted that principal the loot right. Manufacturing an allow-list entry after the kill would hide the design question. The `.26` prototype should record whether entitlement came from ordinary group participation, an explicitly test-only fixture, or a proposed shared Actor loot-principal rule.

## Actor receipts and instrumentation seam

Existing loot, merchant, trader, and barter logs are useful behavioral precedents but are Client/account/character-shaped. Bot trade quest events are useful gameplay notifications but are not a durable Actor transaction receipt. After commit, emit one Actor event correlated by `actor_id`, objective/action ID, action generation, and transaction key with:

- operation and final status (`committed`, `rejected`, or `indeterminate`);
- authoritative source and destination identities;
- item-instance fingerprint and quantity;
- before/after equipment or custody slot;
- authoritative quote and before/after wallet values when currency is involved;
- merchant and stock identity when applicable;
- compact reason code and rule version; and
- links to ordinary gameplay event identifiers when those events also exist.

Do not emit a success receipt until the postcondition is confirmed. If a crash leaves an operation indeterminate, retain enough intent/fingerprint state to reconcile it without replaying the mutation. The existing objective contract already keeps `AcquireOneItem` and `SellOneItem` disabled until these authorities are proven ([objective gates](https://github.com/thunderbump/bump-EQEmu/blob/bcf3473671d7f3967a0727e70b883c9254d97cd3/docs/autonomous-actors/first-objective-profile-contract.md#L39-L60)).

## Exploit boundaries

The planner may name a goal such as “acquire that observed drop” or “sell one unneeded item.” The executor must re-resolve every mutable fact. In particular, never trust planner-provided item data, quantity, equipment slot, vendor identity, price, stock, wallet balance, loot right, range, or party ownership.

At claim and again immediately before commit, verify Actor/profile/Bot/reserved-owner bindings, action generation and idempotency key, live zone/entity/corpse/merchant identity, distance and visibility, corpse lock and active-looter contention, loot entitlement, exact source fingerprint, lore and augment conflicts, class/race/level/equip legality, destination capacity, tradability and No Drop rules, positive bounded quantity, merchant list/temp stock, transaction caps, and sufficient funds. Direct calls to `AddBotItem`, `RemoveBotItemBySlot`, `SaveItemBySlot`, `AddMoneyToPP`, `TakeMoneyFromPP`, or raw repository deletion are not Actor action implementations.

Concurrency and crash safety are unresolved in current sequential paths. The prototype must deliberately inject a stale source, occupied destination, duplicate delivery, persistence failure, and interruption between logical phases. A design that cannot prove unchanged or exactly-once state under those cases is evidence for a guarded database transaction or recoverable transfer record, not permission to compensate with uncorrelated add/delete calls.

Use this minimum fault/concurrency matrix for each mutating prototype:

| Case | Injection | Required observation |
| --- | --- | --- |
| Happy path | One valid claim | Source absent, destination contains the exact instance, one committed receipt |
| Duplicate delivery | Repeat the same transaction key before and after restart | Same receipt or stable rejection; no second mutation |
| Stale source | Change/remove the source after planning | No destination write and no success receipt |
| Destination race | Occupy the selected slot between preflight and commit | Source unchanged; stable conflict reason |
| Competing claim | Two transactions claim one corpse entry | Exactly one commits; the loser cannot remove or clone it |
| Persistence failure | Fail each logical write in turn | Pre-state remains authoritative, or a durable indeterminate record reconciles to one valid state |
| Process interruption | Stop after each durable phase, then restart | Reconciliation yields exactly one custody and at most one committed receipt |
| Receipt failure | Commit state but fail event publication | Retry publishes/retrieves the durable receipt without repeating the transaction |

## Smallest follow-up prototypes

### `central-dcq7.26`: conserved corpse-item acquisition

Use one real, non-container, non-stackable equipment instance on one NPC corpse and one eligible, empty Bot equipment slot. Ending at an empty slot avoids the still-unsolved custody of replaced equipment. Do not include coin, upgrades with returns, general carried inventory, vendors, bags, stacks, No Drop exceptions, or player offers in this first proof.

Compare two disposable paths:

1. **Compatibility baseline:** a real reserved-owner Client receives the item through the ordinary corpse path and gives it to its Bot through ordinary Bot trade. Record every Client/session dependency and the final instance fingerprint. This is a baseline, not a production candidate.
2. **Shared transaction seam:** an explicit loot principal invokes extracted/shared corpse validation and a guarded corpse-to-empty-Bot-slot transfer without a synthetic Client. It must preserve the same applicable range, entitlement, contention, lore, quest-veto, item-instance, and equip rules and end in one correlated Actor receipt.

For both paths, snapshot and reload the corpse entry and Bot slot. Prove exactly one destination on success and unchanged state on invalid entitlement, range, lore/equip rejection, occupied slot, stale fingerprint, duplicate action, persistence failure, and injected interruption. The result should decide the production principal model, whether a common durable transaction can cover both stores, and which corpse/Client rules must be extracted rather than copied.

### `central-dcq7.10`: disposition and availability-pricing strategies

Keep this prototype pure and replayable. Feed it conserved item-instance snapshots, all relevant party Bot equipment snapshots, Bot Gear Value results, authoritative merchant eligibility/quotes, observed availability, observed demand, vendor floor, and time-on-market. Have each strategy output only a versioned recommendation with reasons: `equip(bot, slot)`, `hold`, `vendor(merchant)`, or `offer_later`. No strategy may mutate custody or currency.

Start with these safeguards:

- consider `vendor` or `offer_later` only after no relevant party member has a positive legal upgrade recommendation;
- obtain the vendor floor from the ordinary merchant quote, never from Bot Gear Value;
- treat availability/demand adjustments as bounded experimental recommendations, not authoritative merchant packet prices;
- retain the item when merchant eligibility, custody, quote, or wallet authority is unknown;
- compare strategies over the same deterministic event trace and report utility, sell-through time, stock concentration, Actor wealth, upgrade distribution, rejected actions, and conservation failures; and
- do not build an Actor-only market. Defer real player/Actor offers until common session-independent custody, wallet, offer, and settlement authorities can participate in ordinary Bazaar/Barter visibility and rules.

`.10` can therefore answer which policy is promising before merchant mutation exists. Its chosen strategy becomes a caller of the later transaction seam, not part of that seam. `.26` supplies the first conserved item receipt and failure model that `.10` needs for any subsequent live disposition experiment.

## Result

The existing code is rich enough to avoid inventing Actor-specific loot eligibility, equipment valuation, merchant pricing, or market rules. It is not yet shaped to let a persistent Actor transact without a live player session, and its current mutation ordering is not sufficient evidence of conservation. The next work should prove one empty-slot corpse transfer with a shared authority boundary, while disposition/pricing remains a pure, instrumented strategy comparison. General Actor inventory, wallet ownership, replacement-item custody, merchant settlement, and player-facing offers should be decided only after that proof exposes the necessary transaction shape.
