// Copyright 2026 the openage authors. See copying.md for legal info.

#include "command_dispatcher.h"

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include "gamestate/component/internal/commands/types.h"
#include "testing/testing.h"

namespace openage::input::game::tests {

void command_dispatcher() {
	using gamestate::component::command::command_t;
	using param_map = event::EventHandler::param_map;

	std::vector<param_map> submitted;
	size_t local_captures = 0;
	const auto local_state = std::make_shared<const network::WorldState>();
	const auto local_world = [&local_captures, &local_state]() {
		++local_captures;
		return local_state;
	};
	const auto submit = [&submitted](const param_map &params) {
		submitted.push_back(params);
	};

	TESTTHROWS((CommandDispatcher{{}, {}, {}, submit}));
	TESTTHROWS((CommandDispatcher{{}, {}, local_world, {}}));

	// Solo play: commands become local send_command events.
	const CommandDispatcher solo{{}, {}, local_world, submit};
	TESTEQUALS(solo.route() == CommandRoute::LOCAL, true);
	TESTEQUALS(solo.guest_world() == nullptr, true);
	TESTEQUALS(local_captures, size_t{0});
	const auto solo_snapshot = solo.snapshot();
	TESTEQUALS(solo_snapshot.route == CommandRoute::LOCAL, true);
	TESTEQUALS(solo_snapshot.world == local_state, true);
	TESTEQUALS(local_captures, size_t{1});

	network::CommandIntent garrison{0, network::CommandKind::GARRISON, {7}};
	garrison.target_entity = 9;
	std::optional<bool> decided;
	TESTEQUALS(solo.dispatch(garrison, 2, [&decided](bool accepted) { decided = accepted; }), true);
	TESTEQUALS(submitted.size(), size_t{1});
	const auto &local_params = submitted.back();
	TESTEQUALS(local_params.get("type", command_t::NONE) == command_t::GARRISON, true);
	TESTEQUALS(local_params.get<gamestate::player_id_t>("owner", 0), gamestate::player_id_t{2});
	TESTEQUALS(local_params.get<gamestate::entity_id_t>("target_entity", 0),
	           gamestate::entity_id_t{9});
	local_params.get("result_cb", network::command_result_callback_t{})(false);
	TESTEQUALS(decided, std::optional<bool>{false});

	// Online: the route follows whether a replicated host world exists.
	std::vector<network::CommandIntent> sent;
	bool send_accepted = true;
	std::optional<network::WorldState> replica;
	const CommandDispatcher online{
		[&sent, &send_accepted](network::CommandIntent intent) {
			sent.push_back(std::move(intent));
			return send_accepted;
		},
		[&replica]() { return replica; },
		local_world,
		submit};
	const network::CommandIntent ungarrison{0, network::CommandKind::UNGARRISON, {7}};
	TESTEQUALS(online.route() == CommandRoute::LOCAL, true);
	TESTEQUALS(online.dispatch(ungarrison, 2), true);
	TESTEQUALS(submitted.size(), size_t{2});
	TESTEQUALS(sent.empty(), true);

	replica = network::WorldState{};
	replica->local_player_id = 3;
	TESTEQUALS(online.route() == CommandRoute::GUEST, true);
	TESTEQUALS(online.guest_world()->local_player_id, gamestate::player_id_t{3});
	const auto captures = local_captures;
	const auto guest_snapshot = online.snapshot();
	TESTEQUALS(guest_snapshot.route == CommandRoute::GUEST, true);
	TESTEQUALS(guest_snapshot.world->local_player_id, gamestate::player_id_t{3});
	TESTEQUALS(local_captures, captures);

	// The guest learns the decision from the host's command result.
	bool called = false;
	const network::CommandIntent demolish{0, network::CommandKind::DEMOLISH_TOWER, {12}};
	TESTEQUALS(online.dispatch(demolish, 3, [&called](bool) { called = true; }), true);
	TESTEQUALS(sent.size(), size_t{1});
	TESTEQUALS(sent.back().kind == network::CommandKind::DEMOLISH_TOWER, true);
	TESTEQUALS(sent.back().entity_ids, (std::vector<gamestate::entity_id_t>{12}));
	TESTEQUALS(submitted.size(), size_t{2});
	TESTEQUALS(called, false);
	send_accepted = false;
	TESTEQUALS(online.dispatch(demolish, 3), false);
	TESTEQUALS(sent.size(), size_t{2});

	// A route captured with the click target overrides the current route.
	TESTEQUALS(online.dispatch(CommandRoute::LOCAL, demolish, 3), true);
	TESTEQUALS(submitted.size(), size_t{3});
	replica.reset();
	send_accepted = true;
	TESTEQUALS(online.dispatch(CommandRoute::GUEST, demolish, 3), true);
	TESTEQUALS(sent.size(), size_t{3});
	TESTEQUALS(submitted.size(), size_t{3});

	// Without a sender a guest command cannot leave the client.
	const CommandDispatcher unsent{{}, [&replica]() { return replica; }, local_world, submit};
	replica = network::WorldState{};
	TESTEQUALS(unsent.route() == CommandRoute::GUEST, true);
	TESTEQUALS(unsent.dispatch(demolish, 3), false);
	TESTEQUALS(submitted.size(), size_t{3});
}

} // namespace openage::input::game::tests
