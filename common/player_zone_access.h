#pragma once
#include "types.h"

enum class PlayerZoneAccessFailure { None, MinimumLevel, MaximumLevel, Status, Flag };

// Shared non-routing checks used by Client::CanEnterZone and autonomous actors.
// The caller supplies the server's zone flags and privileged bypass decision.
inline PlayerZoneAccessFailure CheckPlayerZoneAccess(uint8 level, int status, bool gm,
	int minimum_level, int maximum_level, int minimum_status, bool flag_required, bool has_flag)
{
	if (!gm && level < minimum_level) { return PlayerZoneAccessFailure::MinimumLevel; }
	if (!gm && level > maximum_level) { return PlayerZoneAccessFailure::MaximumLevel; }
	if (status < minimum_status) { return PlayerZoneAccessFailure::Status; }
	if (!gm && flag_required && !has_flag) { return PlayerZoneAccessFailure::Flag; }
	return PlayerZoneAccessFailure::None;
}
