// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "event/eventhandler.h"
#include "gamestate/types.h"
#include "network/command_bridge.h"
#include "network/messages.h"


namespace openage {

namespace gamestate {
class GameSimulation;
}

namespace time {
class TimeLoop;
}

namespace input::game {

/** Where player commands are executed. */
enum class CommandRoute {
	/** The local simulation is authoritative. */
	LOCAL,
	/** An online guest sends intents to the authoritative host. */
	GUEST,
};

/** World state for resolving click targets, with the route it belongs to. */
struct WorldSnapshot {
	/**
	 * Replicated host world for a guest, the local presentation state
	 * otherwise. Null until the local simulation captured its first state.
	 */
	std::shared_ptr<const network::WorldState> world;
	CommandRoute route{CommandRoute::LOCAL};
};

/**
 * Executes player commands described as network intents.
 *
 * An online guest sends intents to the authoritative host; otherwise they
 * run as `game.send_command` events on the local simulation, with the same
 * parameters the host uses for authenticated guest intents.
 */
class CommandDispatcher {
public:
	using guest_sender_t = std::function<bool(network::CommandIntent)>;
	using guest_world_t = std::function<std::optional<network::WorldState>()>;
	using local_world_t = std::function<std::shared_ptr<const network::WorldState>()>;
	using local_submitter_t = std::function<void(const event::EventHandler::param_map &)>;

	/**
	 * Create a dispatcher from its command and world sources.
	 *
	 * @param send_guest Sends a guest intent to the host; returns whether it was accepted for sending.
	 * @param guest_world Replicated host world while playing as a guest, \p std::nullopt otherwise.
	 * @param local_world Presentation state of the local simulation.
	 * @param submit_local Submits `game.send_command` parameters to the local simulation.
	 */
	CommandDispatcher(guest_sender_t send_guest,
	                  guest_world_t guest_world,
	                  local_world_t local_world,
	                  local_submitter_t submit_local);

	/**
	 * Create a dispatcher that executes local commands on a simulation at
	 * the current time of its time loop.
	 *
	 * @param simulation Local game simulation.
	 * @param time_loop Time loop providing the current simulation time.
	 * @param send_guest Sends a guest intent to the host.
	 * @param guest_world Replicated host world while playing as a guest.
	 */
	static std::shared_ptr<CommandDispatcher> for_simulation(
		const std::shared_ptr<gamestate::GameSimulation> &simulation,
		const std::shared_ptr<time::TimeLoop> &time_loop,
		guest_sender_t send_guest,
		guest_world_t guest_world);

	/**
	 * Get the route commands currently take.
	 *
	 * @return \p CommandRoute::GUEST while a replicated host world exists.
	 */
	CommandRoute route() const;

	/**
	 * Get the replicated host world without querying the local simulation.
	 *
	 * @return Host world while playing as a guest, \p nullptr otherwise.
	 */
	std::shared_ptr<const network::WorldState> guest_world() const;

	/**
	 * Get the world that click targets are resolved against.
	 *
	 * The local presentation state is only requested when not playing as a
	 * guest, because requesting it schedules a new capture.
	 *
	 * @return Guest replica or local presentation state, and its route.
	 */
	WorldSnapshot snapshot() const;

	/**
	 * Execute a command on the current route.
	 *
	 * @param intent Command to execute.
	 * @param owner Player issuing the command.
	 * @param result_callback Invoked with the local simulation's decision. A
	 *                        guest learns the host's decision from its
	 *                        command result instead.
	 *
	 * @return Whether the command was submitted or sent.
	 */
	bool dispatch(const network::CommandIntent &intent,
	              gamestate::player_id_t owner,
	              network::command_result_callback_t result_callback = {}) const;

	/**
	 * Execute a command on a route captured together with its click target.
	 *
	 * @param route Route the command's target was resolved for.
	 * @param intent Command to execute.
	 * @param owner Player issuing the command.
	 * @param result_callback Invoked with the local simulation's decision.
	 *
	 * @return Whether the command was submitted or sent.
	 */
	bool dispatch(CommandRoute route,
	              const network::CommandIntent &intent,
	              gamestate::player_id_t owner,
	              network::command_result_callback_t result_callback = {}) const;

private:
	guest_sender_t send_guest;
	guest_world_t replicated_world;
	local_world_t local_world;
	local_submitter_t submit_local;
};

} // namespace input::game
} // namespace openage
