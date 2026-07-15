#!/usr/bin/env python3
"""Throwaway Actor equipment-disposition and pricing prototype."""

import argparse
import json
import sys


STRATEGY_VERSIONS = ("liquidate-v1", "balanced-v1", "patient-v1")
DEFAULT_STRATEGY_VERSION = "balanced-v1"
QUOTE_SOURCE_REVISION = "bcf3473671d7f3967a0727e70b883c9254d97cd3"
SIMULATION_DAYS = 30


def captured_quote(sell_copper):
    """Return one replayable player-compatible merchant quote fixture."""
    return {
        "snapshot_version": "client-merchant-quote-v1",
        "authority": "player-compatible-baseline",
        "eligible": True,
        "sell_copper": sell_copper,
        "principal": {
            "character_id": 7001,
            "name": "baseline-client",
            "faction_level": 5,
            "race": 11,
            "class": 1,
            "deity": 208,
            "charisma": 85,
        },
        "merchant": {
            "npc_type_id": 12001,
            "name": "Rivervale Merchant",
            "primary_faction": 77,
        },
        "provenance": {
            "pricing_path": "Client::CalcPriceMod",
            "source_revision": QUOTE_SOURCE_REVISION,
            "ruleset_version": "prototype-fixture-v1",
        },
    }


def _quote_complete(quote):
    principal = quote.get("principal", {})
    merchant = quote.get("merchant", {})
    provenance = quote.get("provenance", {})
    return (
        quote.get("snapshot_version") == "client-merchant-quote-v1"
        and quote.get("authority") == "player-compatible-baseline"
        and all(field in principal for field in (
            "character_id", "name", "faction_level", "race", "class", "deity", "charisma"
        ))
        and all(field in merchant for field in ("npc_type_id", "name", "primary_faction"))
        and all(field in provenance for field in ("pricing_path", "source_revision", "ruleset_version"))
        and "sell_copper" in quote
        and "eligible" in quote
    )


def demo_trace():
    """Return fixed snapshots that expose the policy differences."""
    return [
        {
            "custody_id": "loot-101", "item_id": 101, "name": "Polished Short Sword", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True,
            "upgrades": [{"bot": "Mellis", "slot": "primary", "gain": 8}],
            "merchant_quote": captured_quote(40),
            "availability": 2, "demand": 8, "age_days": 0,
        },
        {
            "custody_id": "loot-102", "item_id": 102, "name": "Common Pelt", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "merchant_quote": captured_quote(12),
            "availability": 9, "demand": 2, "age_days": 1,
        },
        {
            "custody_id": "loot-103", "item_id": 103, "name": "Rare Earring", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "merchant_quote": captured_quote(100),
            "availability": 2, "demand": 9, "age_days": 3,
            "market_outcome": {"sold_after_days": 7, "buyer_max_copper": 180, "settled_copper": 164},
        },
        {
            "custody_id": "loot-104", "item_id": 104, "name": "Unpriced Relic", "quantity": 1,
            "custody": "actor-party", "wallet_authority": False, "upgrades": [],
            "merchant_quote": None,
            "availability": 1, "demand": 10, "age_days": 30,
        },
        {
            "custody_id": "loot-105", "item_id": 105, "name": "Aged Cloak", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "merchant_quote": captured_quote(80),
            "availability": 3, "demand": 6, "age_days": 45,
        },
    ]


def recommend(item, strategy_version=DEFAULT_STRATEGY_VERSION):
    """Return one pure disposition recommendation for one conserved item snapshot."""
    if strategy_version not in STRATEGY_VERSIONS:
        raise ValueError(f"unknown strategy version: {strategy_version}")
    strategy = strategy_version.removesuffix("-v1")

    if item.get("custody") != "actor-party":
        return {"action": "hold", "reason": "custody_unconfirmed"}
    if not item.get("custody_id") or item.get("quantity", 1) <= 0:
        return {"action": "hold", "reason": "custody_invalid"}

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
    ):
        return {"action": "hold", "reason": "authority_incomplete"}

    if not _quote_complete(quote):
        return {"action": "hold", "reason": "quote_snapshot_incomplete"}

    if not quote.get("eligible"):
        return {"action": "hold", "reason": "merchant_ineligible"}

    vendor = {
        "action": "vendor",
        "merchant": quote["merchant"]["name"],
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


def _audit(trace, custody_ledger, currency_ledger):
    errors = []
    sources = {}
    for item in trace:
        custody_id = item.get("custody_id")
        quantity = item.get("quantity", 1)
        if not custody_id or custody_id in sources:
            errors.append(f"duplicate_or_missing_source:{custody_id}")
        if quantity <= 0:
            errors.append(f"invalid_quantity:{custody_id}")
        sources[custody_id] = quantity

    transitions = {}
    for entry in custody_ledger:
        custody_id = entry["custody_id"]
        if entry["from"] != "actor-party":
            errors.append(f"unconfirmed_source:{custody_id}")
        if custody_id in transitions:
            errors.append(f"duplicate_transition:{custody_id}")
        transitions[custody_id] = entry["quantity"]

    for custody_id, quantity in sources.items():
        if transitions.get(custody_id) != quantity:
            errors.append(f"custody_imbalance:{custody_id}")

    expected_currency = sum(entry["expected_copper"] for entry in currency_ledger)
    simulated_currency = sum(entry["credited_copper"] for entry in currency_ledger)
    if expected_currency != simulated_currency:
        errors.append("currency_imbalance")

    return {
        "initial_item_quantity": sum(max(0, item.get("quantity", 1)) for item in trace),
        "final_item_quantity": sum(max(0, entry["quantity"]) for entry in custody_ledger),
        "expected_currency_copper": expected_currency,
        "simulated_currency_copper": simulated_currency,
        "errors": errors,
    }


def _simulate(trace, recommendations):
    custody_ledger = []
    currency_ledger = []
    sell_days = []
    remaining = {}
    aged_unsold = 0
    upgrade_distribution = {}

    for item, result in zip(trace, recommendations):
        quantity = item.get("quantity", 1)
        action = result["action"]
        destination = item.get("custody")
        if action == "equip":
            destination = f"bot-equipment:{result['bot']}:{result['slot']}"
            bot = upgrade_distribution.setdefault(result["bot"], {"items": 0, "utility_gain": 0})
            bot["items"] += quantity
            bot["utility_gain"] += result["utility_gain"]
        elif action == "vendor":
            destination = f"merchant:{result['merchant']}"
            proceeds = result["proceeds_copper"] * quantity
            currency_ledger.append({"expected_copper": proceeds, "credited_copper": proceeds})
        elif action == "offer_later":
            outcome = item.get("market_outcome", {})
            sold = (
                outcome.get("sold_after_days", SIMULATION_DAYS + 1) <= SIMULATION_DAYS
                and outcome.get("buyer_max_copper", 0) >= result["asking_copper"]
            )
            if sold:
                destination = "market-buyer"
                expected = result["asking_copper"] * quantity
                credited = outcome.get("settled_copper", result["asking_copper"]) * quantity
                currency_ledger.append({"expected_copper": expected, "credited_copper": credited})
                sell_days.append(outcome["sold_after_days"])
            else:
                destination = "market-offer"
                aged_unsold += quantity

        custody_ledger.append({
            "custody_id": item.get("custody_id"),
            "from": item.get("custody"),
            "to": destination,
            "quantity": quantity,
        })
        if destination in ("actor-party", "market-offer"):
            remaining[item["item_id"]] = remaining.get(item["item_id"], 0) + quantity

    accounting = _audit(trace, custody_ledger, currency_ledger)
    remaining_total = sum(remaining.values())
    return {
        "accounting": accounting,
        "custody_ledger": custody_ledger,
        "currency_ledger": currency_ledger,
        "metrics": {
            "sell_through_count": len(sell_days),
            "average_sell_through_days": sum(sell_days) // len(sell_days) if sell_days else None,
            "aged_unsold_count": aged_unsold,
            "ending_stock_concentration_bps": (
                max(remaining.values()) * 10000 // remaining_total if remaining_total else 0
            ),
            "projected_actor_wealth_copper": accounting["simulated_currency_copper"],
            "upgrade_distribution": upgrade_distribution,
            "rejected_actions": sum(result["action"] == "hold" for result in recommendations),
            "conservation_failures": len(accounting["errors"]),
        },
    }


def compare(trace, strategy_version=DEFAULT_STRATEGY_VERSION):
    """Compare one strategy over an ordered immutable trace."""
    if strategy_version not in STRATEGY_VERSIONS:
        raise ValueError(f"unknown strategy version: {strategy_version}")
    recommendations = []
    for item in trace:
        recommendations.append(
            {"item_id": item["item_id"], "name": item["name"], "quantity": item.get("quantity", 1)}
            | recommend(item, strategy_version)
        )

    simulation = _simulate(trace, recommendations)
    metrics = {
        "items": len(trace),
        "equip": sum(result["action"] == "equip" for result in recommendations),
        "hold": sum(result["action"] == "hold" for result in recommendations),
        "vendor": sum(result["action"] == "vendor" for result in recommendations),
        "offer_later": sum(result["action"] == "offer_later" for result in recommendations),
        "utility_gain": sum(result.get("utility_gain", 0) for result in recommendations),
        "projected_vendor_copper": sum(result.get("proceeds_copper", 0) for result in recommendations),
        "projected_offer_copper": sum(result.get("asking_copper", 0) for result in recommendations),
    } | simulation["metrics"]
    return {
        "strategy_version": strategy_version,
        "recommendations": recommendations,
        "metrics": metrics,
        "accounting": simulation["accounting"],
        "custody_ledger": simulation["custody_ledger"],
        "currency_ledger": simulation["currency_ledger"],
    }


def _print(strategy_version):
    print(json.dumps(compare(demo_trace(), strategy_version), indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("strategy", nargs="?", choices=STRATEGY_VERSIONS)
    parser.add_argument("--all", action="store_true")
    args = parser.parse_args()

    if args.all or not sys.stdin.isatty():
        for strategy in STRATEGY_VERSIONS if args.all or args.strategy is None else (args.strategy,):
            _print(strategy)
        return

    strategy = args.strategy or DEFAULT_STRATEGY_VERSION
    keys = {"l": "liquidate-v1", "b": "balanced-v1", "p": "patient-v1"}
    while True:
        print("\033[2J\033[H", end="")
        _print(strategy)
        choice = input("\n[l] liquidate  [b] balanced  [p] patient  [q] quit > ").strip().lower()
        if choice == "q":
            return
        strategy = keys.get(choice, strategy)


if __name__ == "__main__":
    main()
