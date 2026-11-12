// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "coord/phys.h"
#include "coord/pixel.h"
#include "gamestate/types.h"
#include "input/controller/game/controller.h"
#include "input/event.h"
#include "network/messages.h"


namespace openage {

namespace gamestate {
class FVD;
class GameSimulation;
} // namespace gamestate

namespace renderer::camera {
class Camera;
}

namespace time {
class TimeLoop;
}

namespace input::game {

class BindingContext;
class CommandDispatcher;

/**
 * Translates game input events into controller state changes and player
 * commands.
 *
 * The bindings registered by \p bind() share this object, so it must be
 * owned by a \p std::shared_ptr. It does not hold the controller it is
 * bound to, which owns the HUD actions that refer back to it.
 */
class GameBindings : public std::enable_shared_from_this<GameBindings> {
public:
	/**
	 * Create game bindings.
	 *
	 * @param time_loop Time loop for getting simulation time.
	 * @param simulation Game simulation, also used for selection events.
	 * @param camera Active game camera.
	 * @param pick_entity Finds a visible entity under a window pixel,
	 *                    optionally restricted to matching IDs. It must
	 *                    invoke its result callback before returning.
	 * @param commands Executes player commands on the local simulation or
	 *                 sends them to the host.
	 * @param can_spawn_hunter Whether this process owns the authoritative FVD
	 *                         simulation; unset means it does.
	 */
	GameBindings(std::shared_ptr<time::TimeLoop> time_loop,
	             std::shared_ptr<gamestate::GameSimulation> simulation,
	             std::shared_ptr<renderer::camera::Camera> camera,
	             entity_picker_t pick_entity,
	             std::shared_ptr<const CommandDispatcher> commands,
	             std::function<bool()> can_spawn_hunter = {});

	/**
	 * Add the default bindings to a binding context:
	 *
	 * - B, then Left Mouse click: Build a tower; Shift-click to append sites.
	 * - Shift+left-drag with Builder: queue a straight line of foundations.
	 * - Shift+left or Shift+right click an owned foundation with a Builder:
	 *   queue it without entering placement mode.
	 * - Shift+click structures: add same-type allied structures; double-click
	 *   a structure to select its owner's same-type on-screen structures.
	 * - Hunter: single-click Builder towers to inspect HP; no building groups.
	 * - T, then Left Mouse click on a tower: garrison the selected Builder.
	 * - G: ungarrison the selected tower. Delete: demolish the selected tower
	 *   or foundation.
	 * - A or P, then Left Mouse click: Hunter attack-move or patrol.
	 *   Z, X, C: Hunter stances.
	 * - Escape: cancel placement, garrison and Hunter targeting modes.
	 * - F9: Skip the first Hunter spawn delay on the authoritative simulation.
	 * - Number keys: switch the controlled player, except in online lobbies.
	 * - Right Mouse click: Move to terrain; Hunter right-clicks enemy to
	 *   focus-attack, while a Builder right-clicks a queued foundation to
	 *   clear its other orders and construct that foundation. A selected
	 *   tower sets its ungarrison point, the Hunter tower its rally point.
	 *
	 * Also sets the controller's HUD stance and ungarrison actions.
	 *
	 * @param ctx Binding context the actions are added to.
	 * @param controller Controller receiving the HUD actions.
	 * @param online_lobby Whether players are assigned by an online lobby.
	 */
	void bind(const std::shared_ptr<BindingContext> &ctx,
	          const std::shared_ptr<Controller> &controller,
	          bool online_lobby);

private:
	/** A Shift-click target, resolved against the world it was clicked in. */
	struct ShiftClick;

	/** A left mouse release, captured before selection updates are applied. */
	struct Release;

	using controller_t = std::shared_ptr<Controller>;

	void pick(const coord::input &position, const entity_pick_callback_t &callback) const;

	void place_tower(const coord::phys3 &position, bool append, const controller_t &controller) const;
	void issue_hunter_route(const event_arguments &args, const controller_t &controller) const;
	void set_stance(const controller_t &controller, gamestate::HunterStance stance) const;
	void demolish_selected(const controller_t &controller) const;
	void ungarrison(const controller_t &controller) const;
	void spawn_hunter() const;
	void switch_player(const gamestate::FVD &fvd, size_t id, const controller_t &controller) const;

	void issue_order(const event_arguments &args, const controller_t &controller) const;
	void submit_order(const event_arguments &args, const controller_t &controller) const;
	void set_ungarrison_point(const event_arguments &args,
	                          const SelectionDetails &selection,
	                          gamestate::player_id_t owner) const;
	void submit_guest_order(const event_arguments &args,
	                        const SelectionDetails &selection,
	                        gamestate::player_id_t owner,
	                        std::shared_ptr<const network::WorldState> replica) const;
	void submit_local_order(const event_arguments &args,
	                        const SelectionDetails &selection,
	                        gamestate::player_id_t owner) const;

	void resolve_queue_click(const coord::input &position,
	                         const coord::phys3 &mouse_pos,
	                         gamestate::player_id_t owner,
	                         std::function<void(const ShiftClick &)> callback) const;
	void queue_existing_foundation(const std::vector<gamestate::entity_id_t> &ids,
	                               const ShiftClick &click) const;
	void queue_foundation(const event_arguments &args, const controller_t &controller) const;

	void init_drag(const event_arguments &args, const controller_t &controller) const;
	void left_release(const event_arguments &args, const controller_t &controller) const;
	void process_release(const Release &release, const controller_t &controller) const;
	void pick_at_click(const Release &release, const entity_pick_callback_t &callback) const;
	void garrison(const Release &release,
	              const controller_t &controller,
	              const SelectionDetails &selection) const;
	void build_shift_click(const Release &release,
	                       const controller_t &controller,
	                       const SelectionDetails &selection) const;
	void select_guest(const Release &release,
	                  const controller_t &controller,
	                  const SelectionDetails &selection,
	                  uint64_t generation,
	                  std::shared_ptr<const network::WorldState> replica) const;
	void select_local(const Release &release,
	                  const controller_t &controller,
	                  const SelectionDetails &selection,
	                  uint64_t generation) const;
	void select_structure_group(const event_arguments &args, const controller_t &controller) const;

	std::shared_ptr<time::TimeLoop> time_loop;
	std::shared_ptr<gamestate::GameSimulation> simulation;
	std::shared_ptr<renderer::camera::Camera> camera;
	entity_picker_t pick_entity;
	std::shared_ptr<const CommandDispatcher> commands;
	std::function<bool()> can_spawn_hunter;
};

/**
 * Bind the default game controls of \p GameBindings for a simulation.
 *
 * @param ctx Binding context the actions are added to.
 * @param controller Controller receiving the HUD actions.
 * @param time_loop Time loop for getting simulation time.
 * @param simulation Game simulation.
 * @param camera Active game camera.
 * @param pick_entity Callback for finding a visible entity under a window pixel,
 *                    optionally restricted to matching IDs. It must invoke its
 *                    result callback before returning.
 * @param send_guest_command Sends an online guest's intent to the host.
 * @param guest_world Replicated host world while playing as an online guest.
 * @param online_lobby Whether players are assigned by an online lobby.
 * @param can_spawn_hunter Whether this process owns the authoritative FVD simulation.
 */
void setup_defaults(const std::shared_ptr<BindingContext> &ctx,
                    const std::shared_ptr<Controller> &controller,
                    const std::shared_ptr<time::TimeLoop> &time_loop,
                    const std::shared_ptr<openage::gamestate::GameSimulation> &simulation,
                    const std::shared_ptr<renderer::camera::Camera> &camera,
                    const entity_picker_t &pick_entity,
                    const std::function<bool(network::CommandIntent)> &send_guest_command = {},
                    const std::function<std::optional<network::WorldState>()> &guest_world = {},
                    bool online_lobby = false,
                    const std::function<bool()> &can_spawn_hunter = {});

} // namespace input::game
} // namespace openage
