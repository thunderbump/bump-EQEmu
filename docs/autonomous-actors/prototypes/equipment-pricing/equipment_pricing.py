#!/usr/bin/env python3
"""Throwaway Actor equipment-disposition and pricing prototype."""

import argparse
import json
import sys


STRATEGIES = ("liquidate", "balanced", "patient")


def demo_trace():
    """Return fixed snapshots that expose the policy differences."""
    quote = {"eligible": True, "merchant": "Rivervale Merchant", "principal": "baseline-client"}
    return [
        {
            "item_id": 101, "name": "Polished Short Sword", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True,
            "upgrades": [{"bot": "Mellis", "slot": "primary", "gain": 8}],
            "merchant_quote": quote | {"sell_copper": 40},
            "availability": 2, "demand": 8, "age_days": 0,
        },
        {
            "item_id": 102, "name": "Common Pelt", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "merchant_quote": quote | {"sell_copper": 12},
            "availability": 9, "demand": 2, "age_days": 1,
        },
        {
            "item_id": 103, "name": "Rare Earring", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "merchant_quote": quote | {"sell_copper": 100},
            "availability": 2, "demand": 9, "age_days": 3,
        },
        {
            "item_id": 104, "name": "Unpriced Relic", "quantity": 1,
            "custody": "actor-party", "wallet_authority": False, "upgrades": [],
            "merchant_quote": None,
            "availability": 1, "demand": 10, "age_days": 30,
        },
        {
            "item_id": 105, "name": "Aged Cloak", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "merchant_quote": quote | {"sell_copper": 80},
            "availability": 3, "demand": 6, "age_days": 45,
        },
    ]


def recommend(item, strategy):
    """Return one pure disposition recommendation for one conserved item snapshot."""
    if strategy not in STRATEGIES:
        raise ValueError(f"unknown strategy: {strategy}")

    upgrades = [upgrade for upgrade in item["upgrades"] if upgrade["gain"] > 0]
    if upgrades:
        best = max(upgrades, key=lambda upgrade: upgrade["gain"])
        return {
            "action": "equip",
            "bot": best["bot"],
            "slot": best["slot"],
            "utility_gain": best["gain"],
            "reason": "best_positive_party_upgrade",
        }

    quote = item.get("merchant_quote")
    if (
        item.get("custody") != "actor-party"
        or not item.get("wallet_authority")
        or not quote
        or not quote.get("principal")
    ):
        return {"action": "hold", "reason": "authority_incomplete"}

    if not quote.get("eligible"):
        return {"action": "hold", "reason": "merchant_ineligible"}

    vendor = {
        "action": "vendor",
        "merchant": quote["merchant"],
        "proceeds_copper": quote["sell_copper"],
        "reason": "prefer_immediate_liquidity",
    }
    if strategy == "liquidate":
        return vendor

    demand_gap = item["demand"] - item["availability"]
    max_age = 14 if strategy == "balanced" else 30
    if demand_gap <= 0 or item["age_days"] >= max_age:
        return vendor | {"reason": "weak_or_aged_market_signal"}

    if strategy == "balanced":
        premium = max(10, min(75, demand_gap * 10 - item["age_days"] * 2))
    else:
        premium = max(20, min(100, demand_gap * 15 - item["age_days"]))

    return {
        "action": "offer_later",
        "asking_copper": quote["sell_copper"] * (100 + premium) // 100,
        "vendor_floor_copper": quote["sell_copper"],
        "reason": "bounded_demand_availability_premium",
    }


def compare(trace, strategy):
    """Compare one strategy over an ordered immutable trace."""
    recommendations = []
    for item in trace:
        recommendations.append(
            {"item_id": item["item_id"], "name": item["name"], "quantity": item.get("quantity", 1)}
            | recommend(item, strategy)
        )

    metrics = {
        "items": len(trace),
        "equip": sum(result["action"] == "equip" for result in recommendations),
        "hold": sum(result["action"] == "hold" for result in recommendations),
        "vendor": sum(result["action"] == "vendor" for result in recommendations),
        "offer_later": sum(result["action"] == "offer_later" for result in recommendations),
        "utility_gain": sum(result.get("utility_gain", 0) for result in recommendations),
        "projected_vendor_copper": sum(result.get("proceeds_copper", 0) for result in recommendations),
        "projected_offer_copper": sum(result.get("asking_copper", 0) for result in recommendations),
        "conservation_failures": sum(
            result["item_id"] != item["item_id"]
            or result["quantity"] != item.get("quantity", 1)
            for item, result in zip(trace, recommendations)
        ) + abs(len(trace) - len(recommendations)),
    }
    return {"strategy_version": f"{strategy}-v1", "recommendations": recommendations, "metrics": metrics}


def _print(strategy):
    print(json.dumps(compare(demo_trace(), strategy), indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("strategy", nargs="?", choices=STRATEGIES)
    parser.add_argument("--all", action="store_true")
    args = parser.parse_args()

    if args.all or not sys.stdin.isatty():
        for strategy in STRATEGIES if args.all or args.strategy is None else (args.strategy,):
            _print(strategy)
        return

    strategy = args.strategy or "balanced"
    keys = {"l": "liquidate", "b": "balanced", "p": "patient"}
    while True:
        print("\033[2J\033[H", end="")
        _print(strategy)
        choice = input("\n[l] liquidate  [b] balanced  [p] patient  [q] quit > ").strip().lower()
        if choice == "q":
            return
        strategy = keys.get(choice, strategy)


if __name__ == "__main__":
    main()
