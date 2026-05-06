/*	EQEmu: EQEmulator

	Copyright (C) 2001-2026 EQEmu Development Team

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <cstdint>
#include <string>

namespace FallbackDialogue {

enum class TargetType {
	Unknown,
	Mercenary,
	NPC,
	Bot
};

enum class OutputType {
	None,
	Emote
};

struct TargetedSayRequest {
	uint32_t    speaker_id = 0;
	uint32_t    target_id = 0;
	std::string message;
	TargetType  target_type = TargetType::Unknown;
	bool        authored_dialogue_handled = false;
	bool        target_engaged = false;
};

struct TargetedSayResult {
	bool        handled = false;
	OutputType  output_type = OutputType::None;
	std::string message;
	std::string debug_reason;
};

TargetedSayResult HandleTargetedSay(const TargetedSayRequest &request);

}
