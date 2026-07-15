# Equipment Disposition and Pricing Prototype

This is a disposable logic prototype for Wayfinder ticket `central-dcq7.10`. It asks which explainable strategy best
classifies conserved items as party upgrades, immediate vendor goods, retained items, or later market offers while
keeping experimental prices bounded by one captured merchant quote.

The public seam is:

```text
compare(ordered_item_trace, strategy_version)
  -> ordered_recommendations, aggregate_metrics
```

Inputs are immutable snapshots. Outputs are recommendations only; this prototype does not mutate inventory, currency,
merchant stock, market listings, databases, or EQEmu runtime state. Merchant quotes are captured player-compatible
baselines, not live Actor authority.

## Strategies

- `liquidate`: equip the best positive party upgrade, then vendor every item with complete authority inputs.
- `balanced`: equip first; offer scarce, demanded, young items up to 1.75 times vendor floor; otherwise vendor.
- `patient`: equip first; wait longer and allow prices up to twice vendor floor; otherwise vendor.

Every strategy holds an item when custody, wallet authority, or its captured quote is incomplete. Market demand and
availability are fixed experimental observations on a 0–10 scale. The included five-item trace exposes an upgrade,
common vendor good, scarce demanded item, missing-authority item, and aged item.

Run all strategies:

```sh
python3 equipment_pricing.py --all
```

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
- `balanced` is the initial default, using a 14-day offer window and a price ceiling of 1.75 times vendor floor;
- `patient` remains an experimental alternative with a 30-day window and twice-vendor ceiling; and
- `liquidate` remains a comparison baseline rather than the initial live policy.

These are versioned experiment seeds, not permanent economy balance. Real disposition remains disabled until conserved
custody, wallet, merchant settlement, and later offer authorities exist.
