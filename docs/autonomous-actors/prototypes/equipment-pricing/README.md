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

## Evaluation pending

The prototype is waiting for human judgment on three questions:

1. Should party upgrades always outrank liquidity and market value?
2. Is immediate vendor disposition a good default when demand is weak or an item has aged out?
3. Are the `balanced` and `patient` price ceilings understandable starting points, or should pricing use a different
   shape before the conserved economy policy is chosen?
