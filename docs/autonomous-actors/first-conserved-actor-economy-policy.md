# First Conserved Actor Economy Policy

Date: 2026-07-14

Wayfinder ticket: **Choose the first conserved actor-economy policy**

## Decision

The first Actor economy exists to make persistent Actor-led Parties visibly acquire, evaluate, equip, retain, buy, and
sell ordinary equipment through the existing world. It does not create a parallel inventory, merchant, currency, or
market authority.

`balanced-v1` is the initial immutable, operator-selected policy. It evaluates the complete current party before
disposing of an item, prefers a legal positive equipment upgrade over liquidity, and uses live ordinary gameplay for
every consequential item or currency transfer. Coarse execution may plan and age intentions, but cannot mutate assets.

## Ownership and initial asset scope

Each Autonomous Actor owns one persistent **Actor Holdings** account containing its wallet and unequipped items. The
Actor leader and each Bot follower keep equipped items in their authoritative gameplay-body equipment slots; Bot
followers own only those equipped items. A reserved owner character, temporary party, Bot wallet, or global treasury
does not own Actor assets. Actor Holdings persist across follower changes and Actor Death Recovery.

The first party begins with:

- an empty Actor Wallet;
- explicitly operator-provisioned leader and follower equipment with a recorded source;
- a configurable default of eight flat holdings slots; and
- no recurring grants, automatic replenishment, bags, nested containers, weight, or encumbrance.

Provisioned equipment begins equipped rather than liquid. Once replaced, it enters Actor Holdings and follows the
ordinary disposition policy. A replacement requires room for displaced gear. Other acquisition requires a free slot;
when holdings are full, the item remains at its authoritative source or the action defers. Nothing is deleted,
overflowed, or automatically sold to make room.

Initial acquisition is limited to one ordinary, non-container, non-stackable equipment instance at a time, plus
separately proven corpse coin. Bags, stacks, consumables, tradeskill materials, quest items, and special No Drop
exception paths remain unavailable until their custody and eligibility rules are proven.

## `balanced-v1` disposition and purchasing

For an item already in conserved Actor custody, `balanced-v1` recommends this order without authorizing a mutation:

1. Require valid custody, quantity one, and a complete evaluation of every relevant current party member. Missing or
   malformed evidence results in `hold`.
2. Recommend equipping the best legal party upgrade whose existing Bot Gear Value gain is greater than zero. A
   positive upgrade always outranks liquidity or speculative value.
3. If no upgrade exists, recommend retaining a scarce, demanded, young item as an **Actor Offer Intent** at no more than `1.75`
   times its captured ordinary vendor floor for days `0` through `13`.
4. Recommend ordinary vendor disposition for weak-demand, common, or aged goods.

An Actor may purchase one ordinary equipment instance when it is the highest-score affordable legal positive upgrade
for any current party member. It uses the authoritative live merchant price, reserves no cash floor, and re-evaluates
the complete party after the purchase. The Actor cannot buy excluded item classes through a resupply label.

`patient-v1` remains an experimental comparison with a 30-day window and `2.0` times vendor-floor ceiling.
`liquidate-v1` remains a comparison baseline. Neither is promoted automatically, and Actors cannot self-modify their
policy. A new default requires an explicit reviewed version in the Actor Strategy Registry.

## Offers, expiration, and prices

Actor Offer Intent is only a recommendation to retain the concrete item in Actor Holdings. It is not a listing,
reservation, escrow, advertisement, or settlement authority. The first policy has no Actor-to-player or Actor-to-Actor
market transaction. Each intent records the strategy version that produced it plus its creation and expiry boundary,
so promoting a new default cannot rewrite an existing intent's window.

At the start of day 14 under `balanced-v1`, or day 30 under `patient-v1`, the intent expires. Expiration clears the old
intent and triggers a fresh complete-party and authority evaluation. If the item remains a non-upgrade and a live
ordinary vendor settlement is executable, it may be sold; otherwise it remains held or deferred. The policy does not
sell off-zone, extend a stale price, or automatically renew an intent.

Availability and observed demand inform the offer recommendation; they do not override an ordinary live merchant
quote. Vendor sales and purchases always use the price authorized by current merchant state and the Actor Merchant
Principal.

## Live merchant authority

Selling and purchasing remain capability-gated until a conserved settlement path proves the complete transfer. The
**Actor Merchant Principal** is the Actor's own race, class, deity, Charisma, and faction identity. Reserved owner
characters and Bot followers cannot provide or modify merchant access or prices, and there is no neutral or guessed
fallback identity. First-slice Actors may use fixed operator-authored identity values, but the ordinary merchant path
must consume those values as the Actor Merchant Principal.

A transaction executes only while the Actor-led Party is materialized in the merchant's live zone and the Actor is
within ordinary interaction range. It reuses current merchant access, stock, pricing, wallet, capacity, and transfer
rules. Actor-versus-player contention receives no special policy: current ordinary merchant state determines whether
the transaction succeeds.

One guarded sale must prove transfer of the concrete item out of Actor Holdings, authoritative Actor Wallet credit,
and a durable receipt. One guarded purchase must prove authoritative wallet debit, live-stock transfer of the concrete
item into available Actor Holdings capacity, and a durable receipt. A synthetic Client, reserved-owner authority,
direct inventory or currency helper calls, guessed success, item creation/deletion, or compensating mutation cannot
stand in for either ordinary settlement.

Coarse city downtime may classify inventory, age an offer intent, prepare a request, or request zone capacity. It
cannot transfer an item or coin. The Actor Resolution Zone Reserve may boot a city to resolve one transaction; when
capacity is unavailable, the action defers without mutation.

Merchant activity occurs during natural city downtime after an objective, when holdings are full, or when a known
affordable equipment upgrade or eligible first-slice resupply need exists. Actors do not continuously detour to or
scan every merchant they pass. Once live and in range, transactions proceed one item at a time.

## Currency and inflation posture

All authorized corpse coin earned from combat the first Actor-led Party actually engaged in goes to the directing
Actor Wallet. Bot followers do not have wallets or coin shares. Coarse or off-zone progress cannot invent loot or
currency.

The first policy imposes no artificial inflation ceiling, vendor-proceeds budget, or mandatory money sink. Actors may
accumulate combat loot and proceeds from proven merchant settlements. If evidence later shows materially harmful
inflation, a later policy may add a sink or tighten a faucet; the first implementation does not assume the remedy.

## Evidence and interrupted outcomes

Full-fidelity **Actor Economy Evidence** is mandatory throughout prototypes and tests and continues across test runs.
Every observed asset mutation records the Actor, item or coin, source, destination, reason, strategy version, and
outcome. Required aggregates include wallet balances, holdings value and concentration, gross and net coin flow,
vendor proceeds, deferred/rejected actions, and conservation violations.

Evidence volume is itself measured. Production retention or sampling is decided later from observed rates rather than
pre-optimized now; conservation failures remain unsampled.

Interrupted merchant recovery stays naive. Ordinary merchant state is authoritative, and after restart the Actor
reloads current wallet and holdings and continues. The first implementation does not add compensation, forensic
recovery, or a guessed repeat transaction. It must prevent duplication, but a rare item loss to a merchant in this
corner case is acceptable and recorded when observable.

## Capability gates and follow-on work

`AcquireOneItem`, `SellOneItem`, and `ResupplyOneItem` become available independently only after their ordinary
gameplay action, authoritative postcondition, custody, and evidence requirements are proven. A strategy recommendation
does not authorize a mutation.

An equip recommendation is likewise inert until a guarded conserved replacement proves the exact item leaving Actor
Holdings for the authorized equipment slot, any displaced item entering reserved Holdings capacity, the confirmed
equipment postcondition, and a durable receipt.

The following remain later work:

- bag and nested-container custody, simulated versus exact capacity, weight, encumbrance, and heavy-item inflation;
- live market visibility, concrete-item escrow, buyer and seller wallets, single-claim settlement, cancellation,
  expiration, delivery, and exploit controls;
- mixed player/Actor or multi-Actor currency splits;
- broader item classes and special No Drop behavior;
- evidence retention or sampling after measured production-like volume; and
- evidence-driven inflation controls and reviewed strategy promotion.

Automated tests use the same custody, authority, evidence, and version boundaries because the living-world economy
needs them. Testing remains a consequence of building understandable persistent Actors, not the primary objective.
