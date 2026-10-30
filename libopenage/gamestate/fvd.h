// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nyan/nyan.h>

#include "coord/phys.h"
#include "coord/tile.h"
#include "gamestate/types.h"
#include "network/messages.h"
#include "pathfinding/types.h"
#include "time/time.h"

namespace openage::event {
class Event;
class EventLoop;
class EventEntity;
}

namespace openage::gamestate {

/** Seconds until a pending first Hunter spawn, rounded up for HUD display. */
std::optional<int> hunter_spawn_countdown(
	const std::optional<time::time_t> &spawn_time,
	const time::time_t &now);

class EntityFactory;
class GameEntity;
class GameState;
class TerrainFactory;

namespace component::command {
class BuildTowerCommand;
}

/** Construction phase exposed at the simulation/network boundary. */
enum class FVDConstructionPhase : uint16_t {
	PLANNED = 1,
	UNDER_CONSTRUCTION = 2,
};

/** Read-only description of one active construction order. */
struct FVDConstructionState {
	uint64_t order_id;
	player_id_t owner;
	entity_id_t builder_id;
	coord::tile target;
	FVDConstructionPhase phase;
	time::time_t phase_end_time;
	entity_id_t site_entity_id;
	/** Actual start of timed construction; planned orders have no progress. */
	time::time_t phase_start_time;

	/** Percentage of completed construction, clamped to 0..100. */
	int progress_percent(const time::time_t &now) const;
};

/** One footprint tile reserved by an active construction order. */
struct FVDTileReservation {
	coord::tile tile;
	entity_id_t builder_id;
};

/** Read-only description of one active Hunter combat order. */
struct FVDAttackOrderState {
	entity_id_t horse_id;
	coord::phys3 destination;
	std::optional<entity_id_t> target_id;
	bool reached_destination;
	bool focus_target;
};

/** Read-only timed projectile launch (Tower or Hunter). */
struct FVDArrowState {
	uint64_t arrow_id;
	player_id_t owner;
	entity_id_t target_id;
	coord::phys3 start;
	coord::phys3 destination;
	time::time_t launch_time;
	time::time_t impact_time;
};

/** Side whose shared vision a player uses. */
enum class FVDTeam : uint8_t {
	HUNTER = 1,
	BUILDERS = 2,
};

/** Last seen state of an enemy building now hidden by fog. */
struct FVDRememberedBuilding {
	entity_id_t entity_id;
	player_id_t owner;
	std::string definition;
	coord::phys3 position;
};

/**
 * One side's view of the map: one bit per tile in row-major (ne, se) order,
 * bit i of word i / 64 for tile index i = ne * map_size + se.
 */
struct FVDTeamVision {
	std::vector<uint64_t> explored;
	std::vector<uint64_t> visible;
	/** Enemy buildings seen before and not yet seen gone, in ID order. */
	std::vector<FVDRememberedBuilding> remembered;
};

/**
 * Flat plains scenario with one Hunter and the remaining players as Builders.
 */
class FVD {
public:
	/** Purely visual render snapshot; authoritative damage is separate. */
	struct ArrowTrail {
		coord::phys3 tail;
		coord::phys3 head;
		double progress;
		player_id_t owner;
		bool impact = false;
	};
	/** Read and validate the scenario's nyan settings. */
	explicit FVD(const std::shared_ptr<GameState> &state);

	/** Create terrain, players and starting Builders. */
	void initialise(const std::shared_ptr<GameState> &state,
	                const std::shared_ptr<openage::event::EventLoop> &loop,
	                const std::shared_ptr<EntityFactory> &entities,
	                const std::shared_ptr<TerrainFactory> &terrain);

	/**
	 * Validate a Builder construction request, route to a reachable adjacent
	 * stand tile and schedule construction completion in simulation time.
	 *
	 * @return Whether the order was accepted.
	 */
	bool request_build(const std::shared_ptr<GameState> &state,
	                   player_id_t owner,
	                   const std::vector<entity_id_t> &selected,
	                   const coord::phys3 &position,
	                   const time::time_t &time);

	/**
	 * Stop an outstanding construction order and release its site reservation.
	 *
	 * @return Whether an order was canceled.
	 */
	bool cancel_build(const std::shared_ptr<GameState> &state,
	                  entity_id_t builder,
	                  const time::time_t &time,
	                  bool preserve_foundation = true);

	/** Validate ownership and the full tower footprint for placement preview. */
	bool can_place_tower(const std::shared_ptr<GameState> &state,
	                     player_id_t owner, const std::vector<entity_id_t> &selected,
	                     const coord::phys3 &position, const time::time_t &time,
	                     std::optional<entity_id_t> existing_foundation = std::nullopt) const;

	/** Append a construction site for a Builder, reserving the footprint. */
	bool queue_build(const std::shared_ptr<GameState> &state,
	                 player_id_t owner, const std::vector<entity_id_t> &selected,
	                 const coord::phys3 &position, const time::time_t &time);

	/** Resume an owned orphan foundation or prioritize a queued foundation. */
	bool prioritize_foundation(const std::shared_ptr<GameState> &state,
	                           player_id_t owner, const std::vector<entity_id_t> &selected,
	                           entity_id_t foundation_id, const time::time_t &time);

	/** Number of sites waiting after the Builder's currently active tower. */
	size_t get_queued_build_count(entity_id_t builder) const;

	/** Full grid footprint of a potential tower. */
	std::vector<coord::tile> get_tower_footprint(const coord::tile &site) const;

	/** Get an unfinished construction order, or nullptr if absent. */
	std::shared_ptr<component::command::BuildTowerCommand> get_build_order(
		entity_id_t builder) const;

	/** Whether an unfinished construction order reserves this tile. */
	bool is_tile_reserved(const coord::tile &tile) const;

	/** Execute an arrival or completion event for a matching order ID. */
	void advance_build(const std::shared_ptr<GameState> &state,
	                   entity_id_t builder,
	                   uint64_t order_id,
	                   uint64_t route_revision,
	                   bool finish,
	                   const time::time_t &time);

	/** Recalculate current routes and release footprints of destroyed towers. */
	void refresh_paths(const std::shared_ptr<GameState> &state,
	                   const time::time_t &time);

	/**
	 * Immediately demolish exactly one selected tower owned by a Builder player.
	 * Reject foreign towers, non-towers and stale selections without mutation.
	 * In-progress construction is canceled without leaving a foundation.
	 */
	bool demolish_owned_tower(const std::shared_ptr<GameState> &state,
	                         player_id_t owner, const std::vector<entity_id_t> &ids,
	                         const time::time_t &time);

	/**
	 * Remove one of a Builder player's foundations: a queued site, the site
	 * a Builder is walking to (who then moves on to his next queued site), or
	 * an abandoned one. Construction already started is a tower instead.
	 */
	bool delete_owned_foundation(const std::shared_ptr<GameState> &state,
	                             player_id_t owner, entity_id_t foundation_id,
	                             const time::time_t &time);

	/** Destroy a tower and restore the path costs under its footprint. */
	bool destroy_tower(const std::shared_ptr<GameState> &state,
	                   entity_id_t tower_id,
	                   const time::time_t &time);

	/** Remove a demolition remnant if it still owns its scheduled expiry. */
	void expire_tower_demolition(const std::shared_ptr<GameState> &state,
	                            entity_id_t demolition_id,
	                            const time::time_t &time);

	/**
	 * Start the local match's Hunter spawn schedule at match simulation time.
	 * Safe to call more than once; no horses spawn while waiting to start.
	 */
	void start_hunter_wave(const std::shared_ptr<GameState> &state,
	                       const time::time_t &match_start);

	/**
	 * Seconds remaining until the first Hunter spawn, rounded up.
	 * The deadline is shared read-only with the presenter; no countdown
	 * exists before match start or after the first spawn deadline.
	 */
	std::optional<int> get_hunter_spawn_countdown(const time::time_t &now) const;

	/**
	 * Authoritative simulation deadline for the first Hunter spawn.
	 * Empty before the wave starts and after the first horse spawns.
	 */
	std::optional<time::time_t> get_first_hunter_spawn_time() const;

	/** Evaluate match end only on the authoritative simulation owner thread.
	 * Disconnects count as eliminated Builders; they do not remove towers.
	 * The first result is terminal, even across subsequent captures. */
	void evaluate_match_outcome(const std::shared_ptr<GameState> &state,
	                            const time::time_t &now,
	                            const std::vector<player_id_t> &disconnected = {});

	/** The final public winner and reason, if the match has ended. */
	std::optional<network::MatchOutcome> get_match_outcome() const;

	/** Host-simulation end-time deadline, if a match has started. */
	std::optional<time::time_t> get_match_end_deadline() const;

	/** Queue the first Hunter spawn immediately and replace its delayed schedule. */
	void request_immediate_hunter_spawn(const std::shared_ptr<GameState> &state,
	                                    const time::time_t &time);

	/**
	 * Spawn an idle Hunter horse and schedule the next one.
	 *
	 * @param skip_initial_delay Cancel the pending first spawn and begin the wave now.
	 */
	void spawn_hunter_horse(const std::shared_ptr<GameState> &state,
	                       const time::time_t &time,
	                       bool skip_initial_delay = false);

	/** Validate a complete group of player-owned movable units. */
	bool can_command_units(const std::shared_ptr<GameState> &state,
	                       player_id_t owner, const std::vector<entity_id_t> &ids,
	                       const time::time_t &time) const;

	/** Resolve a completed tower at its occupied tile, if one exists. */
	std::optional<entity_id_t> completed_tower_at(
		const std::shared_ptr<GameState> &state, const coord::tile &tile) const;

	/** Start Hunter AttackMove for every selected horse, or reject all. */
	bool issue_attack_move(const std::shared_ptr<GameState> &state,
	                       player_id_t owner, const std::vector<entity_id_t> &ids,
	                       const coord::phys3 &destination, const time::time_t &time);

	/** Focus a specific enemy entity, overriding automatic tower priority. */
	bool issue_focus_attack(const std::shared_ptr<GameState> &state,
	                        player_id_t owner, const std::vector<entity_id_t> &ids,
	                        entity_id_t enemy_id, const time::time_t &time);

	/**
	 * Loop every selected horse between its position and a destination,
	 * engaging enemies en route like AttackMove, or reject all.
	 */
	bool issue_patrol(const std::shared_ptr<GameState> &state,
	                  player_id_t owner, const std::vector<entity_id_t> &ids,
	                  const coord::phys3 &destination, const time::time_t &time);

	/**
	 * Set the standing behaviour of every selected Hunter horse, or reject all.
	 * Ends engagements started by the previous stance; human orders continue.
	 */
	bool set_hunter_stance(const std::shared_ptr<GameState> &state,
	                       player_id_t owner, const std::vector<entity_id_t> &ids,
	                       HunterStance stance, const time::time_t &time);

	/** Current stance of a Hunter horse; Observe unless the Hunter changed it. */
	HunterStance get_hunter_stance(entity_id_t horse) const;

	/** Remove the autonomous engagement portion when superseded by another order. */
	void cancel_attack_move(entity_id_t id);

	/** Authoritative combat simulation; called on a fixed simulation-time tick. */
	void combat_tick(const std::shared_ptr<GameState> &state, const time::time_t &time);

	/** Resolve one arrow with the firing tower's identity, even after its death. */
	void arrow_hit(const std::shared_ptr<GameState> &state, entity_id_t target,
	               player_id_t owner, entity_id_t firing_tower,
	               int64_t damage, const time::time_t &time);

	/** Apply a Hunter tower shot when its projectile lands. */
	void hunter_tower_hit(const std::shared_ptr<GameState> &state, entity_id_t target,
	                      int64_t damage, const time::time_t &time);

	/** The Hunter's central tower, if this scenario has one. */
	std::optional<entity_id_t> get_hunter_tower() const;

	/** Where new Hunter horses appear, beside the Hunter tower. */
	coord::phys3 get_horse_spawn_point() const;

	/**
	 * Set where newly spawned horses ride to, like an AoE2 rally point.
	 * Only the Hunter may set it, through its tower (the single entry of
	 * @p ids), on an in-bounds ground point horses can reach from their
	 * spawn point. A red marker shows it to the Hunter only.
	 */
	bool set_hunter_rally_point(const std::shared_ptr<GameState> &state,
	                            player_id_t owner, const std::vector<entity_id_t> &ids,
	                            const coord::phys3 &point, const time::time_t &time);

	/** The Hunter's rally point; without one, new horses stay where they spawn. */
	std::optional<coord::phys3> get_hunter_rally_point() const;

	/** The marker entity showing the rally point, while one is set. */
	std::optional<entity_id_t> get_hunter_rally_marker() const;

	/** Definition of the rally point marker, which only the Hunter may see. */
	const std::string &get_hunter_rally_marker_definition() const;

	/**
	 * Play without the Hunter tower: remove it, free its tile and spawn
	 * horses at the centre again. Used by scenarios built before it existed.
	 */
	void remove_hunter_tower(const std::shared_ptr<GameState> &state, const time::time_t &time);

	/** Apply a Hunter horse's pending strike at its animation's hit frame. */
	void horse_hit(const std::shared_ptr<GameState> &state,
	               entity_id_t horse, entity_id_t target,
	               int64_t damage, const time::time_t &time);

	/**
	 * Thread-safe snapshot of visible arrow trails at simulation time.
	 * Projectiles are never selectable or independently simulated clients.
	 */
	std::vector<ArrowTrail> get_visible_arrows(const time::time_t &now) const;

	/** Lifetime kills by all towers of this player, including destroyed towers. */
	uint64_t get_kills(player_id_t owner) const;

	/** Individual tower kills; only this tower's owning player may read them. */
	std::optional<uint64_t> get_tower_kills(entity_id_t tower, player_id_t owner) const;
	int64_t get_tower_damage() const;
	int64_t get_tower_armor() const;

	/** Get the common camera start location. */
	coord::phys3 get_centre() const;

	/** Get the number of playable players, excluding neutral player zero. */
	size_t get_player_count() const;

	/** Get the width and height of the square map in tiles. */
	size_t get_map_size() const;

	/** Capture active construction orders in stable order-ID order. */
	std::vector<FVDConstructionState> get_construction_state() const;

	/** Capture reserved footprint tiles in coordinate order. */
	std::vector<FVDTileReservation> get_reserved_tiles() const;

	/** Capture active Hunter orders in horse-ID order. */
	std::vector<FVDAttackOrderState> get_attack_order_state() const;

	/** The side whose vision a player shares: the Hunter, or all Builders. */
	static FVDTeam team_of(player_id_t player);

	/**
	 * Whether any of a side's units or completed towers sees the centre of
	 * this tile at this time. Out-of-map tiles are never visible.
	 */
	bool tile_visible(const std::shared_ptr<GameState> &state, FVDTeam team,
	                  const coord::tile &tile, const time::time_t &time) const;

	/**
	 * Whether a side may currently see, target or interact with an entity:
	 * its own side's entities always, others only on a visible tile.
	 */
	bool can_see(const std::shared_ptr<GameState> &state, FVDTeam team,
	             entity_id_t entity, const time::time_t &time) const;

	/** Recompute both sides' visible and explored tiles and remembered buildings. */
	void update_vision(const std::shared_ptr<GameState> &state, const time::time_t &time);

	/** One side's view as of the last update_vision. */
	FVDTeamVision get_vision(FVDTeam team) const;

	/** Capture at most 2048 immutable launches, oldest undelivered first.
	 * Passing no cursor retains only the recent presentation window. */
	std::vector<FVDArrowState> get_arrow_state(
		const time::time_t &now,
		std::optional<uint64_t> delivered_through = std::nullopt) const;

	/** Enable backlog retention during a live network match. */
	void enable_projectile_replication();

	/** Only discard old launches once every live recipient has been sent them. */
	void prune_delivered_projectiles(const time::time_t &now,
	                                 uint64_t delivered_to_all);

private:
	/** A queued tower retains a concrete, pickable foundation entity. */
	struct QueuedBuildSite {
		coord::tile site;
		entity_id_t foundation_id;
	};

	/** Shared construction path and movement setup for new/queued sites. */
	bool start_build(const std::shared_ptr<GameState> &state,
	                 player_id_t owner, const std::vector<entity_id_t> &selected,
	                 const coord::phys3 &position, const time::time_t &time,
	                 std::optional<entity_id_t> existing_foundation);

	/** Find a reachable vacant adjacent tile with public Pathfinder API. */
	std::optional<coord::phys3> find_build_stand(
		const std::shared_ptr<GameState> &state,
		const std::shared_ptr<GameEntity> &builder,
		const coord::tile &site, const time::time_t &time) const;

	/** Start the Builder's next reachable queued site, if any. */
	void start_next_queued_build(const std::shared_ptr<GameState> &state,
	                             player_id_t owner, entity_id_t builder,
	                             const time::time_t &time);

	/**
	 * Where a horse spawned at @p from should ride to reach @p target:
	 * the target itself if a path exists, otherwise the reachable tile
	 * closest to it. Nothing if the horse cannot move at all.
	 */
	std::optional<coord::phys3> closest_reachable(const std::shared_ptr<GameState> &state,
	                                              const coord::tile &from,
	                                              const coord::phys3 &target,
	                                              const time::time_t &time) const;

	/** Abandon queued orders and release their reservations, retaining foundations. */
	void clear_queued_builds(const std::shared_ptr<GameState> &state,
	                        entity_id_t builder, const time::time_t &time);

	struct AttackOrder {
		/** Travel destination; the engagement point of a stance engagement. */
		coord::phys3 destination;
		std::optional<entity_id_t> target;
		time::time_t next_attack;
		bool reached_destination = false;
		bool focus_target = false;
		std::optional<time::time_t> pending_hit{};
		/** Patrol's other endpoint, swapped with the destination on arrival. */
		std::optional<coord::phys3> patrol_return{};
		/** Where a Defensive patrol left its route to chase; it returns here. */
		std::optional<coord::phys3> patrol_engagement{};
		/** Started by an Aggressive or Defensive stance rather than a command. */
		bool stance_engagement = false;
	};

	struct PendingBuild {
		std::shared_ptr<component::command::BuildTowerCommand> order;
		coord::phys3 stand;
		time::time_t arrival;
		uint64_t route_revision;
		entity_id_t foundation_id;
		std::optional<entity_id_t> tower_id;
	};

	struct FootprintCell {
		coord::tile tile;
		nyan::fqon_t path_type;
		path::cost_t previous_cost;
	};

	struct TowerFootprint {
		std::vector<FootprintCell> cells;
		bool under_construction;
	};

	/**
	 * Validate a Hunter selection while ignoring horses confirmed dead by
	 * this simulation. Unknown IDs or foreign entities still invalidate it.
	 */
	std::optional<std::vector<entity_id_t>> resolve_command_units(
		const std::shared_ptr<GameState> &state, player_id_t owner,
		const std::vector<entity_id_t> &ids, const time::time_t &time) const;

	/**
	 * Vision radius from an entity type's LineOfSight ability in tiles,
	 * or zero for types that reveal nothing.
	 */
	double vision_range(const std::shared_ptr<GameState> &state,
	                    const std::string &definition) const;

	/** Living units and completed towers of a side, with their vision radius. */
	std::vector<std::pair<coord::phys3, double>> vision_sources(
		const std::shared_ptr<GameState> &state, FVDTeam team, const time::time_t &time) const;

	/** Shared validation and order setup for AttackMove and Patrol. */
	bool issue_hunter_route(const std::shared_ptr<GameState> &state,
	                        player_id_t owner, const std::vector<entity_id_t> &ids,
	                        const coord::phys3 &destination, bool patrol,
	                        const time::time_t &time);

	/**
	 * Nearest eligible completed enemy tower within the radius, otherwise the
	 * nearest eligible Builder, in deterministic ID order.
	 *
	 * @param anchor Only accept candidates within anchor_radius of this point.
	 */
	std::optional<entity_id_t> nearest_hunter_target(
		const std::shared_ptr<GameState> &state,
		const std::shared_ptr<GameEntity> &horse,
		const std::vector<entity_id_t> &towers,
		const std::vector<entity_id_t> &builders,
		double radius,
		const std::optional<coord::phys3> &anchor,
		double anchor_radius,
		const time::time_t &time) const;

	/** Whether a tower or Builder may currently be targeted by a Hunter horse. */
	bool hunter_target_eligible(const std::shared_ptr<GameState> &state,
	                            entity_id_t candidate, const time::time_t &time) const;

	/** Let an idle horse's stance start an engagement; return whether it did. */
	bool begin_stance_engagement(const std::shared_ptr<GameState> &state,
	                             const std::shared_ptr<GameEntity> &horse,
	                             const std::vector<entity_id_t> &towers,
	                             const std::vector<entity_id_t> &builders,
	                             const time::time_t &time);

	/** Apply damage once and remove dead units. */
	bool apply_damage(const std::shared_ptr<GameState> &state, entity_id_t target,
	                  int64_t damage, player_id_t owner, bool tower_shot,
	                  const time::time_t &time);

	/** Check occupancy, optionally excluding the constructing Builder. */
	bool tile_occupied(const std::shared_ptr<GameState> &state,
	                   const coord::tile &tile,
	                   const time::time_t &time,
	                   std::optional<entity_id_t> ignore = std::nullopt) const;

	/** Separate overlapping idle horses after a spawn without interrupting orders. */
	void relax_horse_crowd(const std::shared_ptr<GameState> &state,
	                      entity_id_t newest_horse,
	                      const time::time_t &time);

	/** Apply or restore a tower footprint in the map path grids. */
	void block_tower(const std::shared_ptr<GameState> &state,
	                 entity_id_t tower_id,
	                 const coord::tile &site,
	                 bool under_construction,
	                 const time::time_t &time);
	void unblock_tower(const std::shared_ptr<GameState> &state,
	                   entity_id_t tower_id,
	                   const time::time_t &time);
	/** Remove the nonblocking demolition remnant at a construction site. */
	void clear_tower_demolition(const std::shared_ptr<GameState> &state,
	                            const coord::tile &site,
	                            const time::time_t &time);

	size_t map_size;
	size_t chunk_size;
	size_t player_count;
	time::time_t construction_time;
	time::time_t tower_demolition_time;
	time::time_t horse_spawn_delay;
	time::time_t horse_spawn_interval;
	double horse_collision_radius;
	int64_t tower_damage;
	int64_t tower_armor;
	int64_t horse_damage;
	time::time_t arrow_travel;
	time::time_t tower_cooldown;
	time::time_t horse_cooldown;
	time::time_t horse_hit_delay;
	time::time_t horse_shove_duration;
	std::string hunter;
	std::string builders;
	std::string builder;
	std::string horse;
	std::string horse_attack_animation;
	std::string foundation;
	std::string tower_demolition;
	std::string tower;
	std::string hunter_tower;
	std::string hunter_rally_marker;
	int64_t hunter_tower_damage;
	double hunter_tower_range;
	time::time_t hunter_tower_cooldown;
	double builder_start_radius;
	/** Absent before the map exists or in scenarios without the tower. */
	std::optional<entity_id_t> hunter_tower_id{};
	/** Path costs under the Hunter tower before it blocked them. */
	std::vector<FootprintCell> hunter_tower_cells;
	/** Where new horses appear: the first free tile beside the Hunter tower. */
	coord::phys3 horse_spawn_point{0, 0, 0};
	time::time_t hunter_tower_next_shot{};
	/** When the Hunter tower may first fire: the first horse's spawn time. */
	std::optional<time::time_t> hunter_tower_active_from{};
	/** Where new horses ride to after spawning, if the Hunter set one. */
	std::optional<coord::phys3> hunter_rally_point{};
	/** The entity marking the rally point. */
	std::optional<entity_id_t> hunter_rally_marker_id{};
	std::string plains;
	double tower_radius_ne;
	double tower_radius_se;
	std::unordered_map<nyan::fqon_t, path::cost_t> tower_path_costs;

	std::weak_ptr<openage::event::EventLoop> event_loop;
	std::weak_ptr<EntityFactory> entity_factory;
	std::shared_ptr<openage::event::EventEntity> horse_spawn_target;
	std::shared_ptr<openage::event::Event> next_hunter_spawn_event;
	bool hunter_wave_started = false;
	/** Simulation time pauses during lobby setup, suspension and authority handoff. */
	std::optional<time::time_t> match_started_at{};
	std::optional<network::MatchOutcome> match_outcome{};
	/** Protect the match-relative first-spawn deadline read by the presenter. */
	mutable std::mutex hunter_spawn_time_mutex;
	std::optional<time::time_t> first_hunter_spawn_time;
	/** Visual event history only; all damage remains authoritative. */
	struct ArrowVisual {
		uint64_t arrow_id;
		player_id_t owner;
		entity_id_t target_id;
		coord::phys3 start;
		coord::phys3 destination;
		time::time_t launch_time;
		time::time_t impact_time;
	};
	mutable std::mutex arrow_visuals_mutex;
	std::vector<ArrowVisual> arrow_visuals;
	bool projectile_replication_enabled = false;
	uint64_t next_arrow_id = 1;
	uint64_t next_build_id = 1;
	std::unordered_map<entity_id_t, AttackOrder> attack_orders;
	/** Vision per side, indexed by FVDTeam value minus one. */
	struct TeamVisionState {
		std::vector<uint64_t> explored;
		std::vector<uint64_t> visible;
		std::map<entity_id_t, FVDRememberedBuilding> remembered;
	};
	std::array<TeamVisionState, 2> team_vision{};
	/** Vision radius per entity type, read once from its nyan data. */
	mutable std::unordered_map<std::string, double> vision_ranges;

	/** Set only during a combat tick, whose visibility queries reuse the bitsets. */
	std::optional<time::time_t> vision_cached_at{};

	/** Hunter-chosen stances; absent horses are Observe. */
	std::unordered_map<entity_id_t, HunterStance> hunter_stances;
	std::unordered_set<entity_id_t> dead_horses;
	std::unordered_map<entity_id_t, time::time_t> tower_next_shot;
	std::array<std::atomic<uint64_t>, 9> player_kills{};
	struct TowerKillCount {
		player_id_t owner;
		uint64_t kills;
	};
	/** Keep retired tower records so shots in flight can still score. */
	mutable std::mutex tower_kills_mutex;
	std::unordered_map<entity_id_t, TowerKillCount> tower_kills{};
	std::unordered_map<entity_id_t, PendingBuild> build_orders;
	std::unordered_map<entity_id_t, std::deque<QueuedBuildSite>> queued_builds;
	std::unordered_map<coord::tile, entity_id_t> reserved_tiles;
	std::unordered_map<entity_id_t, TowerFootprint> tower_footprints;
	std::unordered_map<coord::tile, entity_id_t> tower_demolitions;
};
}
