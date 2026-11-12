// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <functional>
#include <memory>

#include "event/eventhandler.h"
#include "gamestate/types.h"
#include "network/command_mailbox.h"
#include "network/messages.h"

namespace openage::event {
class EventLoop;
}

namespace openage::gamestate {
class GameState;

namespace event {
class Commander;
}
}

namespace openage::network {

class MatchConnectionState;

/** Result callback invoked when the authoritative host decides the command. */
using command_result_callback_t = std::function<void(bool)>;

/**
 * Parameters for the `game.send_command` event executing an intent.
 *
 * Local and authenticated remote commands share this mapping so both reach
 * identical simulation logic. Unset optional intent fields and an empty
 * result callback are omitted.
 *
 * @param intent Command to execute.
 * @param owner Player issuing the command.
 * @param result_callback Invoked with the simulation's decision, if set.
 */
::openage::event::EventHandler::param_map::map_t command_params(
	const CommandIntent &intent,
	gamestate::player_id_t owner,
	command_result_callback_t result_callback = {});

/**
 * Translate an authenticated network command into a simulation event.
 *
 * Ownership comes from the immutable accepted-session identity paired with
 * the intent by CommandMailbox.
 */
void submit_authenticated_command(
	const AuthenticatedCommand &command,
	const std::shared_ptr<::openage::event::EventLoop> &event_loop,
	const std::shared_ptr<gamestate::event::Commander> &commander,
	const std::shared_ptr<gamestate::GameState> &state,
	const time::time_t &time,
	command_result_callback_t result_callback);

/**
 * Submit only if the original session still owns an active match slot.
 *
 * Call this variant when draining an authenticated command mailbox on the
 * simulation thread. A stale/disconnected session is rejected immediately
 * without ever scheduling a gameplay mutation.
 */
void submit_authenticated_command(
	const AuthenticatedCommand &command,
	const MatchConnectionState &connections,
	const std::shared_ptr<::openage::event::EventLoop> &event_loop,
	const std::shared_ptr<gamestate::event::Commander> &commander,
	const std::shared_ptr<gamestate::GameState> &state,
	const time::time_t &time,
	command_result_callback_t result_callback);

/**
 * Capture entity positions, Hunter stances, construction orders and
 * reservations together.
 *
 * Call only on the simulation owner thread, after processing events.
 * This local presentation snapshot omits network identity, private scores,
 * health, future motion and projectiles, and includes every horse's stance;
 * it must not be sent over the wire.
 */
WorldState capture_fvd_presentation_state(
	const std::shared_ptr<gamestate::GameState> &state,
	const time::time_t &time);

/** Capture complete authoritative FVD state on the simulation owner thread. */
WorldState capture_fvd_state(
	const std::shared_ptr<gamestate::GameState> &state,
	const MatchConnectionState &connections,
	const time::time_t &time,
	uint64_t base_revision,
	uint64_t world_revision,
	gamestate::player_id_t recipient,
	uint64_t delivered_projectile_id = 0);

} // namespace openage::network
