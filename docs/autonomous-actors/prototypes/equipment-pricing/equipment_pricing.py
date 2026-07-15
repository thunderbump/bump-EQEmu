#!/usr/bin/env python3
"""Throwaway Actor equipment-disposition and pricing prototype."""

import argparse
import json
import sys
from types import MappingProxyType


STRATEGY_VERSIONS = ("liquidate-v1", "balanced-v1", "patient-v1")
DEFAULT_STRATEGY_VERSION = "balanced-v1"
OFFER_EXPIRY_DAYS = MappingProxyType({"balanced-v1": 14, "patient-v1": 30})
QUOTE_SOURCE_REVISION = "bcf3473671d7f3967a0727e70b883c9254d97cd3"


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
    if not isinstance(quote, dict):
        return False
    principal = quote.get("principal", {})
    merchant = quote.get("merchant", {})
    provenance = quote.get("provenance", {})
    return (
        quote.get("snapshot_version") == "client-merchant-quote-v1"
        and quote.get("authority") == "player-compatible-baseline"
        and isinstance(principal, dict)
        and isinstance(merchant, dict)
        and isinstance(provenance, dict)
        and all(type(principal.get(field)) is int for field in (
            "character_id", "faction_level", "race", "class", "deity", "charisma"
        ))
        and isinstance(principal.get("name"), str) and bool(principal["name"])
        and all(type(merchant.get(field)) is int for field in ("npc_type_id", "primary_faction"))
        and isinstance(merchant.get("name"), str) and bool(merchant["name"])
        and all(isinstance(provenance.get(field), str) and provenance[field] for field in (
            "pricing_path", "source_revision", "ruleset_version"
        ))
        and type(quote.get("sell_copper")) is int
        and quote["sell_copper"] > 0
        and type(quote.get("eligible")) is bool
    )


def _market_observation_valid(item):
    return (
        type(item.get("availability")) is int
        and 0 <= item["availability"] <= 10
        and type(item.get("demand")) is int
        and 0 <= item["demand"] <= 10
        and type(item.get("age_days")) is int
        and item["age_days"] >= 0
    )


def _market_outcome_valid(outcome):
    return (
        isinstance(outcome, dict)
        and all(
            type(outcome.get(field)) is int and outcome[field] >= 0
            for field in (
                "sold_after_days", "buyer_max_copper",
                "settled_copper", "buyer_debited_copper",
            )
        )
    )


def _completed_party_evaluation():
    return {
        "snapshot_version": "bot-gear-value-party-v1",
        "complete": True,
        "relevant_bots": ["Mellis", "Pipin"],
        "evaluated_bots": ["Mellis", "Pipin"],
    }


def _upgrade_evaluation_complete(evaluation):
    if not isinstance(evaluation, dict) or evaluation.get("complete") is not True:
        return False
    relevant = evaluation.get("relevant_bots")
    evaluated = evaluation.get("evaluated_bots")
    return (
        evaluation.get("snapshot_version") == "bot-gear-value-party-v1"
        and isinstance(relevant, list)
        and isinstance(evaluated, list)
        and bool(relevant)
        and all(isinstance(bot, str) and bot for bot in relevant + evaluated)
        and len(relevant) == len(set(relevant))
        and len(evaluated) == len(set(evaluated))
        and set(relevant) == set(evaluated)
    )


def demo_trace():
    """Return fixed snapshots that expose the policy differences."""
    return [
        {
            "custody_id": "loot-101", "item_id": 101, "name": "Polished Short Sword", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True,
            "upgrade_evaluation": _completed_party_evaluation(),
            "upgrades": [{"bot": "Mellis", "slot": "primary", "gain": 8, "legal": True}],
            "merchant_quote": captured_quote(40),
            "availability": 2, "demand": 8, "age_days": 0,
        },
        {
            "custody_id": "loot-102", "item_id": 102, "name": "Common Pelt", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "upgrade_evaluation": _completed_party_evaluation(),
            "merchant_quote": captured_quote(12),
            "availability": 9, "demand": 2, "age_days": 1,
        },
        {
            "custody_id": "loot-103", "item_id": 103, "name": "Rare Earring", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "upgrade_evaluation": _completed_party_evaluation(),
            "merchant_quote": captured_quote(100),
            "availability": 2, "demand": 9, "age_days": 3,
            "market_outcome": {
                "sold_after_days": 7, "buyer_max_copper": 180,
                "settled_copper": 164, "buyer_debited_copper": 164,
            },
        },
        {
            "custody_id": "loot-104", "item_id": 104, "name": "Unpriced Relic", "quantity": 1,
            "custody": "actor-party", "wallet_authority": False, "upgrades": [],
            "upgrade_evaluation": _completed_party_evaluation(),
            "merchant_quote": None,
            "availability": 1, "demand": 10, "age_days": 30,
        },
        {
            "custody_id": "loot-105", "item_id": 105, "name": "Aged Cloak", "quantity": 1,
            "custody": "actor-party", "wallet_authority": True, "upgrades": [],
            "upgrade_evaluation": _completed_party_evaluation(),
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
    quantity = item.get("quantity", 1)
    if not item.get("custody_id") or type(quantity) is not int or quantity <= 0:
        return {"action": "hold", "reason": "custody_invalid"}

    evaluation = item.get("upgrade_evaluation")
    if not _upgrade_evaluation_complete(evaluation):
        return {"action": "hold", "reason": "upgrade_evaluation_incomplete"}

    upgrade_results = item.get("upgrades")
    if not isinstance(upgrade_results, list) or any(
        not isinstance(upgrade, dict)
        or not isinstance(upgrade.get("bot"), str)
        or not upgrade["bot"]
        or upgrade["bot"] not in evaluation["evaluated_bots"]
        or not isinstance(upgrade.get("slot"), str)
        or not upgrade["slot"]
        or type(upgrade.get("gain")) is not int
        or type(upgrade.get("legal")) is not bool
        for upgrade in upgrade_results
    ):
        return {"action": "hold", "reason": "upgrade_snapshot_invalid"}

    upgrades = [upgrade for upgrade in upgrade_results if upgrade["legal"] and upgrade["gain"] > 0]
    if upgrades:
        if quantity != 1:
            return {"action": "hold", "reason": "equip_quantity_unsupported"}
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

    if not _market_observation_valid(item):
        return {"action": "hold", "reason": "market_observation_invalid"}

    vendor = {
        "action": "vendor",
        "merchant": quote["merchant"]["name"],
        "proceeds_copper": quote["sell_copper"],
        "reason": "prefer_immediate_liquidity",
    }
    if strategy == "liquidate":
        return vendor

    demand_gap = item["demand"] - item["availability"]
    max_age = OFFER_EXPIRY_DAYS[strategy_version]
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
        "offer_expires_after_days": max_age,
        "expiry_merchant": quote["merchant"]["name"],
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
        if type(quantity) is not int or quantity <= 0:
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

    expected_currency = sum(entry["unit_copper"] * entry["quantity"] for entry in currency_ledger)
    simulated_currency = sum(entry["actor_delta_copper"] for entry in currency_ledger)
    market_net = sum(
        entry["actor_delta_copper"] + entry["counterparty_delta_copper"]
        for entry in currency_ledger if entry["kind"] == "market_transfer"
    )
    if any(
        entry["actor_delta_copper"] != entry["unit_copper"] * entry["quantity"]
        for entry in currency_ledger
    ):
        errors.append("currency_imbalance")
    if market_net != 0 or any(
        entry["counterparty_delta_copper"] != -entry["unit_copper"] * entry["quantity"]
        for entry in currency_ledger if entry["kind"] == "market_transfer"
    ):
        errors.append("market_currency_imbalance")

    return {
        "initial_item_quantity": sum(
            item.get("quantity", 1) for item in trace
            if type(item.get("quantity", 1)) is int and item.get("quantity", 1) > 0
        ),
        "final_item_quantity": sum(
            entry["quantity"] for entry in custody_ledger
            if type(entry["quantity"]) is int and entry["quantity"] > 0
        ),
        "expected_currency_copper": expected_currency,
        "simulated_currency_copper": simulated_currency,
        "wallet_before_copper": 0,
        "expected_wallet_after_copper": expected_currency,
        "wallet_after_copper": simulated_currency,
        "market_net_copper": market_net,
        "errors": errors,
    }


def _simulate(trace, recommendations):
    custody_ledger = []
    currency_ledger = []
    sell_days = []
    remaining = {}
    aged_unsold_units = 0
    upgrade_distribution = {}
    simulation_errors = []

    for item, result in zip(trace, recommendations):
        quantity = item.get("quantity", 1)
        action = result["action"]
        destination = item.get("custody")
        if action == "equip":
            destination = f"bot-equipment:{result['bot']}:{result['slot']}"
            bot = upgrade_distribution.setdefault(
                result["bot"], {"equipped_units": 0, "utility_gain": 0}
            )
            bot["equipped_units"] += quantity
            bot["utility_gain"] += result["utility_gain"]
        elif action == "vendor":
            destination = f"merchant:{result['merchant']}"
            proceeds = result["proceeds_copper"] * quantity
            currency_ledger.append({
                "kind": "merchant_source_sink",
                "source": f"merchant:{result['merchant']}",
                "unit_copper": result["proceeds_copper"],
                "quantity": quantity,
                "actor_delta_copper": proceeds,
                "counterparty_delta_copper": None,
            })
        elif action == "offer_later":
            outcome = item.get("market_outcome")
            if not _market_outcome_valid(outcome):
                simulation_errors.append(f"invalid_market_outcome:{item.get('custody_id')}")
                outcome = {}
            sold_after_days = outcome.get("sold_after_days")
            sold = (
                type(sold_after_days) is int
                and 0 <= sold_after_days < result["offer_expires_after_days"]
                and outcome.get("buyer_max_copper", 0) >= result["asking_copper"]
            )
            if sold:
                destination = "market-buyer"
                credited = outcome.get("settled_copper", result["asking_copper"]) * quantity
                debited = outcome.get("buyer_debited_copper", 0) * quantity
                currency_ledger.append({
                    "kind": "market_transfer",
                    "source": "market-buyer",
                    "unit_copper": result["asking_copper"],
                    "quantity": quantity,
                    "actor_delta_copper": credited,
                    "counterparty_delta_copper": -debited,
                })
                sell_days.extend([sold_after_days] * quantity)
            else:
                destination = f"merchant:{result['expiry_merchant']}"
                aged_unsold_units += quantity
                proceeds = result["vendor_floor_copper"] * quantity
                currency_ledger.append({
                    "kind": "merchant_source_sink",
                    "source": f"merchant:{result['expiry_merchant']}",
                    "unit_copper": result["vendor_floor_copper"],
                    "quantity": quantity,
                    "actor_delta_copper": proceeds,
                    "counterparty_delta_copper": None,
                })

        custody_ledger.append({
            "custody_id": item.get("custody_id"),
            "from": item.get("custody"),
            "to": destination,
            "quantity": quantity,
        })
        if (destination in ("actor-party", "market-offer")
                and type(quantity) is int and quantity > 0):
            remaining[item["item_id"]] = remaining.get(item["item_id"], 0) + quantity

    accounting = _audit(trace, custody_ledger, currency_ledger)
    accounting["errors"].extend(simulation_errors)
    remaining_total = sum(remaining.values())
    return {
        "accounting": accounting,
        "custody_ledger": custody_ledger,
        "currency_ledger": currency_ledger,
        "metrics": {
            "sell_through_units": len(sell_days),
            "average_sell_through_days": sum(sell_days) // len(sell_days) if sell_days else None,
            "aged_unsold_units": aged_unsold_units,
            "ending_unit_stock_concentration_bps": (
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
        "projected_vendor_copper": sum(
            result.get("proceeds_copper", 0) * result["quantity"] for result in recommendations
            if type(result["quantity"]) is int and result["quantity"] > 0
        ),
        "projected_offer_copper": sum(
            result.get("asking_copper", 0) * result["quantity"] for result in recommendations
            if type(result["quantity"]) is int and result["quantity"] > 0
        ),
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

    if args.all or (not sys.stdin.isatty() and args.strategy is None):
        print(json.dumps([compare(demo_trace(), strategy) for strategy in STRATEGY_VERSIONS], indent=2))
        return

    if not sys.stdin.isatty():
        _print(args.strategy)
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
