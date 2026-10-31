// Copyright 2021-2023 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

#include "coord/pixel.h"
#include "coord/tile.h"
#include "curve/discrete.h"
#include "gamestate/types.h"
#include "input/event.h"
#include "network/messages.h"


namespace openage {

namespace gamestate {
class GameSimulation;
}

namespace time {
class TimeLoop;
}

namespace renderer::world {
class RenderEntity;
}

namespace input::game {

class BindingContext;

/** Kind of the currently selected FVD entity, as resolved by game simulation. */
enum class SelectionKind {
	NONE,
	BUILDER,
	TOWER,
	HORSE,
	GROUP_HORSES,
	/** The Hunter's central tower; right-click sets its rally point. */
	HUNTER_TOWER,
	/** A Builder's planned or queued tower site; Delete removes it. */
	FOUNDATION,
	OTHER,
};

/** A controller-owned snapshot; health is read from its render entity. */
struct SelectionDetails {
	std::vector<gamestate::entity_id_t> ids;
	SelectionKind kind = SelectionKind::NONE;
	std::shared_ptr<renderer::world::RenderEntity> render_entity;
};

/**
 * Interface for game controllers.
 *
 * Controllers handle inputs from outside of a game (e.g. GUI, AI, scripts, ...)
 * and pass the resulting events to game entities. They also act as a form of
 * access control for using in-game functionality of game entities.
 */
class Controller : public std::enable_shared_from_this<Controller> {
public:
	Controller(const std::unordered_set<size_t> &controlled_factions,
	           size_t active_faction_id);

	~Controller() = default;

	/**
	 * Switch the actively controlled faction by the controller.
	 * The ID must be in the list of controlled factions.
	 *
	 * @param faction_id ID of the new active faction.
	 */
	void set_control(size_t faction_id);

	/** Fix the one lobby-assigned faction and disable solo role switching. */
	void lock_to_online_player(size_t faction_id);

	/** Restore available FVD factions when returning to the lobby. */
	void reset_online_player_slots(size_t player_count);

	/**
	 * Get the ID of the faction actively controlled by the controller.
	 *
	 * @return ID of the active faction.
	 */
	size_t get_controlled() const;

	/**
	 * Get the currently selected entities.
	 *
	 * @return Selected entities.
	 */
	std::vector<gamestate::entity_id_t> get_selected() const;

	/**
	 * Set the currently selected entities.
	 *
	 * @param ids Selected entities.
	 */
	void set_selected(
		const std::vector<gamestate::entity_id_t> ids,
		SelectionKind kind = SelectionKind::NONE,
		const std::shared_ptr<renderer::world::RenderEntity> &render_entity = nullptr);

	/** Start an asynchronous selection update and return its generation. */
	uint64_t begin_selection_update();

	/** Apply a selection result only if it is the latest pending update. */
	bool set_selected_if_current(
		uint64_t generation,
		const std::vector<gamestate::entity_id_t> ids,
		SelectionKind kind = SelectionKind::NONE,
		const std::shared_ptr<renderer::world::RenderEntity> &render_entity = nullptr);

	/**
	 * Defer an input action while the latest selection update is pending.
	 *
	 * Only the most recent deferred action is retained.
	 *
	 * @return Whether the action was deferred.
	 */
	bool defer_until_selection_applied(std::function<void()> action);

	/** Get the selected units and resolved type as a single thread-safe snapshot. */
	SelectionDetails get_selection_details() const;

	/** Enter FVD tower placement mode when a Builder has been selected. */
	bool begin_tower_placement();

	/** Cancel a tower placement without issuing a world command. */
	void cancel_tower_placement();

	/** Prepare a human Hunter attack-move destination from the next map click. */
	bool begin_attack_move();
	bool is_attack_move_active() const;

	/** Prepare a human Hunter patrol destination from the next map click. */
	bool begin_patrol();
	bool is_patrol_active() const;

	/** Leave attack-move or patrol targeting without issuing an order. */
	void cancel_hunter_targeting();

	/** Whether the active Hunter has selected horses it may command. */
	bool is_hunter_selection() const;

	/** Bind the same local/guest stance command used by Hunter hotkeys to the HUD. */
	void set_hunter_stance_action(std::function<void(gamestate::HunterStance)> action);

	/** Issue a stance for the currently selected Hunter horses, when permitted. */
	bool issue_hunter_stance(gamestate::HunterStance stance);

	/** Whether the next normal map click should place a tower. */
	bool is_tower_placement_active() const;

	/**
	 * Process an input event from the input manager.
	 *
	 * @param ev_args Input event and arguments.
	 * @param ctx Binding context for looking up the event transformation.
	 *
	 * @return true if the event is accepted, else false.
	 */
	bool process(const event_arguments &ev_args, const std::shared_ptr<BindingContext> &ctx);

	/**
	 * Set the start position of a drag selection.
	 *
	 * @param start Start position of the drag selection.
	 * @param world_start World tile under the cursor when the drag began.
	 */
	void set_drag_select_start(const coord::input &start,
	                           const coord::tile &world_start);

	/**
	 * Get the start position of a drag selection.
	 *
	 * @return Start position of the drag selection.
	 */
	const coord::input get_drag_select_start() const;

	/** Current left-button drag origin, or none when no drag is underway. */
	std::optional<coord::input> get_drag_select_start_optional() const;

	/** World-tile origin captured when the current left-button drag began. */
	std::optional<coord::tile> get_drag_world_start_optional() const;

	/**
	 * Reset the drag select start position.
	 */
	void reset_drag_select();

private:
	/**
	 * Factions controllable by this controller.
	 */
	std::unordered_set<size_t> controlled_factions;

	/**
	 * ID of the currently active faction.
	 */
	size_t active_faction_id;

	/**
	 * Currently selected entities.
	 */
	std::vector<gamestate::entity_id_t> selected;

	/** Simulation-resolved selection kind and thread-safe health source. */
	SelectionKind selected_kind = SelectionKind::NONE;
	std::shared_ptr<renderer::world::RenderEntity> selected_render_entity;
	uint64_t selection_generation = 0;
	bool selection_pending = false;
	std::function<void()> deferred_selection_action{};

	/** Which order the next Hunter map click issues. */
	enum class HunterTargeting {
		NONE,
		ATTACK_MOVE,
		PATROL,
	};

	/** Enter a Hunter targeting mode when horses are selected. */
	bool begin_hunter_targeting(HunterTargeting mode);

	/** Controller-local input mode, not authoritative simulation state. */
	bool placing_tower = false;
	HunterTargeting hunter_targeting = HunterTargeting::NONE;
	std::function<void(gamestate::HunterStance)> hunter_stance_action{};

	/**
	 * Screen and world positions captured at the start of a drag selection.
	 *
	 * TODO: Move this into an input event.
	 */
	struct DragStart {
		coord::input screen;
		coord::tile world;
	};
	std::optional<DragStart> drag_start;

	/**
	 * Mutex for threaded access.
	 */
	mutable std::recursive_mutex mutex;
};

/**
 * A box that catches units selects only the units, as in AoE2: buildings
 * and foundations swept up with them (the Hunter tower, a Builder's towers)
 * are left out so the units can still be ordered. A box with no units keeps
 * what it caught.
 */
std::vector<gamestate::entity_id_t> units_take_priority(
	const std::vector<gamestate::entity_id_t> &ids,
	const std::function<bool(gamestate::entity_id_t)> &is_unit);

/** Ordered sites constrained to whichever isometric map axis spans the drag furthest. */
std::vector<coord::tile> tower_line_sites(const coord::tile &start,
                                          const coord::tile &end);

/**
 * Setup default controller action bindings:
 *
 * - B, then Left Mouse click: Build a tower; Shift-click to append sites.
 * - Shift+left-drag: queue a straight line of foundations along one map axis.
 * - F9: Skip the first Hunter spawn delay on the authoritative simulation.
 * - Right Mouse click: Move to terrain; Hunter right-clicks enemy to
 *   focus-attack, while a Builder right-clicks a queued foundation to
 *   clear its other orders and construct that foundation.
 *
 * @param ctx Binding context the actions are added to.
 * @param controller Active controller receiving the shared Hunter stance command.
 * @param time_loop Time loop for getting simulation time.
 * @param simulation Game simulation.
 * @param camera Active game camera.
 * @param pick_entity Callback for finding the visible entity under a window pixel.
 * @param can_spawn_hunter Whether this process owns the authoritative FVD simulation.
 */
void setup_defaults(const std::shared_ptr<BindingContext> &ctx,
                    const std::shared_ptr<Controller> &controller,
                    const std::shared_ptr<time::TimeLoop> &time_loop,
                    const std::shared_ptr<openage::gamestate::GameSimulation> &simulation,
                    const std::shared_ptr<renderer::camera::Camera> &camera,
	                const std::function<void(
		                const coord::input &,
		                const std::function<void(std::optional<gamestate::entity_id_t>)> &)> &pick_entity,
                    const std::function<bool(network::CommandIntent)> &send_guest_command = {},
                    const std::function<std::optional<network::WorldState>()> &guest_world = {},
                    bool online_lobby = false,
                    const std::function<bool()> &can_spawn_hunter = {});

} // namespace input::game
} // namespace openage
