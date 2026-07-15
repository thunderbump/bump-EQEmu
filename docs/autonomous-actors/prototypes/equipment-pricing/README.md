# Equipment Disposition and Pricing Prototype

This is a disposable logic prototype for Wayfinder ticket `central-dcq7.10`. It asks which explainable strategy best
classifies conserved items as party upgrades, immediate vendor goods, retained items, or later market offers while
keeping experimental prices bounded by one captured merchant quote.

The public seam is:

```text
compare(ordered_item_trace, strategy_version="balanced-v1")
  -> ordered_recommendations, aggregate_metrics
```

Inputs are immutable snapshots. Outputs are recommendations plus a deterministic simulated ledger; this prototype does
not mutate inventory, currency, merchant stock, market listings, databases, or EQEmu runtime state. Every item requires
confirmed `actor-party` custody, a unique custody ID, and positive quantity before any equip or disposition recommendation.

Merchant quotes are captured player-compatible baselines, not live Actor authority. Each fixture records its snapshot
version, source revision/ruleset provenance, merchant identity/faction, and the Client character ID, name, faction level,
race, class, deity, and Charisma inputs used by `Client::CalcPriceMod`. Incomplete snapshots are held rather than priced.

## Strategies

- `liquidate-v1`: equip the best positive party upgrade, then vendor every item with complete authority inputs.
- `balanced-v1`: the programmatic default; offer scarce, demanded, young items up to 1.75 times vendor floor.
- `patient-v1`: wait longer and allow prices up to twice vendor floor; otherwise vendor.

Every strategy requires an explicitly legal positive upgrade before recommending equipment. When there is no legal
upgrade, the strategy holds an item if wallet authority, its captured quote, or its market observations are incomplete.
Market demand and availability are fixed experimental observations on a 0–10 scale. The included five-item trace
exposes an upgrade, common vendor good, scarce demanded item, missing-authority item, and aged item. A fixed 30-day
market outcome simulation reports sell-through/aging, ending stock concentration, projected Actor wealth, upgrade
distribution, and rejected actions. A separate custody/currency audit detects duplicate sources/transitions, invalid
quantity, and settlement imbalance instead of deriving conservation from the recommendation count.

Run all strategies:

```sh
python3 equipment_pricing.py --all
```

This emits one JSON array containing the three versioned results.

Run the interactive comparison:

```sh
python3 equipment_pricing.py
```

Run the repository-required behavior checks:

```sh
python3 -m unittest -v test_equipment_pricing.py
```

## Accepted result

Human evaluation on 2026-07-14 accepted the prototype's basic policy:

- a positive legal party upgrade always outranks liquidity or speculative market value;
- weak-demand or aged goods default to ordinary vendor disposition;
- `balanced-v1` is the initial default, using a 14-day offer window and a price ceiling of 1.75 times vendor floor;
- `patient-v1` remains an experimental alternative with a 30-day window and twice-vendor ceiling; and
- `liquidate-v1` remains a comparison baseline rather than the initial live policy.

These are versioned experiment seeds, not permanent economy balance. Real disposition remains disabled until conserved
custody, wallet, merchant settlement, and later offer authorities exist.
