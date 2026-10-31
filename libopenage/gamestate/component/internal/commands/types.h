// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once


namespace openage::gamestate::component::command {

/**
 * Command types.
 */
enum class command_t {
	NONE,

	CUSTOM,
	IDLE,
	MOVE,
	BUILD_TOWER,
	ATTACK_MOVE,
	FOCUS_ATTACK,
	SET_STANCE,
	PATROL,
	DEMOLISH_TOWER,
	SET_RALLY_POINT,
	GARRISON,
	UNGARRISON,
	SET_UNGARRISON_POINT,
};

} // namespace openage::gamestate::component::command
