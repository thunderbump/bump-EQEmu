import unittest

from equipment_pricing import compare, demo_trace, recommend


class EquipmentPricingPrototypeTest(unittest.TestCase):
    def test_every_strategy_equips_the_best_positive_party_upgrade(self):
        snapshot = {
            "item_id": 101,
            "name": "Polished Short Sword",
            "custody": "actor-party",
            "upgrades": [
                {"bot": "Mellis", "slot": "primary", "gain": 8},
                {"bot": "Pipin", "slot": "primary", "gain": 3},
            ],
            "merchant_quote": {"eligible": True, "sell_copper": 40, "principal": "baseline-client"},
            "availability": 2,
            "demand": 8,
            "age_days": 0,
        }

        for strategy in ("liquidate", "balanced", "patient"):
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

        for strategy in ("liquidate", "balanced", "patient"):
            with self.subTest(strategy=strategy):
                self.assertEqual(
                    recommend(snapshot, strategy),
                    {"action": "hold", "reason": "authority_incomplete"},
                )

    def test_strategies_make_bounded_vendor_or_market_recommendations(self):
        snapshot = {
            "item_id": 303,
            "name": "Rare Earring",
            "custody": "actor-party",
            "upgrades": [],
            "merchant_quote": {
                "eligible": True,
                "merchant": "Rivervale Merchant",
                "sell_copper": 100,
                "principal": "baseline-client",
            },
            "wallet_authority": True,
            "availability": 2,
            "demand": 9,
            "age_days": 3,
        }

        self.assertEqual(recommend(snapshot, "liquidate")["action"], "vendor")
        balanced = recommend(snapshot, "balanced")
        patient = recommend(snapshot, "patient")
        self.assertEqual(balanced["action"], "offer_later")
        self.assertEqual(patient["action"], "offer_later")
        self.assertEqual(balanced["asking_copper"], 164)
        self.assertEqual(patient["asking_copper"], 200)
        self.assertLessEqual(patient["asking_copper"], 2 * snapshot["merchant_quote"]["sell_copper"])

    def test_compare_reports_deterministic_behavior_and_conservation_metrics(self):
        result = compare(demo_trace(), "balanced")

        self.assertEqual(
            result["metrics"],
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


if __name__ == "__main__":
    unittest.main()
