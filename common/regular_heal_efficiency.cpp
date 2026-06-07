/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#include "regular_heal_efficiency.h"

#include "rulesys.h"
#include "spdat.h"

#include <limits>

namespace RegularHealEfficiency {

Settings LoadSettingsFromRules()
{
	return {
		.prefer_efficient_regular_heals = RuleB(Bots, PreferEfficientRegularHeals)
	};
}

HealEstimate EstimateRegularHealAmount(
	uint16_t spell_id,
	const SpellEffectValueCalculator &calculate_spell_effect_value,
	const SpellHealingAdjuster &adjust_spell_healing
)
{
	if (!calculate_spell_effect_value || !adjust_spell_healing) {
		return {};
	}

	if (!IsRegularSingleTargetHealSpell(spell_id)) {
		return {};
	}

	if (IsEffectInSpell(spell_id, SpellEffect::CurrentHPOnce)) {
		return {};
	}

	const auto effect_index = GetSpellEffectIndex(spell_id, SpellEffect::CurrentHP);
	if (effect_index < 0) {
		return {};
	}

	const auto raw_heal = calculate_spell_effect_value(spell_id, effect_index);
	if (raw_heal <= 0) {
		return {};
	}

	const auto adjusted_heal = adjust_spell_healing(spell_id, raw_heal);
	if (adjusted_heal <= 0) {
		return {};
	}

	return {
		.usable = true,
		.amount = adjusted_heal
	};
}

namespace {

bool IsBeforePriority(const Candidate &candidate, const Selection &selection)
{
	return !selection.found || candidate.list_order < selection.list_order;
}

bool IsEfficientCandidate(const Candidate &candidate, int64_t sufficient_heal_amount)
{
	return candidate.has_usable_estimated_heal &&
		candidate.estimated_heal > 0 &&
		candidate.estimated_heal >= sufficient_heal_amount;
}

bool IsBetterEfficientCandidate(
	const Candidate &candidate,
	const Candidate &best_candidate,
	int64_t target_missing_hp,
	bool has_best_candidate
)
{
	if (!has_best_candidate) {
		return true;
	}

	const auto candidate_overheal = candidate.estimated_heal - target_missing_hp;
	const auto best_overheal = best_candidate.estimated_heal - target_missing_hp;
	if (candidate_overheal != best_overheal) {
		return candidate_overheal < best_overheal;
	}

	if (candidate.mana_cost != best_candidate.mana_cost) {
		return candidate.mana_cost < best_candidate.mana_cost;
	}

	return candidate.list_order < best_candidate.list_order;
}

Selection ToSelection(const Candidate &candidate, bool selected_for_efficiency)
{
	return {
		.found = true,
		.spell_id = candidate.spell_id,
		.list_order = candidate.list_order,
		.selected_for_efficiency = selected_for_efficiency
	};
}

}

Selection SelectRegularHealCandidate(
	const Settings &settings,
	int64_t target_missing_hp,
	int64_t sufficient_heal_margin,
	std::span<const Candidate> candidates
)
{
	Selection fallback{};
	for (const auto &candidate : candidates) {
		if (IsBeforePriority(candidate, fallback)) {
			fallback = ToSelection(candidate, false);
		}
	}

	if (!settings.prefer_efficient_regular_heals || !fallback.found) {
		return fallback;
	}

	const auto required_heal = target_missing_hp + sufficient_heal_margin;
	if (required_heal <= 0) {
		return fallback;
	}

	bool has_best_candidate = false;
	Candidate best_candidate{};
	for (const auto &candidate : candidates) {
		if (!IsEfficientCandidate(candidate, required_heal)) {
			continue;
		}

		if (IsBetterEfficientCandidate(candidate, best_candidate, target_missing_hp, has_best_candidate)) {
			best_candidate = candidate;
			has_best_candidate = true;
		}
	}

	if (!has_best_candidate) {
		return fallback;
	}

	return ToSelection(best_candidate, true);
}

Selection SelectRegularHealCandidate(
	const Settings &settings,
	int64_t target_missing_hp,
	int64_t sufficient_heal_margin,
	std::initializer_list<Candidate> candidates
)
{
	return SelectRegularHealCandidate(
		settings,
		target_missing_hp,
		sufficient_heal_margin,
		std::span<const Candidate>(candidates.begin(), candidates.size())
	);
}

}
