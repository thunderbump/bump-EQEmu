import unittest

import equipment_pricing as pricing
from equipment_pricing import captured_quote, compare, demo_trace, recommend


class EquipmentPricingPrototypeTest(unittest.TestCase):
    def test_balanced_v1_is_the_replayable_programmatic_default(self):
        implicit = pricing.compare(pricing.demo_trace())
        explicit = pricing.compare(pricing.demo_trace(), "balanced-v1")

        self.assertEqual(implicit, explicit)
        self.assertEqual(implicit["strategy_version"], "balanced-v1")
        with self.assertRaises(ValueError):
            pricing.compare(pricing.demo_trace(), "balanced")

    def test_every_strategy_equips_the_best_positive_party_upgrade(self):
        snapshot = {
            "item_id": 101,
            "name": "Polished Short Sword",
            "custody": "actor-party",
            "custody_id": "loot-101",
            "upgrades": [
                {"bot": "Mellis", "slot": "primary", "gain": 8},
                {"bot": "Pipin", "slot": "primary", "gain": 3},
            ],
            "merchant_quote": {"eligible": True, "sell_copper": 40, "principal": "baseline-client"},
            "availability": 2,
            "demand": 8,
            "age_days": 0,
        }

        for strategy in ("liquidate-v1", "balanced-v1", "patient-v1"):
            with self.subTest(strategy=strategy):
                result = recommend(snapshot, strategy)
                self.assertEqual(result["action"], "equip")
                self.assertEqual(result["bot"], "Mellis")
                self.assertEqual(result["slot"], "primary")
                self.assertEqual(result["utility_gain"], 8)

    def test_unconfirmed_authority_holds_the_item(self):
        snapshot = {
            "item_id": 202,
            "name": "Unpriced Relic",
            "custody": "unknown",
            "upgrades": [],
            "merchant_quote": None,
            "wallet_authority": False,
            "availability": 1,
            "demand": 10,
            "age_days": 30,
        }

        for strategy in ("liquidate-v1", "balanced-v1", "patient-v1"):
            with self.subTest(strategy=strategy):
                self.assertEqual(
                    recommend(snapshot, strategy),
                    {"action": "hold", "reason": "custody_unconfirmed"},
                )

    def test_unknown_custody_blocks_a_positive_upgrade(self):
        snapshot = {
            "item_id": 203,
            "name": "Unclaimed Sword",
            "custody": "unknown",
            "upgrades": [{"bot": "Mellis", "slot": "primary", "gain": 12}],
        }

        self.assertEqual(
            recommend(snapshot, "balanced-v1"),
            {"action": "hold", "reason": "custody_unconfirmed"},
        )

    def test_invalid_conserved_source_blocks_a_positive_upgrade(self):
        snapshot = {
            "item_id": 204,
            "name": "Zero-Quantity Sword",
            "custody": "actor-party",
            "custody_id": "loot-204",
            "quantity": 0,
            "upgrades": [{"bot": "Mellis", "slot": "primary", "gain": 12}],
        }

        self.assertEqual(
            recommend(snapshot, "balanced-v1"),
            {"action": "hold", "reason": "custody_invalid"},
        )

    def test_strategies_make_bounded_vendor_or_market_recommendations(self):
        snapshot = {
            "item_id": 303,
            "name": "Rare Earring",
            "custody": "actor-party",
            "custody_id": "loot-303",
            "upgrades": [],
            "merchant_quote": captured_quote(100),
            "wallet_authority": True,
            "availability": 2,
            "demand": 9,
            "age_days": 3,
        }

        self.assertEqual(recommend(snapshot, "liquidate-v1")["action"], "vendor")
        balanced = recommend(snapshot, "balanced-v1")
        patient = recommend(snapshot, "patient-v1")
        self.assertEqual(balanced["action"], "offer_later")
        self.assertEqual(patient["action"], "offer_later")
        self.assertEqual(balanced["asking_copper"], 164)
        self.assertEqual(patient["asking_copper"], 200)
        self.assertLessEqual(patient["asking_copper"], 2 * snapshot["merchant_quote"]["sell_copper"])

    def test_quote_snapshot_records_replay_inputs_and_rejects_missing_charisma(self):
        snapshot = demo_trace()[1]
        quote = snapshot["merchant_quote"]

        self.assertEqual(quote["snapshot_version"], "client-merchant-quote-v1")
        self.assertEqual(quote["authority"], "player-compatible-baseline")
        self.assertEqual(
            quote["principal"],
            {
                "character_id": 7001,
                "name": "baseline-client",
                "faction_level": 5,
                "race": 11,
                "class": 1,
                "deity": 208,
                "charisma": 85,
            },
        )
        self.assertEqual(
            quote["provenance"],
            {
                "pricing_path": "Client::CalcPriceMod",
                "source_revision": "bcf3473671d7f3967a0727e70b883c9254d97cd3",
                "ruleset_version": "prototype-fixture-v1",
            },
        )

        incomplete = dict(snapshot)
        incomplete["merchant_quote"] = dict(quote)
        incomplete["merchant_quote"]["principal"] = dict(quote["principal"])
        del incomplete["merchant_quote"]["principal"]["charisma"]
        self.assertEqual(
            recommend(incomplete, "balanced-v1"),
            {"action": "hold", "reason": "quote_snapshot_incomplete"},
        )

    def test_compare_reports_deterministic_behavior_and_conservation_metrics(self):
        result = compare(demo_trace(), "balanced-v1")

        self.assertEqual(
            {key: result["metrics"][key] for key in (
                "items", "equip", "hold", "vendor", "offer_later", "utility_gain",
                "projected_vendor_copper", "projected_offer_copper", "conservation_failures",
            )},
            {
                "items": 5,
                "equip": 1,
                "hold": 1,
                "vendor": 2,
                "offer_later": 1,
                "utility_gain": 8,
                "projected_vendor_copper": 92,
                "projected_offer_copper": 164,
                "conservation_failures": 0,
            },
        )
        self.assertEqual(
            [recommendation["item_id"] for recommendation in result["recommendations"]],
            [101, 102, 103, 104, 105],
        )

    def test_compare_simulates_time_custody_currency_and_strategy_outcomes(self):
        result = compare(demo_trace(), "balanced-v1")

        self.assertEqual(result["metrics"]["sell_through_count"], 1)
        self.assertEqual(result["metrics"]["average_sell_through_days"], 7)
        self.assertEqual(result["metrics"]["aged_unsold_count"], 0)
        self.assertEqual(result["metrics"]["ending_stock_concentration_bps"], 10000)
        self.assertEqual(result["metrics"]["projected_actor_wealth_copper"], 256)
        self.assertEqual(
            result["metrics"]["upgrade_distribution"],
            {"Mellis": {"items": 1, "utility_gain": 8}},
        )
        self.assertEqual(result["metrics"]["rejected_actions"], 1)
        self.assertEqual(
            result["accounting"],
            {
                "initial_item_quantity": 5,
                "final_item_quantity": 5,
                "expected_currency_copper": 256,
                "simulated_currency_copper": 256,
                "errors": [],
            },
        )

    def test_accounting_independently_detects_duplicate_custody_and_currency_imbalance(self):
        trace = demo_trace()
        trace[1]["custody_id"] = trace[0]["custody_id"]
        trace[2]["market_outcome"]["settled_copper"] = 150

        result = compare(trace, "balanced-v1")

        self.assertEqual(result["accounting"]["initial_item_quantity"], 5)
        self.assertEqual(result["accounting"]["final_item_quantity"], 5)
        self.assertEqual(
            result["accounting"]["errors"],
            [
                "duplicate_or_missing_source:loot-101",
                "duplicate_transition:loot-101",
                "currency_imbalance",
            ],
        )
        self.assertEqual(result["metrics"]["conservation_failures"], 3)

    def test_accounting_does_not_invent_actor_custody_for_a_held_item(self):
        trace = demo_trace()
        trace[0]["custody"] = "unknown"

        result = compare(trace, "balanced-v1")

        self.assertEqual(result["recommendations"][0]["action"], "hold")
        self.assertIn("unconfirmed_source:loot-101", result["accounting"]["errors"])


if __name__ == "__main__":
    unittest.main()
