// Copyright 2026 the openage authors. See copying.md for legal info.

#include "command_bridge.h"

#include <optional>
#include <utility>
#include <vector>

#include "gamestate/component/internal/commands/types.h"
#include "gamestate/types.h"
#include "testing/testing.h"

namespace openage::network::tests {

void command_params() {
	using gamestate::component::command::command_t;
	using param_map = ::openage::event::EventHandler::param_map;

	const std::vector<std::pair<CommandKind, command_t>> kinds{
		{CommandKind::MOVE, command_t::MOVE},
		{CommandKind::ATTACK_MOVE, command_t::ATTACK_MOVE},
		{CommandKind::BUILD_TOWER, command_t::BUILD_TOWER},
		{CommandKind::FOCUS_ATTACK, command_t::FOCUS_ATTACK},
		// The simulation resolves targeted, optionally queued foundation moves.
		{CommandKind::PRIORITIZE_FOUNDATION, command_t::MOVE},
		{CommandKind::SET_STANCE, command_t::SET_STANCE},
		{CommandKind::PATROL, command_t::PATROL},
		{CommandKind::DEMOLISH_TOWER, command_t::DEMOLISH_TOWER},
		{CommandKind::SET_RALLY_POINT, command_t::SET_RALLY_POINT},
		{CommandKind::GARRISON, command_t::GARRISON},
		{CommandKind::UNGARRISON, command_t::UNGARRISON},
		{CommandKind::SET_UNGARRISON_POINT, command_t::SET_UNGARRISON_POINT},
		{CommandKind::FLARE, command_t::FLARE},
		{CommandKind::DELETE_FOUNDATIONS, command_t::DELETE_FOUNDATIONS},
	};
	for (const auto &[kind, expected] : kinds) {
		const param_map params{network::command_params(CommandIntent{0, kind}, 2)};
		TESTEQUALS(params.get("type", command_t::NONE) == expected, true);
	}

	// Required fields are always present; unset optional fields are omitted.
	const param_map minimal{network::command_params(
		CommandIntent{7, CommandKind::MOVE, {10, 11}, {3, 4, 0}}, 2)};
	TESTEQUALS(minimal.check_type<gamestate::player_id_t>("owner"), true);
	TESTEQUALS(minimal.get<gamestate::player_id_t>("owner", 0), gamestate::player_id_t{2});
	TESTEQUALS(minimal.get<std::vector<gamestate::entity_id_t>>("entity_ids"),
	           (std::vector<gamestate::entity_id_t>{10, 11}));
	TESTEQUALS(minimal.check_type<coord::phys3>("target"), true);
	TESTEQUALS(minimal.get<coord::phys3>("target", coord::phys3{0, 0, 0}),
	           (coord::phys3{3, 4, 0}));
	TESTEQUALS(minimal.check_type<bool>("queue"), true);
	TESTEQUALS(minimal.get("queue", true), false);
	TESTEQUALS(minimal.contains("result_cb"), false);
	TESTEQUALS(minimal.contains("target_entity"), false);
	TESTEQUALS(minimal.contains("build_line"), false);
	TESTEQUALS(minimal.contains("stance"), false);

	CommandIntent full{0, CommandKind::BUILD_TOWER, {12}, {5.5, 6.5, 0}};
	full.queue = true;
	full.target_entity = 42;
	full.build_line = {{5.5, 6.5, 0}, {6.5, 6.5, 0}};
	full.stance = gamestate::HunterStance::DEFENSIVE;
	std::optional<bool> decided;
	const param_map complete{network::command_params(
		full, 3, [&decided](bool accepted) { decided = accepted; })};
	TESTEQUALS(complete.get<gamestate::player_id_t>("owner", 0), gamestate::player_id_t{3});
	TESTEQUALS(complete.get("queue", false), true);
	TESTEQUALS(complete.get<gamestate::entity_id_t>("target_entity", 0),
	           gamestate::entity_id_t{42});
	TESTEQUALS(complete.get<std::vector<coord::phys3>>("build_line") == full.build_line, true);
	TESTEQUALS(complete.get("stance", gamestate::HunterStance::OBSERVE)
	               == gamestate::HunterStance::DEFENSIVE,
	           true);
	const auto callback = complete.get("result_cb", command_result_callback_t{});
	TESTEQUALS(static_cast<bool>(callback), true);
	callback(true);
	TESTEQUALS(decided, std::optional<bool>{true});

	TESTTHROWS(network::command_params(CommandIntent{0, static_cast<CommandKind>(0)}, 2));
}

} // namespace openage::network::tests
