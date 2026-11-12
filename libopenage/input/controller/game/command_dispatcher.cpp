// Copyright 2026 the openage authors. See copying.md for legal info.

#include "command_dispatcher.h"

#include <utility>

#include "error/error.h"
#include "event/event_loop.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "time/clock.h"
#include "time/time_loop.h"


namespace openage::input::game {

CommandDispatcher::CommandDispatcher(guest_sender_t send_guest,
                                     guest_world_t guest_world,
                                     local_world_t local_world,
                                     local_submitter_t submit_local) :
	send_guest{std::move(send_guest)},
	replicated_world{std::move(guest_world)},
	local_world{std::move(local_world)},
	submit_local{std::move(submit_local)} {
	if (not this->local_world or not this->submit_local) {
		throw Error{MSG(err) << "Command dispatcher requires local world and command sources"};
	}
}

std::shared_ptr<CommandDispatcher> CommandDispatcher::for_simulation(
	const std::shared_ptr<gamestate::GameSimulation> &simulation,
	const std::shared_ptr<time::TimeLoop> &time_loop,
	guest_sender_t send_guest,
	guest_world_t guest_world) {
	return std::make_shared<CommandDispatcher>(
		std::move(send_guest),
		std::move(guest_world),
		[simulation]() {
			return simulation->get_fvd_presentation_state();
		},
		[simulation, time_loop](const event::EventHandler::param_map &params) {
			simulation->get_event_loop()->submit_event(
				"game.send_command",
				simulation->get_commander(),
				simulation->get_game()->get_state(),
				time_loop->get_clock()->get_time(),
				params);
		});
}

CommandRoute CommandDispatcher::route() const {
	return this->guest_world() ? CommandRoute::GUEST : CommandRoute::LOCAL;
}

std::shared_ptr<const network::WorldState> CommandDispatcher::guest_world() const {
	if (not this->replicated_world) {
		return nullptr;
	}
	auto replica = this->replicated_world();
	if (not replica) {
		return nullptr;
	}
	return std::make_shared<const network::WorldState>(std::move(*replica));
}

WorldSnapshot CommandDispatcher::snapshot() const {
	if (auto replica = this->guest_world()) {
		return WorldSnapshot{std::move(replica), CommandRoute::GUEST};
	}
	return WorldSnapshot{this->local_world(), CommandRoute::LOCAL};
}

bool CommandDispatcher::dispatch(const network::CommandIntent &intent,
                                 gamestate::player_id_t owner,
                                 network::command_result_callback_t result_callback) const {
	return this->dispatch(this->route(), intent, owner, std::move(result_callback));
}

bool CommandDispatcher::dispatch(CommandRoute route,
                                 const network::CommandIntent &intent,
                                 gamestate::player_id_t owner,
                                 network::command_result_callback_t result_callback) const {
	if (route == CommandRoute::GUEST) {
		return this->send_guest and this->send_guest(intent);
	}
	this->submit_local(network::command_params(intent, owner, std::move(result_callback)));
	return true;
}

} // namespace openage::input::game
