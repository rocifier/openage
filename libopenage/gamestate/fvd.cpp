// Copyright 2026 the openage authors. See copying.md for legal info.

#include "fvd.h"

#include <algorithm>
#include <array>
#include <map>
#include <utility>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <vector>

#include <nyan/nyan.h>

#include "curve/discrete.h"
#include "error/error.h"
#include "event/event.h"
#include "event/event_loop.h"
#include "event/eventhandler.h"
#include "gamestate/api/ability.h"
#include "gamestate/api/animation.h"
#include "gamestate/api/property.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/build_tower.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/activity/xor_event_gate.h"
#include "gamestate/entity_factory.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/system/idle.h"
#include "gamestate/system/move.h"
#include "gamestate/terrain_chunk.h"
#include "gamestate/terrain_factory.h"
#include "log/log.h"
#include "pathfinding/path.h"
#include "pathfinding/definitions.h"
#include "pathfinding/cost_field.h"
#include "pathfinding/grid.h"
#include "pathfinding/sector.h"
#include "pathfinding/pathfinder.h"
#include "util/math_constants.h"

namespace openage::gamestate {

int FVDConstructionState::progress_percent(const time::time_t &now) const {
	if (this->phase != FVDConstructionPhase::UNDER_CONSTRUCTION) {
		return 0;
	}
	const auto duration = this->phase_end_time - this->phase_start_time;
	if (duration <= time::TIME_ZERO) {
		return 100;
	}
	const auto elapsed = now - this->phase_start_time;
	const double ratio = elapsed.to_double() / duration.to_double();
	return std::clamp(static_cast<int>(std::clamp(ratio, 0.0, 1.0) * 100.0), 0, 100);
}

namespace {

/** Arrive at a construction site, then complete it at a later simulation time. */
class BuildProgressHandler : public openage::event::OnceEventHandler {
public:
	BuildProgressHandler() :
		OnceEventHandler{"game.fvd_build_progress"} {}

	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}

	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time,
	            const param_map &params) override {
		auto game_state = std::dynamic_pointer_cast<GameState>(state);
		game_state->get_fvd()->advance_build(
			game_state,
			params.get<entity_id_t>("builder"),
			params.get<uint64_t>("order"),
			params.get<uint64_t>("route"),
			params.get<bool>("finish"),
			time);
	}

	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override {
		return at;
	}
};

/** Complete an accepted garrison at its authoritative movement arrival time. */
class GarrisonArrivalHandler : public openage::event::OnceEventHandler {
public:
	GarrisonArrivalHandler() : OnceEventHandler{"game.fvd_garrison_arrival"} {}
	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}
	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time, const param_map &params) override {
		auto game = std::dynamic_pointer_cast<GameState>(state);
		game->get_fvd()->advance_garrison(game,
		                                 params.get<entity_id_t>("builder"),
		                                 params.get<entity_id_t>("tower"),
		                                 params.get<uint64_t>("revision"), time);
	}
	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override { return at; }
};

/** Remove a tower demolition after its configured lifetime. */
class TowerDemolitionExpiryHandler : public openage::event::OnceEventHandler {
public:
	TowerDemolitionExpiryHandler() :
		OnceEventHandler{"game.fvd_tower_demolition_expiry"} {}

	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}

	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time,
	            const param_map &params) override {
		auto game_state = std::dynamic_pointer_cast<GameState>(state);
		game_state->get_fvd()->expire_tower_demolition(
			game_state, params.get<entity_id_t>("demolition"), time);
	}

	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override {
		return at;
	}
};

/** Schedule Hunter-owned horses without assigning automatic movement. */
class SpawnHorseHandler : public openage::event::OnceEventHandler {
public:
	SpawnHorseHandler() :
		OnceEventHandler{"game.fvd_spawn_horse"} {}

	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}

	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time,
	            const param_map &params) override {
		auto game_state = std::dynamic_pointer_cast<GameState>(state);
		game_state->get_fvd()->spawn_hunter_horse(
			game_state, time, params.get<bool>("skip_initial_delay", false));
	}

	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override {
		return at;
	}
};

/** Fixed-time authoritative combat tick. */
class FVDCombatHandler : public openage::event::RepeatEventHandler {
public:
	FVDCombatHandler() : RepeatEventHandler{"game.fvd_combat_tick"} {}
	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}
	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time, const param_map &) override {
		auto gstate = std::dynamic_pointer_cast<GameState>(state);
		gstate->get_fvd()->combat_tick(gstate, time);
	}
	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override {
		return at + time::time_t::from_double(0.2);
	}
};

/** A launched arrow deals damage at its predicted impact time. */
/** A Hunter tower shot deals its damage when the projectile lands. */
class FVDHunterTowerHitHandler : public openage::event::OnceEventHandler {
public:
	FVDHunterTowerHitHandler() : OnceEventHandler{"game.fvd_hunter_tower_hit"} {}
	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}
	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time, const param_map &params) override {
		auto gstate = std::dynamic_pointer_cast<GameState>(state);
		gstate->get_fvd()->hunter_tower_hit(gstate, params.get<entity_id_t>("target"),
		                                  params.get<int64_t>("damage"), time);
	}
	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override {
		return at;
	}
};

class FVDArrowHitHandler : public openage::event::OnceEventHandler {
public:
	FVDArrowHitHandler() : OnceEventHandler{"game.fvd_arrow_hit"} {}
	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}
	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time, const param_map &params) override {
		auto gstate = std::dynamic_pointer_cast<GameState>(state);
		gstate->get_fvd()->arrow_hit(gstate, params.get<entity_id_t>("target"),
		                           params.get<player_id_t>("owner"),
		                           params.get<entity_id_t>("firing_tower"),
		                           params.get<int64_t>("damage"), time);
	}
	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override {
		return at;
	}
};

/** A Hunter horse deals damage when its attack animation reaches the hit frame. */
class FVDHorseHitHandler : public openage::event::OnceEventHandler {
public:
	FVDHorseHitHandler() : OnceEventHandler{"game.fvd_horse_hit"} {}
	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}
	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time, const param_map &params) override {
		auto gstate = std::dynamic_pointer_cast<GameState>(state);
		gstate->get_fvd()->horse_hit(gstate,
		                             params.get<entity_id_t>("horse"),
		                             params.get<entity_id_t>("target"),
		                             params.get<int64_t>("damage"), time);
	}
	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override {
		return at;
	}
};

/** Recalculate active routes at a low fixed frequency. */
class RefreshPathsHandler : public openage::event::RepeatEventHandler {
public:
	RefreshPathsHandler() :
		RepeatEventHandler{"game.fvd_refresh_paths"} {}

	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override {}

	void invoke(openage::event::EventLoop &,
	            const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time,
	            const param_map &) override {
		auto game_state = std::dynamic_pointer_cast<GameState>(state);
		game_state->get_fvd()->refresh_paths(game_state, time);
	}

	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &,
		const std::shared_ptr<openage::event::State> &,
		const time::time_t &at) override {
		return at + time::time_t::from_int(4);
	}
};

bool builder_alive(const std::shared_ptr<GameEntity> &entity,
                   const time::time_t &time) {
	if (not entity->has_component(component::component_t::LIVE)) {
		return false;
	}
	auto live = std::dynamic_pointer_cast<component::Live>(
		entity->get_component(component::component_t::LIVE));
	return live->get_attribute(time, "fvd.data.entities.Health")->get(time) > 0;
}

} // namespace

FVD::FVD(const std::shared_ptr<GameState> &state) {
	auto settings = state->get_db_view()->get_object("fvd.data.scenario.Scenario");
	auto size = settings.get_int("map_size");
	auto chunk = settings.get_int("chunk_size");
	auto players = settings.get_int("player_count");
	auto build_time = settings.get_float("construction_time");
	auto demolition_time = settings.get_int("tower_demolition_time");
	auto horse_delay = settings.get_int("horse_spawn_delay");
	auto horse_interval = settings.get_int("horse_spawn_interval");
	auto collision_radius = settings.get_float("horse_collision_radius");
	auto shove_duration = settings.get_float("horse_shove_duration");
	auto arrow_damage = settings.get_int("tower_arrow_damage");
	auto armor = settings.get_int("tower_armor");
	auto melee_damage = settings.get_int("horse_melee_damage");
	auto arrow_duration = settings.get_float("arrow_travel_time");
	auto central_damage = settings.get_int("hunter_tower_damage");
	auto central_range = settings.get_float("hunter_tower_range");
	auto central_cooldown = settings.get_float("hunter_tower_cooldown");
	auto start_radius = settings.get_float("builder_start_radius");
	if (central_damage < 1 or central_range < 1.0 or central_range > 32.0
	    or central_cooldown < 0.1 or central_cooldown > 60.0
	    or start_radius <= central_range or start_radius > size / 2.0 - 2.0) {
		throw Error{ERR << "Invalid FVD Hunter tower settings: damage >= 1, range 1..32 tiles, "
		                << "cooldown 0.1..60 seconds, Builders starting outside its range on the map"};
	}
	if (size < 16 or size > 512 or chunk < 4 or chunk > size
	    or chunk > MAX_CHUNK_WIDTH or chunk > MAX_CHUNK_HEIGHT
	    or size % chunk != 0 or players < 2 or players > 8
	    or build_time <= 0.0 or build_time > 300.0
	    or demolition_time < 1 or demolition_time > 300
	    or horse_delay < 0 or horse_delay > 600
	    or horse_interval < 1 or horse_interval > 60
	    or collision_radius < 0.1 or collision_radius > 0.49
	    or shove_duration <= 0.0 or shove_duration >= 0.9
	    or arrow_damage < 1 or arrow_damage > 100
	    or armor < 0 or armor > 50
	    or melee_damage < 1 or melee_damage > 100
	    or arrow_duration < 0.01 or arrow_duration > 3.0) {
		throw Error{ERR << "Invalid FVD settings: map 16..512, chunk 4.."
		                << MAX_CHUNK_WIDTH << ", players 2..8, construction >0..300 seconds"};
	}
	this->map_size = size;
	this->chunk_size = chunk;
	this->player_count = players;
	this->construction_time = time::time_t::from_double(build_time);
	this->tower_demolition_time = time::time_t::from_int(demolition_time);
	this->horse_spawn_delay = time::time_t::from_int(horse_delay);
	this->horse_spawn_interval = time::time_t::from_int(horse_interval);
	this->horse_collision_radius = collision_radius;
	this->horse_shove_duration = time::time_t::from_double(shove_duration);
	this->tower_damage = arrow_damage;
	this->tower_armor = armor;
	this->horse_damage = melee_damage;
	this->arrow_travel = time::time_t::from_double(arrow_duration);
	this->hunter_tower_damage = central_damage;
	this->hunter_tower_range = central_range;
	this->hunter_tower_cooldown = time::time_t::from_double(central_cooldown);
	this->builder_start_radius = start_radius;
	const size_t vision_words = (this->map_size * this->map_size + 63) / 64;
	for (auto &team : this->team_vision) {
		team.explored.assign(vision_words, 0);
		team.visible.assign(vision_words, 0);
	}
	this->tower_cooldown = time::time_t::from_double(0.8);
	this->horse_cooldown = time::time_t::from_double(0.6);
	this->horse_hit_delay = time::time_t::from_double(
		this->horse_cooldown.to_double() * 0.75);
	for (auto &kills : this->player_kills) {
		kills.store(0);
	}
	this->hunter = settings.get_object("hunter").get_name();
	this->builders = settings.get_object("builders").get_name();
	this->builder = settings.get_object("builder").get_name();
	this->horse = settings.get_object("horse").get_name();
	this->foundation = settings.get_object("foundation").get_name();
	this->tower_demolition = settings.get_object("tower_demolition").get_name();
	this->tower = settings.get_object("tower").get_name();
	this->hunter_tower = settings.get_object("hunter_tower").get_name();
	this->hunter_rally_marker = settings.get_object("hunter_rally_point").get_name();
	this->plains = settings.get_object("terrain").get_name();
	auto horse_attack = state->get_db_view()->get_object(this->horse + ".Attack");
	auto attack_property = api::APIAbility::get_property(
		horse_attack, api::ability_property_t::ANIMATED);
	auto attack_animations = api::APIAbilityProperty::get_animations(attack_property);
	auto attack_paths = api::APIAnimation::get_animation_paths(attack_animations);
	if (attack_paths.size() != 1) [[unlikely]] {
		throw Error{ERR << "FVD Hunter horse requires exactly one attack animation"};
	}
	this->horse_attack_animation = attack_paths.front();

	auto tower_object = state->get_db_view()->get_object(this->tower);
	bool found_pathable = false;
	for (const auto &ability : tower_object.get_set("GameEntity.abilities")) {
		auto value = std::dynamic_pointer_cast<nyan::ObjectValue>(ability.get_ptr());
		auto object = state->get_db_view()->get_object(value->get_name());
		if (object.get_parents()[0] != "engine.ability.type.Pathable") {
			continue;
		}
		auto hitbox = object.get_object("Pathable.hitbox");
		this->tower_radius_ne = hitbox.get_float("Hitbox.radius_x");
		this->tower_radius_se = hitbox.get_float("Hitbox.radius_y");
		for (const auto &[path_type, cost] : object.get_dict("Pathable.path_costs")) {
			auto path_value = std::dynamic_pointer_cast<nyan::ObjectValue>(path_type.get_ptr());
			auto cost_value = std::dynamic_pointer_cast<nyan::Int>(cost.get_ptr());
			if (cost_value->get() < path::COST_MIN or cost_value->get() > path::COST_IMPASSABLE) {
				throw Error{ERR << "Tower path cost is outside the valid range"};
			}
			this->tower_path_costs.emplace(
				path_value->get_name(), static_cast<path::cost_t>(cost_value->get()));
		}
		found_pathable = true;
		break;
	}
	if (not found_pathable or this->tower_radius_ne <= 0.0 or this->tower_radius_se <= 0.0
	    or this->tower_path_costs.empty()) {
		throw Error{ERR << "FVD tower requires a positive Pathable hitbox and path costs"};
	}
}

void FVD::initialise(const std::shared_ptr<GameState> &state,
                     const std::shared_ptr<openage::event::EventLoop> &loop,
                     const std::shared_ptr<EntityFactory> &entities,
                     const std::shared_ptr<TerrainFactory> &terrain) {
	this->event_loop = loop;
	this->entity_factory = entities;
	this->horse_spawn_target = std::make_shared<event::Spawner>(loop);
	loop->add_event_handler(std::make_shared<SpawnHorseHandler>());
	loop->add_event_handler(std::make_shared<FVDCombatHandler>());
	loop->add_event_handler(std::make_shared<FVDArrowHitHandler>());
	loop->add_event_handler(std::make_shared<FVDHorseHitHandler>());
	loop->add_event_handler(std::make_shared<FVDHunterTowerHitHandler>());
	loop->add_event_handler(std::make_shared<BuildProgressHandler>());
	loop->add_event_handler(std::make_shared<GarrisonArrivalHandler>());
	loop->add_event_handler(std::make_shared<TowerDemolitionExpiryHandler>());
	loop->add_event_handler(std::make_shared<RefreshPathsHandler>());

	std::vector<std::shared_ptr<TerrainChunk>> chunks;
	for (size_t se = 0; se < this->map_size; se += this->chunk_size) {
		for (size_t ne = 0; ne < this->map_size; ne += this->chunk_size) {
			chunks.push_back(terrain->add_uniform_chunk(
				state, {this->chunk_size, this->chunk_size},
				{static_cast<coord::tile_t>(ne), static_cast<coord::tile_t>(se)}, this->plains));
		}
	}
	state->set_map(std::make_shared<Map>(
		state, terrain->add_terrain({this->map_size, this->map_size}, std::move(chunks))));

	// Reserve zero for neutral ownership; playable players are numbered from one.
	std::shared_ptr<GameEntity> refresh_target;
	for (size_t id = 0; id <= this->player_count; ++id) {
		auto setup = id == 0 ? "" : (id == 1 ? this->hunter : this->builders);
		state->add_player(entities->add_player(loop, state, setup));
		if (id >= 2) {
			// Builders start evenly around a ring outside the Hunter tower's reach.
			const double angle = 2.0 * math::PI * static_cast<double>(id - 2)
			                     / static_cast<double>(this->player_count - 1);
			const auto centre = this->get_centre();
			const coord::tile site{
				static_cast<coord::tile_t>(std::floor(centre.ne.to_double()
				                                      + this->builder_start_radius * std::cos(angle))),
				static_cast<coord::tile_t>(std::floor(centre.se.to_double()
				                                      + this->builder_start_radius * std::sin(angle)))};
			auto entity = entities->spawn_game_entity(
				loop, state, id, this->builder, site.to_phys3_center(), time::TIME_ZERO);
			if (not refresh_target) {
				refresh_target = entity;
			}
		}
	}
	this->hunter_tower_id = entities->spawn_game_entity(
		loop, state, 1, this->hunter_tower, this->get_centre(), time::TIME_ZERO)->get_id();
	// The Hunter tower is solid like a Builder tower. Horses appear on the
	// first free tile beside it, on the side facing the camera.
	auto map = state->get_map();
	std::unordered_set<nyan::fqon_t> blocked_grids;
	coord::tile_t spawn_se = this->get_centre().to_tile().se;
	for (const auto &tile : this->get_tower_footprint(this->get_centre().to_tile())) {
		for (const auto &[path_type, cost] : this->tower_path_costs) {
			auto previous = map->set_path_cost(path_type, tile, cost, time::TIME_ZERO);
			this->hunter_tower_cells.push_back(FootprintCell{tile, path_type, previous});
			blocked_grids.insert(path_type);
		}
		spawn_se = std::max(spawn_se, tile.se);
	}
	for (const auto &path_type : blocked_grids) {
		map->rebuild_path_grid(path_type);
	}
	this->horse_spawn_point = coord::tile{this->get_centre().to_tile().ne, spawn_se + 1}.to_phys3_center();
	loop->create_event("game.fvd_refresh_paths", refresh_target->get_manager(), state, time::TIME_ZERO);
	// Players see their starting surroundings before the first combat tick.
	this->update_vision(state, time::TIME_ZERO);
}

bool FVD::tile_occupied(const std::shared_ptr<GameState> &state,
                        const coord::tile &tile,
                        const time::time_t &time,
                        std::optional<entity_id_t> ignore) const {
	for (const auto &[id, entity] : state->get_game_entities()) {
		if ((ignore.has_value() and id == *ignore) or this->is_garrisoned(id)) {
			continue;
		}
		if (entity->get_definition() == this->foundation
		    or entity->get_definition() == this->tower_demolition
		    or entity->get_definition() == this->hunter_rally_marker) {
			continue;
		}
		auto position = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		if (position->get_positions().get(time).to_tile() == tile) {
			return true;
		}
	}
	return false;
}

std::vector<coord::tile> FVD::get_tower_footprint(const coord::tile &site) const {
	auto ne_radius = static_cast<coord::tile_t>(std::ceil(this->tower_radius_ne - 0.5));
	auto se_radius = static_cast<coord::tile_t>(std::ceil(this->tower_radius_se - 0.5));
	std::vector<coord::tile> footprint;
	footprint.reserve((ne_radius * 2 + 1) * (se_radius * 2 + 1));
	for (auto se = site.se - se_radius; se <= site.se + se_radius; ++se) {
		for (auto ne = site.ne - ne_radius; ne <= site.ne + ne_radius; ++ne) {
			footprint.emplace_back(ne, se);
		}
	}
	return footprint;
}

void FVD::block_tower(const std::shared_ptr<GameState> &state,
                      entity_id_t tower_id,
                      const coord::tile &site,
                      bool under_construction,
                      const time::time_t &time) {
	if (this->tower_footprints.contains(tower_id)) [[unlikely]] {
		throw Error{ERR << "Tower " << tower_id << " already blocks a footprint"};
	}
	auto map = state->get_map();
	TowerFootprint blocked{{}, under_construction};
	std::unordered_set<nyan::fqon_t> changed_grids;
	for (const auto &tile : this->get_tower_footprint(site)) {
		for (const auto &[path_type, cost] : this->tower_path_costs) {
			auto previous = map->set_path_cost(path_type, tile, cost, time);
			blocked.cells.push_back(FootprintCell{tile, path_type, previous});
			changed_grids.insert(path_type);
		}
	}
	for (const auto &path_type : changed_grids) {
		map->rebuild_path_grid(path_type);
	}
	this->tower_footprints.emplace(tower_id, std::move(blocked));
}

void FVD::unblock_tower(const std::shared_ptr<GameState> &state,
                        entity_id_t tower_id,
                        const time::time_t &time) {
	auto found = this->tower_footprints.find(tower_id);
	if (found == this->tower_footprints.end()) {
		return;
	}
	auto map = state->get_map();
	std::unordered_set<nyan::fqon_t> changed_grids;
	for (const auto &cell : found->second.cells) {
		map->set_path_cost(cell.path_type, cell.tile, cell.previous_cost, time);
		changed_grids.insert(cell.path_type);
	}
	for (const auto &path_type : changed_grids) {
		map->rebuild_path_grid(path_type);
	}
	this->tower_footprints.erase(found);
}

bool FVD::can_place_tower(const std::shared_ptr<GameState> &state,
                          player_id_t owner,
                          const std::vector<entity_id_t> &selected,
                          const coord::phys3 &position,
                          const time::time_t &time,
                          std::optional<entity_id_t> existing_foundation) const {
	if (owner < 2 or owner > this->player_count) {
		return false;
	}
	auto civilisation = state->get_player(owner)->get_db_view()->get_object(
		state->get_player(owner)->get_setup());
	bool allowed = false;
	for (const auto &entry : civilisation.get_set("Civilisation.buildables")) {
		if (std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr())->get_name() == this->tower) {
			allowed = true;
		}
	}
	if (not allowed or selected.size() != 1) {
		return false;
	}
	auto builder_id = selected.front();
	auto found = state->get_game_entities().find(builder_id);
	if (found == state->get_game_entities().end()) {
		return false;
	}
	auto entity = found->second;
	if (entity->get_definition() != this->builder
	    or this->is_garrisoned(builder_id)
	    or not entity->has_component(component::component_t::OWNERSHIP)
	    or not entity->has_component(component::component_t::MOVE)
	    or not builder_alive(entity, time)) {
		return false;
	}
	auto ownership = std::dynamic_pointer_cast<component::Ownership>(
		entity->get_component(component::component_t::OWNERSHIP));
	if (ownership->get_owners().get(time) != owner) {
		return false;
	}
	if (position.ne < 0 or position.se < 0
	    or position.ne >= this->map_size or position.se >= this->map_size
	    or position.up != 0) {
		return false;
	}
	auto site = position.to_tile();
	for (const auto &tile : this->get_tower_footprint(site)) {
		if (tile.ne < 0 or tile.se < 0
		    or tile.ne >= this->map_size or tile.se >= this->map_size
		    or this->is_tile_reserved(tile) or this->tile_occupied(state, tile, time)) {
			return false;
		}
		for (const auto &[id, entity] : state->get_game_entities()) {
			if (id == existing_foundation or entity->get_definition() != this->foundation) {
				continue;
			}
			auto foundation_position = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION));
			if (foundation_position->get_positions().get(time).to_tile() == tile) {
				return false;
			}
		}
	}
	return true;
}


std::optional<coord::phys3> FVD::find_build_stand(
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<GameEntity> &entity,
	const coord::tile &site, const time::time_t &time) const {
	auto current_position = std::dynamic_pointer_cast<component::Position>(
		entity->get_component(component::component_t::POSITION))->get_positions().get(time);
	auto origin = current_position.to_tile();
	auto move = std::dynamic_pointer_cast<component::Move>(
		entity->get_component(component::component_t::MOVE));
	auto grid_name = move->get_ability().get<nyan::ObjectValue>("Move.path_type")->get_name();
	auto map = state->get_map();
	auto grid = map->get_grid_id(grid_name);
	auto pathfinder = map->get_pathfinder();

	const std::array<coord::tile_delta, 8> offsets{{
		{-1, 0}, {1, 0}, {0, -1}, {0, 1},
		{-1, -1}, {-1, 1}, {1, -1}, {1, 1},
	}};
	std::vector<coord::tile> candidates;
	for (const auto &offset : offsets) {
		coord::tile candidate{site.ne + offset.ne, site.se + offset.se};
		if (candidate.ne < 0 or candidate.se < 0
		    or candidate.ne >= this->map_size or candidate.se >= this->map_size
		    or this->is_tile_reserved(candidate)
		    or this->tile_occupied(state, candidate, time, entity->get_id())) {
			continue;
		}
		candidates.push_back(candidate);
	}
	std::stable_sort(candidates.begin(), candidates.end(),
	                 [&](const coord::tile &a, const coord::tile &b) {
		                 auto da = (a.ne - origin.ne) * (a.ne - origin.ne)
		                         + (a.se - origin.se) * (a.se - origin.se);
		                 auto db = (b.ne - origin.ne) * (b.ne - origin.ne)
		                         + (b.se - origin.se) * (b.se - origin.se);
		                 return da < db;
	                 });

	// Consider all eight faces and corners. The nearest tile by straight-line
	// distance is not always the shortest accessible route around other
	// towers. Choose by actual path length, with the stable candidate order
	// providing a deterministic tie-breaker.
	std::optional<coord::phys3> stand;
	std::optional<double> best_length;
	for (const auto &candidate : candidates) {
		if (candidate == origin) return current_position;
		auto route = pathfinder->get_path(path::PathRequest{grid, origin, candidate, time});
		if (route.status != path::PathResult::FOUND or route.waypoints.empty()) continue;
		double length = 0.0;
		for (size_t i = 1; i < route.waypoints.size(); ++i) {
			auto ne = route.waypoints[i].ne - route.waypoints[i - 1].ne;
			auto se = route.waypoints[i].se - route.waypoints[i - 1].se;
			length += std::hypot(static_cast<double>(ne), static_cast<double>(se));
		}
		if (not best_length or length < *best_length) {
			best_length = length;
			stand = candidate.to_phys3_center();
		}
	}
	return stand;
}

bool FVD::request_build(const std::shared_ptr<GameState> &state,
                        player_id_t owner,
                        const std::vector<entity_id_t> &selected,
                        const coord::phys3 &position,
                        const time::time_t &time) {
	return this->start_build(state, owner, selected, position, time, std::nullopt);
}

bool FVD::start_build(const std::shared_ptr<GameState> &state,
                      player_id_t owner,
                      const std::vector<entity_id_t> &selected,
                      const coord::phys3 &position,
                      const time::time_t &time,
                      std::optional<entity_id_t> existing_foundation) {
	if (not this->can_place_tower(state, owner, selected, position, time,
	                               existing_foundation)) {
		return false;
	}
	if (existing_foundation) {
		auto it = state->get_game_entities().find(*existing_foundation);
		if (it == state->get_game_entities().end()
		    or it->second->get_definition() != this->foundation) {
			return false;
		}
		auto foundation_position = std::dynamic_pointer_cast<component::Position>(
			it->second->get_component(component::component_t::POSITION));
		if (foundation_position->get_positions().get(time).to_tile() != position.to_tile()) {
			return false;
		}
	}
	auto builder_id = selected.front();
	auto entity = state->get_game_entity(builder_id);
	auto site = position.to_tile();

	auto current_position = std::dynamic_pointer_cast<component::Position>(
		entity->get_component(component::component_t::POSITION))->get_positions().get(time);
	auto stand = this->find_build_stand(state, entity, site, time);
	if (not stand.has_value()) {
		log::log(WARN << "No reachable adjacent construction tile");
		return false;
	}

	// A valid new construction command supersedes walking into a tower.
	this->cancel_pending_garrison(builder_id);
	// Old movement commands must not restart while the build order is active.
	this->cancel_build(state, builder_id, time);
	auto activity = std::dynamic_pointer_cast<component::Activity>(
		entity->get_component(component::component_t::ACTIVITY));
	auto commands = std::dynamic_pointer_cast<component::CommandQueue>(
		entity->get_component(component::component_t::COMMANDQUEUE));
	activity->cancel_events(time);
	while (not commands->get_queue().empty(time)) {
		commands->pop_command(time);
	}
	activity->init(time);
	entity->get_manager()->run_activity_system(time);

	auto loop = this->event_loop.lock();
	auto factory = this->entity_factory.lock();
	if (not loop or not factory) [[unlikely]] {
		throw Error{ERR << "FVD construction services no longer exist"};
	}
	this->clear_tower_demolition(state, site, time);

	auto order = std::make_shared<component::command::BuildTowerCommand>(
		this->next_build_id++, owner, builder_id, site);
	for (const auto &tile : this->get_tower_footprint(site)) {
		this->reserved_tiles.emplace(tile, builder_id);
	}
	auto travel = *stand == current_position
	            ? time::TIME_ZERO
	            : system::Move::move_default(entity, state, *stand, time);
	auto arrival = time + travel;
	entity_id_t foundation_id;
	if (existing_foundation) {
		// Already verified before any order was canceled or movement changed.
		foundation_id = *existing_foundation;
	}
	else {
		foundation_id = factory->spawn_game_entity(
			loop, state, owner, this->foundation,
			site.to_phys3_center(), time)->get_id();
	}
	this->build_orders.emplace(
		builder_id,
		PendingBuild{order, *stand, arrival, 0, foundation_id, std::nullopt});
	loop->create_event("game.fvd_build_progress", entity->get_manager(), state, arrival,
	                   {{"builder", builder_id}, {"order", order->get_id()},
	                    {"route", uint64_t{0}}, {"finish", false}});
	return true;
}

size_t FVD::get_queued_build_count(entity_id_t builder) const {
	auto it = this->queued_builds.find(builder);
	return it == this->queued_builds.end() ? 0 : it->second.size();
}

bool FVD::queue_build(const std::shared_ptr<GameState> &state,
                      player_id_t owner, const std::vector<entity_id_t> &selected,
                      const coord::phys3 &position, const time::time_t &time) {
	if (not this->can_place_tower(state, owner, selected, position, time)) {
		return false;
	}
	auto builder = selected.front();
	if (not this->build_orders.contains(builder)) {
		// Shift-clicking the first tower starts work immediately.
		return this->request_build(state, owner, selected, position, time);
	}
	constexpr size_t max_queued_towers = 256;
	auto &waiting = this->queued_builds[builder];
	if (waiting.size() >= max_queued_towers) {
		return false;
	}
	auto site = position.to_tile();
	auto loop = this->event_loop.lock();
	auto factory = this->entity_factory.lock();
	if (not loop or not factory) {
		return false;
	}
	this->clear_tower_demolition(state, site, time);
	// Render queued markers as actual pickable world entities immediately.
	auto foundation = factory->spawn_game_entity(
		loop, state, owner, this->foundation, site.to_phys3_center(), time);
	for (const auto &tile : this->get_tower_footprint(site)) {
		this->reserved_tiles.emplace(tile, builder);
	}
	waiting.push_back(QueuedBuildSite{site, foundation->get_id()});
	return true;
}

void FVD::clear_queued_builds(const std::shared_ptr<GameState> &state,
                              entity_id_t builder, const time::time_t &time) {
	auto pending = this->queued_builds.find(builder);
	if (pending == this->queued_builds.end()) {
		return;
	}
	for (const auto &site : pending->second) {
		for (const auto &tile : this->get_tower_footprint(site.site)) {
			auto reserved = this->reserved_tiles.find(tile);
			if (reserved != this->reserved_tiles.end() and reserved->second == builder) {
				this->reserved_tiles.erase(reserved);
			}
		}
	}
	this->queued_builds.erase(pending);
}

bool FVD::prioritize_foundation(const std::shared_ptr<GameState> &state,
                                player_id_t owner, const std::vector<entity_id_t> &selected,
                                entity_id_t foundation_id, const time::time_t &time) {
	if (owner < 2 or owner > this->player_count or selected.size() != 1) {
		return false;
	}
	auto builder_id = selected.front();
	auto builder_it = state->get_game_entities().find(builder_id);
	auto foundation_it = state->get_game_entities().find(foundation_id);
	if (builder_it == state->get_game_entities().end()
	    or foundation_it == state->get_game_entities().end()
	    or builder_it->second->get_definition() != this->builder
	    or foundation_it->second->get_definition() != this->foundation
	    or not builder_alive(builder_it->second, time)) {
		return false;
	}
	auto ownership = std::dynamic_pointer_cast<component::Ownership>(
		builder_it->second->get_component(component::component_t::OWNERSHIP));
	auto site_ownership = std::dynamic_pointer_cast<component::Ownership>(
		foundation_it->second->get_component(component::component_t::OWNERSHIP));
	if (not ownership or not site_ownership
	    or ownership->get_owners().get(time) != owner
	    or site_ownership->get_owners().get(time) != owner) {
		return false;
	}
	auto active = this->build_orders.find(builder_id);
	if (active != this->build_orders.end()
	    and active->second.foundation_id == foundation_id
	    and not active->second.tower_id) {
		// Already the active job: retain its progress and cancel future work.
		this->clear_queued_builds(state, builder_id, time);
		return true;
	}
	auto pending = this->queued_builds.find(builder_id);
	const auto site = std::dynamic_pointer_cast<component::Position>(
		foundation_it->second->get_component(component::component_t::POSITION))
		->get_positions().get(time).to_tile();
	const auto wanted = pending == this->queued_builds.end()
	                    ? std::deque<QueuedBuildSite>::iterator{}
	                    : std::find_if(pending->second.begin(), pending->second.end(),
	                                   [foundation_id](const QueuedBuildSite &entry) {
	                                   return entry.foundation_id == foundation_id;
	                                   });
	const bool queued = pending != this->queued_builds.end()
	                 and wanted != pending->second.end();
	if ((not queued and this->is_tile_reserved(site))
	    or not this->find_build_stand(state, builder_it->second, site, time)) {
		return false;
	}
	// Check eligibility before mutating the live queue.
	if (queued) {
		pending->second.erase(wanted);
		if (pending->second.empty()) {
			this->queued_builds.erase(pending);
		}
	}
	// Detach a queued site's reservation before replacing the current order.
	if (queued) {
		for (const auto &tile : this->get_tower_footprint(site)) {
			auto reserved = this->reserved_tiles.find(tile);
			if (reserved != this->reserved_tiles.end() and reserved->second == builder_id) {
				this->reserved_tiles.erase(reserved);
			}
		}
	}
	this->cancel_build(state, builder_id, time);
	return this->start_build(state, owner, selected, site.to_phys3_center(), time,
	                         foundation_id);
}

bool FVD::cancel_build(const std::shared_ptr<GameState> &state,
                       entity_id_t builder,
                       const time::time_t &time,
                       bool preserve_foundation) {
	auto it = this->build_orders.find(builder);
	if (it == this->build_orders.end()) {
		return false;
	}
	for (const auto &tile : this->get_tower_footprint(it->second.order->get_target())) {
		auto reserved = this->reserved_tiles.find(tile);
		if (reserved != this->reserved_tiles.end() and reserved->second == builder) {
			this->reserved_tiles.erase(reserved);
		}
	}
	// Incomplete towers revert to foundations, so interrupted work stays reusable.
	if (it->second.tower_id.has_value()) {
		this->destroy_tower(state, *it->second.tower_id, time);
		if (preserve_foundation) {
			auto loop = this->event_loop.lock();
			auto factory = this->entity_factory.lock();
			if (not loop or not factory) [[unlikely]] {
				throw Error{ERR << "FVD construction services no longer exist"};
			}
			const auto &site = it->second.order->get_target();
			this->clear_tower_demolition(state, site, time);
			factory->spawn_game_entity(
				loop, state, it->second.order->get_owner(), this->foundation,
				site.to_phys3_center(), time);
		}
	}
	this->build_orders.erase(it);
	// Abandon future work without deleting the visible, reusable foundations.
	this->clear_queued_builds(state, builder, time);
	auto found = state->get_game_entities().find(builder);
	if (found != state->get_game_entities().end()) {
		auto entity = found->second;
		auto position = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		auto current = position->get_positions().get(time);
		position->set_position(time, current);
		if (entity->has_component(component::component_t::IDLE)) {
			system::Idle::idle(entity, time);
		}
	}
	return true;
}

void FVD::advance_build(const std::shared_ptr<GameState> &state,
                        entity_id_t builder,
                        uint64_t order_id,
                        uint64_t route_revision,
                        bool finish,
                        const time::time_t &time) {
	if (this->match_outcome) return;
	auto it = this->build_orders.find(builder);
	if (it == this->build_orders.end()
	    or it->second.order->get_id() != order_id
	    or it->second.route_revision != route_revision) {
		return;
	}
	auto found = state->get_game_entities().find(builder);
	if (found == state->get_game_entities().end()) {
		this->cancel_build(state, builder, time);
		return;
	}
	auto entity = found->second;
	auto order = it->second.order;
	auto ownership = std::dynamic_pointer_cast<component::Ownership>(
		entity->get_component(component::component_t::OWNERSHIP));
	auto position = std::dynamic_pointer_cast<component::Position>(
		entity->get_component(component::component_t::POSITION));
	auto tile = order->get_target();
	auto current = position->get_positions().get(time);
	auto placed_tower = it->second.tower_id;
	if (not builder_alive(entity, time)
	    or ownership->get_owners().get(time) != order->get_owner()
	    or current != it->second.stand
	    or (not placed_tower.has_value()
	        and this->tile_occupied(state, current.to_tile(), time, builder))
	    or not this->is_tile_reserved(tile)
	    or this->reserved_tiles.at(tile) != builder
	    or (not placed_tower.has_value() and this->tile_occupied(state, tile, time))) {
		this->cancel_build(state, builder, time);
		return;
	}
	if (not finish) {
		auto loop = this->event_loop.lock();
		auto factory = this->entity_factory.lock();
		if (not loop or not factory) [[unlikely]] {
			throw Error{ERR << "FVD construction services no longer exist"};
		}
		if (not state->get_game_entities().contains(it->second.foundation_id)) [[unlikely]] {
			throw Error{ERR << "Pending construction has no foundation entity"};
		}
		state->remove_game_entity(it->second.foundation_id, time);
		auto tower_entity = factory->spawn_game_entity(
			loop, state, order->get_owner(), this->tower, tile.to_phys3_center(), time);
		if (not tower_entity->has_component(component::component_t::LIVE)) [[unlikely]] {
			throw Error{ERR << "FVD tower requires a Live ability"};
		}
		auto live = std::dynamic_pointer_cast<component::Live>(
			tower_entity->get_component(component::component_t::LIVE));
		auto health = nyan::fqon_t{"fvd.data.entities.Health"};
		int64_t max_health = 0;
		for (const auto &setting : live->get_ability().get_set("Live.attributes")) {
			auto value = std::dynamic_pointer_cast<nyan::ObjectValue>(setting.get_ptr());
			auto object = live->get_ability().get_view()->get_object(value->get_name());
			if (object.get_object("AttributeSetting.attribute").get_name() == health) {
				max_health = object.get_int("AttributeSetting.max_value");
				break;
			}
		}
		if (max_health <= 0) [[unlikely]] {
			throw Error{ERR << "FVD tower requires a positive maximum health"};
		}
		live->set_attribute_progress(time, time + this->construction_time,
		                             health, 0, max_health);
		system::Idle::idle(tower_entity, time);
		this->block_tower(state, tower_entity->get_id(), tile, true, time);
		{
			std::lock_guard lock{this->tower_kills_mutex};
			const bool inserted = this->tower_kills.emplace(
				tower_entity->get_id(), TowerKillCount{order->get_owner(), 0}).second;
			if (not inserted) [[unlikely]] {
				throw Error{ERR << "Duplicate FVD tower kill record"};
			}
		}
		it->second.tower_id = tower_entity->get_id();
		system::Idle::idle(entity, time);
		loop->create_event(
			"game.fvd_build_progress", entity->get_manager(), state,
			time + this->construction_time,
			{{"builder", builder}, {"order", order_id},
			 {"route", route_revision}, {"finish", true}});
		return;
	}

	if (not placed_tower.has_value() or not this->tower_footprints.contains(*placed_tower)) [[unlikely]] {
		throw Error{ERR << "Completed construction has no tower footprint"};
	}
	this->tower_footprints.at(*placed_tower).under_construction = false;
	for (const auto &footprint_tile : this->get_tower_footprint(tile)) {
		this->reserved_tiles.erase(footprint_tile);
	}
	this->build_orders.erase(it);
	system::Idle::idle(entity, time);
	this->start_next_queued_build(state, order->get_owner(), builder, time);
}

void FVD::start_next_queued_build(const std::shared_ptr<GameState> &state,
                                  player_id_t owner, entity_id_t builder,
                                  const time::time_t &time) {
	// The queued foundation already exists and retains its render identity
	// until this job begins constructing; skip unreachable abandoned sites.
	auto pending = this->queued_builds.find(builder);
	while (pending != this->queued_builds.end() and not pending->second.empty()) {
		auto next = pending->second.front();
		pending->second.pop_front();
		for (const auto &tile : this->get_tower_footprint(next.site)) {
			auto reserved = this->reserved_tiles.find(tile);
			if (reserved != this->reserved_tiles.end() and reserved->second == builder) {
				this->reserved_tiles.erase(reserved);
			}
		}
		if (pending->second.empty()) {
			this->queued_builds.erase(pending);
		}
		if (this->start_build(state, owner, {builder},
		                      next.site.to_phys3_center(), time, next.foundation_id)) {
			break;
		}
		// An unreachable queued site remains available to resume manually.
		pending = this->queued_builds.find(builder);
	}
}

void FVD::refresh_paths(const std::shared_ptr<GameState> &state,
                        const time::time_t &time) {
	if (this->match_outcome) return;
	std::vector<entity_id_t> destroyed;
	for (const auto &[tower_id, footprint] : this->tower_footprints) {
		if (footprint.under_construction) {
			continue;
		}
		auto found = state->get_game_entities().find(tower_id);
		if (found == state->get_game_entities().end()) {
			destroyed.push_back(tower_id);
			continue;
		}
		auto tower_entity = found->second;
		if (tower_entity->has_component(component::component_t::LIVE)) {
			auto live = std::dynamic_pointer_cast<component::Live>(
				tower_entity->get_component(component::component_t::LIVE));
			if (live->get_attribute(time, "fvd.data.entities.Health")->get(time) <= 0) {
				destroyed.push_back(tower_id);
			}
		}
	}
	for (auto tower_id : destroyed) {
		this->destroy_tower(state, tower_id, time);
	}

	auto loop = this->event_loop.lock();
	if (not loop) [[unlikely]] {
		throw Error{ERR << "FVD construction event loop no longer exists"};
	}
	for (auto &[builder_id, build] : this->build_orders) {
		if (build.tower_id.has_value()) {
			continue;
		}
		auto found = state->get_game_entities().find(builder_id);
		if (found == state->get_game_entities().end() or not builder_alive(found->second, time)) {
			continue;
		}
		auto entity = found->second;
		auto travel = system::Move::move_default(entity, state, build.stand, time);
		build.arrival = time + travel;
		build.route_revision += 1;
		loop->create_event(
			"game.fvd_build_progress", entity->get_manager(), state, build.arrival,
			{{"builder", builder_id}, {"order", build.order->get_id()},
			 {"route", build.route_revision}, {"finish", false}});
	}

	for (const auto &[entity_id, entity] : state->get_game_entities()) {
		if (this->build_orders.contains(entity_id)
		    or this->attack_orders.contains(entity_id)
		    or not entity->has_component(component::component_t::MOVE)) {
			continue;
		}
		auto move = std::dynamic_pointer_cast<component::Move>(
			entity->get_component(component::component_t::MOVE));
		if (not move->get_active_target().has_value()
		    or move->get_active_arrival() <= time) {
			continue;
		}
		auto activity_component = std::dynamic_pointer_cast<component::Activity>(
			entity->get_component(component::component_t::ACTIVITY));
		auto node = activity_component->get_node(time);
		if (node->get_type() != activity::node_t::XOR_EVENT_GATE) {
			continue;
		}
		auto target = *move->get_active_target();
		auto duration = system::Move::move_default(entity, state, target, time);
		activity_component->cancel_events(time);
		auto gate = std::static_pointer_cast<activity::XorEventGate>(node);
		for (const auto &[next_id, primer] : gate->get_primers()) {
			activity_component->add_event(primer(time + duration, entity, loop, state, next_id));
		}
	}
}

bool FVD::is_garrisoned(entity_id_t builder_id) const {
	return this->garrisoned_builders.contains(builder_id);
}

std::optional<entity_id_t> FVD::get_garrisoned_builder(entity_id_t tower_id) const {
	for (const auto &[builder, tower] : this->garrisoned_builders) {
		if (tower == tower_id) return builder;
	}
	return std::nullopt;
}

std::optional<coord::phys3> FVD::get_ungarrison_point(entity_id_t tower_id) const {
	auto found = this->ungarrison_points.find(tower_id);
	return found == this->ungarrison_points.end() ? std::nullopt
	                                              : std::make_optional(found->second.destination);
}

bool FVD::set_ungarrison_point(const std::shared_ptr<GameState> &state,
                               player_id_t owner, const std::vector<entity_id_t> &ids,
                               const coord::phys3 &point, const time::time_t &time,
                               std::optional<entity_id_t> target_tower) {
	if (this->match_outcome or owner < 2 or owner > this->player_count or ids.size() != 1
	    or (not target_tower and (point.ne < 0 or point.se < 0 or point.up != 0
	                              or point.ne >= this->map_size or point.se >= this->map_size))) return false;
	const auto tower_id = ids.front();
	auto tower = state->get_game_entities().find(tower_id);
	if (tower == state->get_game_entities().end()
	    or tower->second->get_definition() != this->tower
	    or not this->tower_footprints.contains(tower_id)
	    or this->tower_footprints.at(tower_id).under_construction) return false;
	auto ownership = std::dynamic_pointer_cast<component::Ownership>(
		tower->second->get_component(component::component_t::OWNERSHIP));
	if (not ownership or ownership->get_owners().get(time) != owner) return false;
	coord::phys3 destination = point;
	if (target_tower) {
		if (*target_tower == tower_id) return false;
		auto candidate = state->get_game_entities().find(*target_tower);
		if (candidate == state->get_game_entities().end()
		    or candidate->second->get_definition() != this->tower
		    or not this->tower_footprints.contains(*target_tower)
		    or this->tower_footprints.at(*target_tower).under_construction) return false;
		auto candidate_owner = std::dynamic_pointer_cast<component::Ownership>(
			candidate->second->get_component(component::component_t::OWNERSHIP));
		if (not candidate_owner or candidate_owner->get_owners().get(time) != owner) return false;
		auto target_pos = std::dynamic_pointer_cast<component::Position>(
			candidate->second->get_component(component::component_t::POSITION));
		destination = target_pos->get_positions().get(time);
		destination.up = coord::phys_t::from_int(0);
	}
	auto loop = this->event_loop.lock();
	auto factory = this->entity_factory.lock();
	if (not loop or not factory) [[unlikely]]
		throw Error{ERR << "Ungarrison marker services unavailable"};
	auto previous = this->ungarrison_points.find(tower_id);
	if (previous != this->ungarrison_points.end()) {
		if (state->get_game_entities().contains(previous->second.marker_id))
			state->remove_game_entity(previous->second.marker_id, time);
		this->ungarrison_points.erase(previous);
	}
	// The red rally marker is a world sprite. When its destination is a tower
	// tile, a ground-level sprite is depth-occluded by the tower, making the
	// endpoint look unset. Render it above the tower without changing the
	// actual (ground-level) ungarrison destination or hop target.
	auto marker_position = destination;
	if (target_tower or this->completed_tower_at(state, destination.to_tile())) {
		marker_position.up = coord::phys_t::from_double(3.0);
	}
	auto marker = factory->spawn_game_entity(loop, state, owner,
	                                        this->hunter_rally_marker, marker_position, time);
	system::Idle::idle(marker, time);
	this->ungarrison_points.emplace(
		tower_id, UngarrisonPoint{destination, marker->get_id(), target_tower});
	return true;
}

void FVD::cancel_pending_garrison(entity_id_t builder_id) {
	this->pending_garrisons.erase(builder_id);
}

bool FVD::request_garrison(const std::shared_ptr<GameState> &state,
                            player_id_t owner, const std::vector<entity_id_t> &ids,
                            entity_id_t tower_id, const time::time_t &time) {
	if (owner < 2 or owner > this->player_count or ids.size() != 1
	    or this->is_garrisoned(ids.front())
	    or this->pending_garrisons.contains(ids.front())) return false;
	const auto builder_id = ids.front();
	auto builder = state->get_game_entities().find(builder_id);
	auto tower = state->get_game_entities().find(tower_id);
	if (builder == state->get_game_entities().end()
	    or tower == state->get_game_entities().end()
	    or builder->second->get_definition() != this->builder
	    or tower->second->get_definition() != this->tower
	    or not builder_alive(builder->second, time)
	    or not this->tower_footprints.contains(tower_id)
	    or this->tower_footprints.at(tower_id).under_construction) return false;
	for (const auto &entity : {builder->second, tower->second}) {
		if (not entity->has_component(component::component_t::OWNERSHIP)) return false;
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (not ownership or ownership->get_owners().get(time) != owner) return false;
	}
	if (this->get_garrisoned_builder(tower_id)) return false;
	for (const auto &[pending_builder, pending] : this->pending_garrisons) {
		if (pending.tower_id == tower_id) return false;
	}
	auto tower_pos = std::dynamic_pointer_cast<component::Position>(
		tower->second->get_component(component::component_t::POSITION));
	auto position = std::dynamic_pointer_cast<component::Position>(
		builder->second->get_component(component::component_t::POSITION));
	const auto current = position->get_positions().get(time);
	const auto origin = current.to_tile();
	const auto site = tower_pos->get_positions().get(time).to_tile();
	// Any adjacent face or corner is an entrance; don't route the Builder
	// around the building to a particular side of its isometric sprite.
	const bool touching = std::abs(origin.ne - site.ne) <= 1
	                   and std::abs(origin.se - site.se) <= 1;
	auto stand = touching ? std::make_optional(current)
	                      : this->find_build_stand(state, builder->second, site, time);
	if (not stand) return false;

	// Reuse construction's cancellation semantics: keep visible, reusable foundations.
	this->cancel_build(state, builder_id, time);
	auto activity = std::dynamic_pointer_cast<component::Activity>(
		builder->second->get_component(component::component_t::ACTIVITY));
	auto queue = std::dynamic_pointer_cast<component::CommandQueue>(
		builder->second->get_component(component::component_t::COMMANDQUEUE));
	activity->cancel_events(time);
	while (not queue->get_queue().empty(time)) queue->pop_command(time);
	activity->init(time);
	builder->second->get_manager()->run_activity_system(time);
	const auto travel = current == *stand ? time::TIME_ZERO
	                                    : system::Move::move_default(builder->second, state, *stand, time);
	const auto revision = this->next_garrison_revision++;
	this->pending_garrisons.emplace(builder_id, PendingGarrison{tower_id, revision});
	auto loop = this->event_loop.lock();
	if (not loop) [[unlikely]] throw Error{ERR << "Garrison event loop unavailable"};
	loop->create_event("game.fvd_garrison_arrival", builder->second->get_manager(), state,
	                   time + travel,
	                   {{"builder", builder_id}, {"tower", tower_id}, {"revision", revision}});
	return true;
}

void FVD::advance_garrison(const std::shared_ptr<GameState> &state,
                            entity_id_t builder_id, entity_id_t tower_id,
                            uint64_t revision, const time::time_t &time) {
	auto pending = this->pending_garrisons.find(builder_id);
	if (pending == this->pending_garrisons.end()
	    or pending->second.tower_id != tower_id
	    or pending->second.revision != revision) return;
	this->pending_garrisons.erase(pending);
	auto builder = state->get_game_entities().find(builder_id);
	auto tower = state->get_game_entities().find(tower_id);
	if (builder == state->get_game_entities().end()
	    or tower == state->get_game_entities().end()
	    or not builder_alive(builder->second, time)
	    or not this->tower_footprints.contains(tower_id)
	    or this->tower_footprints.at(tower_id).under_construction) return;
	if (this->get_garrisoned_builder(tower_id)) return;
	auto builder_pos = std::dynamic_pointer_cast<component::Position>(
		builder->second->get_component(component::component_t::POSITION));
	auto tower_pos = std::dynamic_pointer_cast<component::Position>(
		tower->second->get_component(component::component_t::POSITION));
	const auto at = builder_pos->get_positions().get(time).to_tile();
	const auto site = tower_pos->get_positions().get(time).to_tile();
	if (std::abs(at.ne - site.ne) > 1 or std::abs(at.se - site.se) > 1) return;
	builder_pos->set_position(time, builder_pos->get_positions().get(time));
	system::Idle::idle(builder->second, time);
	this->garrisoned_builders.emplace(builder_id, tower_id);
	this->vision_cached_at.reset();
}

bool FVD::ungarrison_builder(const std::shared_ptr<GameState> &state,
                              player_id_t owner, const std::vector<entity_id_t> &ids,
                              const time::time_t &time) {
	if (owner < 2 or owner > this->player_count or ids.size() != 1) return false;
	return this->eject_builder(state, ids.front(), owner, time, true);
}

bool FVD::eject_builder(const std::shared_ptr<GameState> &state,
                        entity_id_t tower_id, player_id_t owner,
                        const time::time_t &time, bool use_point) {
	auto tower = state->get_game_entities().find(tower_id);
	if (tower == state->get_game_entities().end()
	    or tower->second->get_definition() != this->tower) return false;
	auto ownership = std::dynamic_pointer_cast<component::Ownership>(
		tower->second->get_component(component::component_t::OWNERSHIP));
	if (not ownership or ownership->get_owners().get(time) != owner) return false;
	auto occupant_id = this->get_garrisoned_builder(tower_id);
	if (not occupant_id) return false;
	auto builder = state->get_game_entities().find(*occupant_id);
	if (builder == state->get_game_entities().end()) return false;
	auto tower_pos = std::dynamic_pointer_cast<component::Position>(
		tower->second->get_component(component::component_t::POSITION));
	const auto site = tower_pos->get_positions().get(time).to_tile();
	const auto point = use_point ? this->get_ungarrison_point(tower_id)
	                             : std::optional<coord::phys3>{};
	auto configured = this->ungarrison_points.find(tower_id);
	const auto explicit_tower = use_point and configured != this->ungarrison_points.end()
	                          ? configured->second.target_tower : std::nullopt;
	// Only an explicitly targeted tower can trigger a transfer. An ordinary
	// ground point never silently becomes a garrison command. If the intended
	// tower was destroyed or became occupied, keep the Builder sheltered.
	if (explicit_tower) {
		auto destination_tower = state->get_game_entities().find(*explicit_tower);
		if (destination_tower == state->get_game_entities().end()
		    or destination_tower->second->get_definition() != this->tower
		    or not this->tower_footprints.contains(*explicit_tower)
		    or this->tower_footprints.at(*explicit_tower).under_construction
		    or this->get_garrisoned_builder(*explicit_tower)) return false;
		auto dest_owner = std::dynamic_pointer_cast<component::Ownership>(
			destination_tower->second->get_component(component::component_t::OWNERSHIP));
		if (not dest_owner or dest_owner->get_owners().get(time) != owner) return false;
		auto hop_tower = explicit_tower;
		if (hop_tower and *hop_tower != tower_id
		    and this->tower_footprints.contains(*hop_tower)
		    and not this->tower_footprints.at(*hop_tower).under_construction
		    and not this->get_garrisoned_builder(*hop_tower)) {
			auto candidate = state->get_game_entities().find(*hop_tower);
			if (candidate != state->get_game_entities().end()) {
				auto other_owner = std::dynamic_pointer_cast<component::Ownership>(
					candidate->second->get_component(component::component_t::OWNERSHIP));
				bool touching = false;
				const auto &from = this->tower_footprints.at(tower_id).cells;
				const auto &to = this->tower_footprints.at(*hop_tower).cells;
				for (const auto &left : from) for (const auto &right : to) {
					if (std::abs(left.tile.ne - right.tile.ne) <= 1
					    and std::abs(left.tile.se - right.tile.se) <= 1) touching = true;
				}
				if (touching and other_owner and other_owner->get_owners().get(time) == owner) {
					this->garrisoned_builders.insert_or_assign(*occupant_id, *hop_tower);
					return true;
				}
			}
		}
	}
	const auto grid_id = state->get_map()->get_grid_id("fvd.data.terrain.Land");
	const auto pathfinder = state->get_map()->get_pathfinder();
	const auto grid = pathfinder->get_grid(grid_id);
	const auto sector_size = grid->get_sector_size();
	// Scan adjacent tiles first; if a wall fully encloses the tower, scan
	// outwards so combat demolition never makes an occupied tower immortal.
	std::optional<coord::phys3> destination;
	for (coord::tile_t radius = 1;
	     radius < static_cast<coord::tile_t>(this->map_size) and not destination; ++radius) {
		for (coord::tile_t ne = site.ne - radius;
		     ne <= site.ne + radius and not destination; ++ne) {
			for (coord::tile_t se = site.se - radius; se <= site.se + radius; ++se) {
				if (ne < 0 or se < 0 or ne >= static_cast<coord::tile_t>(this->map_size)
				    or se >= static_cast<coord::tile_t>(this->map_size)) continue;
				if (std::max(std::abs(ne - site.ne), std::abs(se - site.se)) != radius) continue;
				const coord::tile candidate{ne, se};
				if (this->is_tile_reserved(candidate) or this->tile_occupied(state, candidate, time)) continue;
				auto sector = grid->get_sector(candidate.ne / sector_size,
				                              candidate.se / sector_size);
				auto local = candidate - sector->get_position().to_tile(sector_size);
				if (sector->get_cost_field()->get_cost(local) >= path::COST_IMPASSABLE) continue;
				destination = candidate.to_phys3_center();
				break;
			}
		}
	}
	// After destruction the centre footprint becomes free. It is a safe
	// last-resort exit when a fully enclosed wall has no exterior tile.
	if (not destination and not use_point) destination = site.to_phys3_center();
	if (not destination) return false;
	this->garrisoned_builders.erase(*occupant_id);
	auto position = std::dynamic_pointer_cast<component::Position>(
		builder->second->get_component(component::component_t::POSITION));
	position->set_position(time, *destination);
	system::Idle::idle(builder->second, time);
	this->vision_cached_at.reset();
	if (explicit_tower) {
		// A distant tower transfers via its nearest reachable perimeter.
		// The authoritative request handles the travel and arrival event.
		// If the target is now blocked/destroyed, the Builder remains safely
		// outside instead of walking toward an impassable tower centre.
		this->request_garrison(state, owner, {*occupant_id}, *explicit_tower, time);
		return true;
	}
	if (point) {
		if (auto reachable = this->closest_reachable(state, destination->to_tile(), *point, time)) {
			if (*reachable != *destination)
				system::Move::move_default(builder->second, state, *reachable, time);
		}
	}
	return true;
}

bool FVD::demolish_owned_tower(const std::shared_ptr<GameState> &state,
                               player_id_t owner, const std::vector<entity_id_t> &ids,
                               const time::time_t &time) {
	if (owner < 2 or owner > this->player_count or ids.size() != 1) {
		return false;
	}
	const auto tower_id = ids.front();
	auto found = state->get_game_entities().find(tower_id);
	// Delete on a foundation removes that planned site instead.
	if (found != state->get_game_entities().end()
	    and found->second->get_definition() == this->foundation) {
		return this->delete_owned_foundation(state, owner, tower_id, time);
	}
	if (found == state->get_game_entities().end()
	    or found->second->get_definition() != this->tower
	    or not found->second->has_component(component::component_t::OWNERSHIP)
	    or not this->tower_footprints.contains(tower_id)) {
		return false;
	}
	auto ownership = std::dynamic_pointer_cast<component::Ownership>(
		found->second->get_component(component::component_t::OWNERSHIP));
	if (not ownership or ownership->get_owners().get(time) != owner) {
		return false;
	}

	// An unfinished tower still belongs to its Builder's current order.
	// Cancel that order, release reservations and delete the tower without
	// leaving the reusable foundation that an ordinary cancellation creates.
	if (this->tower_footprints.at(tower_id).under_construction) {
		for (const auto &[builder_id, build] : this->build_orders) {
			if (build.tower_id == tower_id) {
				return this->cancel_build(state, builder_id, time, false);
			}
		}
		return false;
	}
	return this->destroy_tower(state, tower_id, time);
}

bool FVD::delete_owned_foundation(const std::shared_ptr<GameState> &state,
                                  player_id_t owner, entity_id_t foundation_id,
                                  const time::time_t &time) {
	auto found = state->get_game_entities().find(foundation_id);
	if (owner < 2 or owner > this->player_count
	    or found == state->get_game_entities().end()
	    or found->second->get_definition() != this->foundation
	    or not found->second->has_component(component::component_t::OWNERSHIP)) {
		return false;
	}
	auto ownership = std::dynamic_pointer_cast<component::Ownership>(
		found->second->get_component(component::component_t::OWNERSHIP));
	if (not ownership or ownership->get_owners().get(time) != owner) {
		return false;
	}

	// A site waiting in a Builder's queue leaves the queue and frees its tiles.
	for (auto pending = this->queued_builds.begin(); pending != this->queued_builds.end(); ++pending) {
		auto &sites = pending->second;
		auto wanted = std::find_if(sites.begin(), sites.end(), [&](const QueuedBuildSite &entry) {
			return entry.foundation_id == foundation_id;
		});
		if (wanted == sites.end()) {
			continue;
		}
		for (const auto &tile : this->get_tower_footprint(wanted->site)) {
			auto reserved = this->reserved_tiles.find(tile);
			if (reserved != this->reserved_tiles.end() and reserved->second == pending->first) {
				this->reserved_tiles.erase(reserved);
			}
		}
		sites.erase(wanted);
		if (sites.empty()) {
			this->queued_builds.erase(pending);
		}
		break;
	}

	// The site a Builder is walking to: cancel that job, then carry on with
	// the rest of the queue rather than abandoning it.
	std::optional<entity_id_t> walking_builder{};
	for (const auto &[builder_id, build] : this->build_orders) {
		if (build.foundation_id == foundation_id and not build.tower_id) {
			walking_builder = builder_id;
			break;
		}
	}
	if (walking_builder) {
		std::deque<QueuedBuildSite> rest{};
		if (auto pending = this->queued_builds.find(*walking_builder); pending != this->queued_builds.end()) {
			rest = std::move(pending->second);
			this->queued_builds.erase(pending);
		}
		this->cancel_build(state, *walking_builder, time, false);
		if (not rest.empty()) {
			this->queued_builds.emplace(*walking_builder, std::move(rest));
		}
	}

	state->remove_game_entity(foundation_id, time);
	if (walking_builder) {
		this->start_next_queued_build(state, owner, *walking_builder, time);
	}
	return true;
}

bool FVD::destroy_tower(const std::shared_ptr<GameState> &state,
                        entity_id_t tower_id,
                        const time::time_t &time) {
	if (not this->tower_footprints.contains(tower_id)) {
		return false;
	}
	std::optional<coord::phys3> position;
	player_id_t owner = 0;
	auto tower = state->get_game_entities().find(tower_id);
	if (tower != state->get_game_entities().end()) {
		auto position_component = std::dynamic_pointer_cast<component::Position>(
			tower->second->get_component(component::component_t::POSITION));
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			tower->second->get_component(component::component_t::OWNERSHIP));
		position = position_component->get_positions().get(time);
		owner = ownership->get_owners().get(time);
	}
	// Builder survives tower demolition; never remove the tower while occupied.
	if (auto occupied = std::find_if(
	        this->garrisoned_builders.begin(), this->garrisoned_builders.end(),
	        [tower_id](const auto &entry) { return entry.second == tower_id; });
	    occupied != this->garrisoned_builders.end()
	    and not this->eject_builder(state, tower_id, owner, time, false)) return false;
	std::erase_if(this->pending_garrisons,
	              [tower_id](const auto &entry) { return entry.second.tower_id == tower_id; });
	auto point = this->ungarrison_points.find(tower_id);
	if (point != this->ungarrison_points.end()) {
		if (state->get_game_entities().contains(point->second.marker_id))
			state->remove_game_entity(point->second.marker_id, time);
		this->ungarrison_points.erase(point);
	}
	this->tower_next_shot.erase(tower_id);
	this->unblock_tower(state, tower_id, time);
	if (state->get_game_entities().contains(tower_id)) {
		state->remove_game_entity(tower_id, time);
	}
	if (position) {
		auto loop = this->event_loop.lock();
		auto factory = this->entity_factory.lock();
		if (not loop or not factory) [[unlikely]] {
			throw Error{ERR << "FVD demolition services no longer exist"};
		}
		const auto site = position->to_tile();
		this->clear_tower_demolition(state, site, time);
		auto demolition = factory->spawn_game_entity(
			loop, state, owner, this->tower_demolition, *position, time);
		system::Idle::idle(demolition, time);
		const auto inserted = this->tower_demolitions.emplace(site, demolition->get_id()).second;
		if (not inserted) [[unlikely]] {
			throw Error{ERR << "FVD tower demolition site was not cleared"};
		}
		loop->create_event(
			"game.fvd_tower_demolition_expiry", demolition->get_manager(), state,
			time + this->tower_demolition_time,
			{{"demolition", demolition->get_id()}});
	}
	return true;
}

void FVD::expire_tower_demolition(const std::shared_ptr<GameState> &state,
                                  entity_id_t demolition_id,
                                  const time::time_t &time) {
	auto found = std::find_if(
		this->tower_demolitions.begin(), this->tower_demolitions.end(),
		[demolition_id](const auto &entry) { return entry.second == demolition_id; });
	if (found == this->tower_demolitions.end()) {
		return;
	}
	this->clear_tower_demolition(state, found->first, time);
}

void FVD::clear_tower_demolition(const std::shared_ptr<GameState> &state,
                                 const coord::tile &site,
                                 const time::time_t &time) {
	auto found = this->tower_demolitions.find(site);
	if (found == this->tower_demolitions.end()) {
		return;
	}
	auto entity = state->get_game_entities().find(found->second);
	if (entity == state->get_game_entities().end()
	    or entity->second->get_definition() != this->tower_demolition) [[unlikely]] {
		throw Error{ERR << "FVD tower demolition state is inconsistent"};
	}
	state->remove_game_entity(found->second, time);
	this->tower_demolitions.erase(found);
}

std::shared_ptr<component::command::BuildTowerCommand> FVD::get_build_order(
	entity_id_t builder) const {
	auto it = this->build_orders.find(builder);
	return it == this->build_orders.end() ? nullptr : it->second.order;
}

bool FVD::is_tile_reserved(const coord::tile &tile) const {
	return this->reserved_tiles.contains(tile);
}

void FVD::relax_horse_crowd(const std::shared_ptr<GameState> &state,
                            entity_id_t newest_horse,
                            const time::time_t &time) {
	// A kinematic, 2D circle-contact solver. This runs on the simulation
	// thread at spawn time, not on the renderer or an independent timer.
	// It deliberately does not replace a human player's active Move order.
	struct Body {
		entity_id_t id;
		std::shared_ptr<GameEntity> entity;
		coord::phys3 original;
		double ne;
		double se;
		double radius;
		double mobility;
	};
	std::vector<Body> bodies;
	bodies.reserve(state->get_game_entities().size());
	for (const auto &[id, entity] : state->get_game_entities()) {
		const auto &definition = entity->get_definition();
		if (definition != this->horse and definition != this->builder) {
			continue;
		}
		if (not builder_alive(entity, time) or this->is_garrisoned(id)) {
			continue;
		}
		auto position = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		auto current = position->get_positions().get(time);
		double mobility = 0.0;
		if (definition == this->horse) {
			auto move = std::dynamic_pointer_cast<component::Move>(
				entity->get_component(component::component_t::MOVE));
			if (not move->get_active_target().has_value()
			    or move->get_active_arrival() <= time) {
				// Give older horses most of the shove; a new arrival stays
				// near the actual spawn point, pushing the pack outwards.
				mobility = id == newest_horse ? 0.15 : 1.0;
			}
		}
		bodies.push_back(Body{id, entity, current,
		                     current.ne.to_double(), current.se.to_double(),
		                     definition == this->horse ? this->horse_collision_radius : 0.28,
		                     mobility});
	}
	if (bodies.empty()) {
		return;
	}
	std::sort(bodies.begin(), bodies.end(),
	          [](const Body &a, const Body &b) { return a.id < b.id; });

	// Use the same land cost field as construction and normal pathfinding.
	// Sample the four disk extremities so a shove cannot pass through a
	// blocked tower footprint or a reserved construction tile.
	auto grid = state->get_map()->get_pathfinder()->get_grid(
		state->get_map()->get_grid_id("fvd.data.terrain.Land"));
	const auto sector_size = grid->get_sector_size();
	auto walkable = [&](double ne, double se, double radius) {
		const std::array<std::pair<double, double>, 5> probes{{
			{ne, se}, {ne - radius, se - radius},
			{ne - radius, se + radius}, {ne + radius, se - radius},
			{ne + radius, se + radius},
		}};
		for (const auto &[x, y] : probes) {
			if (x < 0.0 or y < 0.0
			    or x >= static_cast<double>(this->map_size)
			    or y >= static_cast<double>(this->map_size)) {
				return false;
			}
			auto tile = coord::tile{
				static_cast<coord::tile_t>(x),
				static_cast<coord::tile_t>(y)};
			if (this->is_tile_reserved(tile)) {
				return false;
			}
			auto sector = grid->get_sector(tile.ne / sector_size, tile.se / sector_size);
			auto local = tile - sector->get_position().to_tile(sector_size);
			if (sector->get_cost_field()->get_cost(local) >= path::COST_IMPASSABLE) {
				return false;
			}
		}
		return true;
	};

	// If the centre is blocked by a tower, still spawn the horse, then
	// ease it to the nearest usable contact position. Never reject spawning.
	for (auto &body : bodies) {
		if (body.id != newest_horse or walkable(body.ne, body.se, body.radius)) {
			continue;
		}
		constexpr std::array<std::pair<double, double>, 8> directions{{
			{1, 0}, {0.7071067811865476, 0.7071067811865476},
			{0, 1}, {-0.7071067811865476, 0.7071067811865476},
			{-1, 0}, {-0.7071067811865476, -0.7071067811865476},
			{0, -1}, {0.7071067811865476, -0.7071067811865476},
		}};
		bool found = false;
		for (int ring = 1; ring <= 24 and not found; ++ring) {
			for (const auto &[dx, dy] : directions) {
				double x = body.ne + dx * ring * 0.25;
				double y = body.se + dy * ring * 0.25;
				if (walkable(x, y, body.radius)) {
					body.ne = x;
					body.se = y;
					found = true;
					break;
				}
			}
		}
	}

	// Stable IDs, stable cell order and fixed pair directions avoid using
	// unordered container iteration order or random forces in gameplay.
	constexpr std::array<std::pair<double, double>, 8> directions{{
		{1, 0}, {0.7071067811865476, 0.7071067811865476},
		{0, 1}, {-0.7071067811865476, 0.7071067811865476},
		{-1, 0}, {-0.7071067811865476, -0.7071067811865476},
		{0, -1}, {0.7071067811865476, -0.7071067811865476},
	}};
	for (int pass = 0; pass < 24; ++pass) {
		std::map<std::pair<int, int>, std::vector<size_t>> cells;
		for (size_t i = 0; i < bodies.size(); ++i) {
			cells[{static_cast<int>(std::floor(bodies[i].ne)),
			       static_cast<int>(std::floor(bodies[i].se))}].push_back(i);
		}
		bool changed = false;
		for (size_t i = 0; i < bodies.size(); ++i) {
			auto &a = bodies[i];
			auto cx = static_cast<int>(std::floor(a.ne));
			auto cy = static_cast<int>(std::floor(a.se));
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					auto it = cells.find({cx + dx, cy + dy});
					if (it == cells.end()) {
						continue;
					}
					for (auto j : it->second) {
						if (j <= i) {
							continue;
						}
						auto &b = bodies[j];
						double total_mobility = a.mobility + b.mobility;
						if (total_mobility == 0.0) {
							continue;
						}
						double x = b.ne - a.ne;
						double y = b.se - a.se;
						double min_distance = a.radius + b.radius;
						double distance_squared = x * x + y * y;
						if (distance_squared >= min_distance * min_distance) {
							continue;
						}
						double distance = std::sqrt(distance_squared);
						if (distance < 0.00001) {
							auto dir = directions[(i * 17 + j * 31) % directions.size()];
							x = dir.first;
							y = dir.second;
							distance = 1.0;
							// Fully overlapped circles require a full radius
							// displacement, rather than a divide by zero.
							distance_squared = 0.0;
						}
						double overlap = min_distance - std::sqrt(distance_squared) + 0.002;
						double step_x = x / distance * overlap;
						double step_y = y / distance * overlap;
						if (a.mobility > 0.0) {
							double nx = a.ne - step_x * a.mobility / total_mobility;
							double ny = a.se - step_y * a.mobility / total_mobility;
							if (walkable(nx, ny, a.radius)) {
								a.ne = nx;
								a.se = ny;
								changed = true;
							}
						}
						if (b.mobility > 0.0) {
							double nx = b.ne + step_x * b.mobility / total_mobility;
							double ny = b.se + step_y * b.mobility / total_mobility;
							if (walkable(nx, ny, b.radius)) {
								b.ne = nx;
								b.se = ny;
								changed = true;
							}
						}
					}
				}
			}
		}
		if (not changed) {
			break;
		}
	}

	for (auto &body : bodies) {
		if (body.mobility == 0.0) {
			continue;
		}
		auto target = coord::phys3{
			coord::phys_t::from_double(body.ne),
			coord::phys_t::from_double(body.se),
			body.original.up};
		if (target == body.original) {
			continue;
		}
		auto position = std::dynamic_pointer_cast<component::Position>(
			body.entity->get_component(component::component_t::POSITION));
		position->set_position(time, body.original);
		position->set_position(time + this->horse_shove_duration, target);
		system::Idle::idle(body.entity, time);
	}
}

void FVD::start_hunter_wave(const std::shared_ptr<GameState> &state,
                            const time::time_t &match_start) {
	if (this->hunter_wave_started) {
		return;
	}
	auto loop = this->event_loop.lock();
	if (not loop or not this->horse_spawn_target) {
		throw Error{ERR << "Cannot start the Hunter wave before FVD initialization"};
	}
	auto spawn_time = match_start + this->horse_spawn_delay;
	{
		std::lock_guard lock{this->hunter_spawn_time_mutex};
		this->first_hunter_spawn_time = spawn_time;
	}
	this->hunter_tower_active_from = spawn_time;
	this->hunter_wave_started = true;
	this->match_started_at = match_start;
	this->next_hunter_spawn_event = loop->create_event(
		"game.fvd_spawn_horse", this->horse_spawn_target, state, spawn_time);
	loop->create_event("game.fvd_combat_tick", this->horse_spawn_target, state, match_start);
}

std::optional<network::MatchOutcome> decide_fvd_match_outcome(
	bool any_builder_alive, const time::time_t &now,
	const std::optional<time::time_t> &deadline) {
	if (not any_builder_alive) {
		return network::MatchOutcome{network::MatchWinner::HUNTER,
		                             network::MatchEndReason::ALL_BUILDERS_DEAD, now};
	}
	if (deadline and now >= *deadline) {
		return network::MatchOutcome{network::MatchWinner::BUILDERS,
		                             network::MatchEndReason::TIME_LIMIT, now};
	}
	return std::nullopt;
}

std::optional<network::MatchOutcome> FVD::get_match_outcome() const {
	return this->match_outcome;
}

std::optional<time::time_t> FVD::get_match_end_deadline() const {
	if (not this->match_started_at) return std::nullopt;
	return *this->match_started_at
	     + time::time_t::from_int(FVD_MATCH_DURATION_SECONDS);
}

void FVD::evaluate_match_outcome(
	const std::shared_ptr<GameState> &state,
	const time::time_t &now,
	const std::vector<player_id_t> &disconnected) {
	if (not this->match_started_at or this->match_outcome) return;
	// Count only actually admitted Builders. Unused slots have no initial
	// Builder entity after the lobby's pre-bootstrap pruning.
	std::vector<bool> alive(this->player_count + 1, false);
	for (const auto &[id, entity] : state->get_game_entities()) {
		(void) id;
		const auto &definition = entity->get_definition();
		if (definition != this->builder) continue;
		if (not builder_alive(entity, now)) continue;
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (not ownership) continue;
		const auto owner = ownership->get_owners().get(now);
		if (owner < 2 or owner > this->player_count) continue;
		if (definition == this->builder
		    and std::find(disconnected.begin(), disconnected.end(), owner)
		        == disconnected.end()) {
			alive[owner] = true;
		}
	}
	// A genuinely killed Builder loses all towers, sites and foundations.
	// Mere disconnection counts toward elimination but deliberately leaves
	// that Builder's structures in the world, as the rules specify.
	for (player_id_t owner = 2; owner <= this->player_count; ++owner) {
		if (alive[owner]
		    or std::find(disconnected.begin(), disconnected.end(), owner) != disconnected.end()
		    or not this->collapsed_builders.insert(owner).second) continue;
		std::vector<entity_id_t> pending_builders{};
		for (const auto &[builder_id, build] : this->build_orders) {
			if (build.order->get_owner() == owner) pending_builders.push_back(builder_id);
		}
		for (auto id : pending_builders) this->cancel_build(state, id, now, false);
		std::vector<entity_id_t> towers{};
		std::vector<entity_id_t> foundations{};
		for (const auto &[id, entity] : state->get_game_entities()) {
			const auto &definition = entity->get_definition();
			if (definition != this->tower and definition != this->foundation) continue;
			auto ownership = std::dynamic_pointer_cast<component::Ownership>(
				entity->get_component(component::component_t::OWNERSHIP));
			if (not ownership or ownership->get_owners().get(now) != owner) continue;
			if (definition == this->tower) towers.push_back(id);
			else foundations.push_back(id);
		}
		for (auto id : towers) this->destroy_tower(state, id, now);
		for (auto id : foundations) this->delete_owned_foundation(state, owner, id, now);
	}
	const bool any_builder_alive = std::any_of(
		alive.begin() + 2, alive.end(), [](bool value) { return value; });
	this->match_outcome = decide_fvd_match_outcome(
		any_builder_alive, now, this->get_match_end_deadline());
}

std::optional<int> match_end_countdown(
	const std::optional<time::time_t> &deadline,
	const time::time_t &now) {
	if (not deadline) return std::nullopt;
	if (now >= *deadline) return 0;

	const auto remaining = *deadline - now;
	const auto whole_seconds = remaining.to_int();
	return static_cast<int>(whole_seconds
	                        + (remaining > time::time_t::from_int(whole_seconds) ? 1 : 0));
}

std::optional<int> hunter_spawn_countdown(
	const std::optional<time::time_t> &spawn_time,
	const time::time_t &now) {
	if (not spawn_time or now >= *spawn_time) {
		return std::nullopt;
	}
	const auto remaining = *spawn_time - now;
	const auto whole_seconds = remaining.to_int();
	// Round up so 00:01 remains visible until the first spawn deadline.
	return static_cast<int>(whole_seconds
	                        + (remaining > time::time_t::from_int(whole_seconds) ? 1 : 0));
}

std::optional<int> FVD::get_hunter_spawn_countdown(const time::time_t &now) const {
	std::lock_guard lock{this->hunter_spawn_time_mutex};
	return hunter_spawn_countdown(this->first_hunter_spawn_time, now);
}

std::optional<time::time_t> FVD::get_first_hunter_spawn_time() const {
	std::lock_guard lock{this->hunter_spawn_time_mutex};
	return this->first_hunter_spawn_time;
}

void FVD::request_immediate_hunter_spawn(const std::shared_ptr<GameState> &state,
                                         const time::time_t &time) {
	if (this->match_outcome) return;
	auto loop = this->event_loop.lock();
	if (not loop or not this->horse_spawn_target) {
		throw Error{ERR << "Cannot request a Hunter horse without FVD services"};
	}
	loop->submit_event(
		"game.fvd_spawn_horse", this->horse_spawn_target, state, time,
		{{"skip_initial_delay", true}});
}

void FVD::spawn_hunter_horse(const std::shared_ptr<GameState> &state,
	                            const time::time_t &time,
	                            bool skip_initial_delay) {
	if (this->match_outcome) return;
	if (not this->hunter_wave_started) {
		return;
	}
	if (skip_initial_delay) {
		bool first_spawn_pending = false;
		{
			std::lock_guard lock{this->hunter_spawn_time_mutex};
			first_spawn_pending = this->first_hunter_spawn_time
			                      and time < *this->first_hunter_spawn_time;
			if (first_spawn_pending) {
				this->first_hunter_spawn_time.reset();
			}
		}
		if (not first_spawn_pending) {
			return;
		}
		if (this->next_hunter_spawn_event) {
			this->next_hunter_spawn_event->cancel(time);
		}
	}
	else {
		std::lock_guard lock{this->hunter_spawn_time_mutex};
		this->first_hunter_spawn_time.reset();
	}
	auto loop = this->event_loop.lock();
	auto factory = this->entity_factory.lock();
	if (not loop or not factory or not this->horse_spawn_target) {
		throw Error{ERR << "Cannot spawn a Hunter horse without FVD services"};
	}
	auto spawned = factory->spawn_game_entity(loop, state, 1, this->horse, this->horse_spawn_point, time);
	this->relax_horse_crowd(state, spawned->get_id(), time);
	// New horses ride to the rally point like a Move order: a travelling
	// horse does not stop to fight, then its stance applies on arrival.
	if (this->hunter_rally_point) {
		const auto from = std::dynamic_pointer_cast<component::Position>(
			spawned->get_component(component::component_t::POSITION))->get_positions().get(time).to_tile();
		if (auto destination = this->closest_reachable(state, from, *this->hunter_rally_point, time)) {
			system::Move::move_default(spawned, state, *destination, time);
		}
	}
	this->next_hunter_spawn_event = loop->create_event(
		"game.fvd_spawn_horse", this->horse_spawn_target, state,
		time + this->horse_spawn_interval);
}

std::optional<std::vector<entity_id_t>> FVD::resolve_command_units(
	const std::shared_ptr<GameState> &state, player_id_t owner,
	const std::vector<entity_id_t> &ids, const time::time_t &time) const {
	if (owner < 1 or owner > this->player_count or ids.empty() or ids.size() > 1024) {
		return std::nullopt;
	}
	std::unordered_set<entity_id_t> seen;
	std::vector<entity_id_t> survivors;
	survivors.reserve(ids.size());
	for (auto id : ids) {
		if (not seen.insert(id).second) {
			return std::nullopt;
		}
		// Only a horse known to have died can be pruned from an old
		// selection. An unknown or forged entity ID must still fail.
		if (owner == 1 and this->dead_horses.contains(id)) {
			continue;
		}
		// A box selection may sweep up the Hunter tower with the horses.
		// It cannot move or fight, so the order applies to the horses alone.
		if (owner == 1 and this->hunter_tower_id == id) {
			continue;
		}
		auto found = state->get_game_entities().find(id);
		if (found == state->get_game_entities().end()) {
			return std::nullopt;
		}
		auto entity = found->second;
		if (entity->get_definition() != (owner == 1 ? this->horse : this->builder)
		    or not entity->has_component(component::component_t::MOVE)
		    or not entity->has_component(component::component_t::OWNERSHIP)) {
			return std::nullopt;
		}
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (ownership->get_owners().get(time) != owner) {
			return std::nullopt;
		}
		if (not builder_alive(entity, time) or this->is_garrisoned(id)) {
			if (owner == 1) {
				continue;
			}
			return std::nullopt;
		}
		survivors.push_back(id);
	}
	if (survivors.empty()) {
		return std::nullopt;
	}
	return survivors;
}

bool FVD::can_command_units(const std::shared_ptr<GameState> &state,
                            player_id_t owner, const std::vector<entity_id_t> &ids,
                            const time::time_t &time) const {
	return this->resolve_command_units(state, owner, ids, time).has_value();
}

void FVD::cancel_attack_move(entity_id_t id) {
	this->attack_orders.erase(id);
}

std::optional<entity_id_t> FVD::completed_tower_at(
	const std::shared_ptr<GameState> &state, const coord::tile &tile) const {
	std::optional<entity_id_t> found;
	for (const auto &[id, footprint] : this->tower_footprints) {
		if (footprint.under_construction
		    or not state->get_game_entities().contains(id)) {
			continue;
		}
		for (const auto &cell : footprint.cells) {
			if (cell.tile == tile and (not found or id < *found)) {
				found = id;
				break;
			}
		}
	}
	return found;
}

bool FVD::issue_attack_move(const std::shared_ptr<GameState> &state,
                            player_id_t owner, const std::vector<entity_id_t> &ids,
                            const coord::phys3 &destination, const time::time_t &time) {
	return this->issue_hunter_route(state, owner, ids, destination, false, time);
}

bool FVD::issue_patrol(const std::shared_ptr<GameState> &state,
                       player_id_t owner, const std::vector<entity_id_t> &ids,
                       const coord::phys3 &destination, const time::time_t &time) {
	return this->issue_hunter_route(state, owner, ids, destination, true, time);
}

bool FVD::issue_hunter_route(const std::shared_ptr<GameState> &state,
                             player_id_t owner, const std::vector<entity_id_t> &ids,
                             const coord::phys3 &destination, bool patrol,
                             const time::time_t &time) {
	if (owner != 1 or destination.ne < 0 or destination.se < 0
	    or destination.ne >= this->map_size or destination.se >= this->map_size
	    or destination.up != 0) {
		return false;
	}
	auto survivors = this->resolve_command_units(state, owner, ids, time);
	if (not survivors) {
		return false;
	}
	// Clicking a blocked tower footprint is an attack, not an
	// unreachable movement destination. A patrol could never reach it.
	if (auto tower = this->completed_tower_at(state, destination.to_tile())) {
		return not patrol
		   and this->issue_focus_attack(state, owner, *survivors, *tower, time);
	}
	auto pathfinder = state->get_map()->get_pathfinder();
	auto grid = state->get_map()->get_grid_id("fvd.data.terrain.Land");
	for (auto id : *survivors) {
		auto entity = state->get_game_entity(id);
		auto position = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		auto route = pathfinder->get_path(path::PathRequest{
			grid,
			position->get_positions().get(time).to_tile(),
			destination.to_tile(),
			time});
		if (route.status != path::PathResult::FOUND) {
			return false;
		}
	}
	auto ordered = *survivors;
	std::sort(ordered.begin(), ordered.end());
	for (auto id : ordered) {
		auto entity = state->get_game_entity(id);
		auto activity = std::dynamic_pointer_cast<component::Activity>(
			entity->get_component(component::component_t::ACTIVITY));
		auto queue = std::dynamic_pointer_cast<component::CommandQueue>(
			entity->get_component(component::component_t::COMMANDQUEUE));
		activity->cancel_events(time);
		while (not queue->get_queue().empty(time)) {
			queue->pop_command(time);
		}
		activity->init(time);
		entity->get_manager()->run_activity_system(time);
		AttackOrder order{destination, std::nullopt, time};
		if (patrol) {
			order.patrol_return = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION))->get_positions().get(time);
		}
		this->attack_orders.insert_or_assign(id, order);
		system::Move::move_default(entity, state, destination, time);
	}
	return true;
}

void FVD::remove_hunter_tower(const std::shared_ptr<GameState> &state, const time::time_t &time) {
	auto map = state->get_map();
	std::unordered_set<nyan::fqon_t> changed_grids;
	for (const auto &cell : this->hunter_tower_cells) {
		map->set_path_cost(cell.path_type, cell.tile, cell.previous_cost, time);
		changed_grids.insert(cell.path_type);
	}
	for (const auto &path_type : changed_grids) {
		map->rebuild_path_grid(path_type);
	}
	this->hunter_tower_cells.clear();
	if (this->hunter_tower_id) {
		state->remove_game_entity(*this->hunter_tower_id, time);
	}
	this->hunter_tower_id.reset();
	this->horse_spawn_point = this->get_centre();
	if (this->hunter_rally_marker_id and state->get_game_entities().contains(*this->hunter_rally_marker_id)) {
		state->remove_game_entity(*this->hunter_rally_marker_id, time);
	}
	this->hunter_rally_marker_id.reset();
	this->hunter_rally_point.reset();
}

bool FVD::set_hunter_rally_point(const std::shared_ptr<GameState> &state,
                                 player_id_t owner, const std::vector<entity_id_t> &ids,
                                 const coord::phys3 &point, const time::time_t &time) {
	if (owner != 1 or not this->hunter_tower_id or ids.size() != 1
	    or ids.front() != *this->hunter_tower_id
	    or not state->get_game_entities().contains(*this->hunter_tower_id)
	    or point.ne < 0 or point.se < 0
	    or point.ne >= this->map_size or point.se >= this->map_size
	    or point.up != 0) {
		return false;
	}
	// Like AoE2, anywhere on the map is allowed, even behind a wall: horses
	// ride as close as they can get whenever it cannot be reached.
	auto loop = this->event_loop.lock();
	auto factory = this->entity_factory.lock();
	if (not loop or not factory) [[unlikely]] {
		throw Error{ERR << "Cannot place a Hunter rally point without FVD services"};
	}
	// Replace the marker rather than moving it, so every client sees one
	// entity appear in the new place without interpolating across the map.
	if (this->hunter_rally_marker_id and state->get_game_entities().contains(*this->hunter_rally_marker_id)) {
		state->remove_game_entity(*this->hunter_rally_marker_id, time);
	}
	auto marker = factory->spawn_game_entity(loop, state, 1, this->hunter_rally_marker, point, time);
	system::Idle::idle(marker, time);
	this->hunter_rally_marker_id = marker->get_id();
	this->hunter_rally_point = point;
	return true;
}

std::optional<coord::phys3> FVD::closest_reachable(const std::shared_ptr<GameState> &state,
                                                   const coord::tile &from,
                                                   const coord::phys3 &target,
                                                   const time::time_t &time) const {
	auto map = state->get_map();
	auto pathfinder = map->get_pathfinder();
	const auto grid_id = map->get_grid_id("fvd.data.terrain.Land");
	if (pathfinder->get_path(path::PathRequest{grid_id, from, target.to_tile(), time}).status
	    == path::PathResult::FOUND) {
		return target;
	}
	// Flood the tiles reachable from the horse, in a fixed order, and keep
	// the one nearest the target. Orthogonal steps only, so every tile found
	// is one the pathfinder can reach too.
	auto grid = pathfinder->get_grid(grid_id);
	const auto sector_size = grid->get_sector_size();
	const auto size = static_cast<coord::tile_t>(this->map_size);
	auto passable = [&](const coord::tile &tile) {
		if (tile.ne < 0 or tile.se < 0 or tile.ne >= size or tile.se >= size) {
			return false;
		}
		auto sector = grid->get_sector(tile.ne / sector_size, tile.se / sector_size);
		return sector->get_cost_field()->get_cost(tile - sector->get_position().to_tile(sector_size))
		       < path::COST_IMPASSABLE;
	};
	if (not passable(from)) {
		return std::nullopt;
	}
	auto distance = [&](const coord::tile &tile) {
		const auto delta = tile.to_phys3_center() - target;
		const double ne = delta.ne.to_double();
		const double se = delta.se.to_double();
		return ne * ne + se * se;
	};
	std::vector<bool> seen(static_cast<size_t>(size) * size, false);
	auto index = [&](const coord::tile &tile) { return static_cast<size_t>(tile.ne) * size + tile.se; };
	std::deque<coord::tile> frontier{from};
	seen[index(from)] = true;
	coord::tile best = from;
	double best_distance = distance(from);
	while (not frontier.empty()) {
		const auto tile = frontier.front();
		frontier.pop_front();
		if (const auto d = distance(tile); d < best_distance) {
			best = tile;
			best_distance = d;
		}
		for (const auto &next : {coord::tile{tile.ne + 1, tile.se}, coord::tile{tile.ne - 1, tile.se},
		                         coord::tile{tile.ne, tile.se + 1}, coord::tile{tile.ne, tile.se - 1}}) {
			if (passable(next) and not seen[index(next)]) {
				seen[index(next)] = true;
				frontier.push_back(next);
			}
		}
	}
	if (best == from) {
		return std::nullopt;
	}
	return best.to_phys3_center();
}

std::optional<coord::phys3> FVD::get_hunter_rally_point() const {
	return this->hunter_rally_point;
}

std::optional<entity_id_t> FVD::get_hunter_rally_marker() const {
	return this->hunter_rally_marker_id;
}

const std::string &FVD::get_hunter_rally_marker_definition() const {
	return this->hunter_rally_marker;
}

coord::phys3 FVD::get_centre() const {
	auto centre = coord::phys_t::from_int(this->map_size / 2);
	return {centre, centre, 0};
}

size_t FVD::get_player_count() const {
	return this->player_count;
}

size_t FVD::get_map_size() const {
	return this->map_size;
}

std::vector<FVDConstructionState> FVD::get_construction_state() const {
	std::vector<FVDConstructionState> result{};
	result.reserve(this->build_orders.size());
	for (const auto &[builder_id, build] : this->build_orders) {
		result.push_back({
			build.order->get_id(),
			build.order->get_owner(),
			builder_id,
			build.order->get_target(),
			build.tower_id.has_value()
				? FVDConstructionPhase::UNDER_CONSTRUCTION
				: FVDConstructionPhase::PLANNED,
			build.tower_id.has_value()
				? build.arrival + this->construction_time
				: build.arrival,
			build.tower_id.value_or(build.foundation_id),
			build.arrival,
		});
	}
	std::sort(result.begin(), result.end(),
	          [](const auto &left, const auto &right) {
		          return left.order_id < right.order_id;
	          });
	return result;
}

std::vector<FVDTileReservation> FVD::get_reserved_tiles() const {
	std::vector<FVDTileReservation> result{};
	result.reserve(this->reserved_tiles.size());
	for (const auto &[tile, builder_id] : this->reserved_tiles) {
		result.push_back({tile, builder_id});
	}
	std::sort(result.begin(), result.end(),
	          [](const auto &left, const auto &right) {
		          return left.tile.ne < right.tile.ne
		              or (left.tile.ne == right.tile.ne
		                  and left.tile.se < right.tile.se);
	          });
	return result;
}

} // namespace openage::gamestate
