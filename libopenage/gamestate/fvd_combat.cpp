// Copyright 2026 the openage authors. See copying.md for legal info.

#include "fvd.h"

#include <algorithm>
#include <optional>
#include <mutex>
#include <cmath>
#include <vector>
#include <utility>

#include "error/error.h"
#include "event/event_loop.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/entity_factory.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/manager.h"
#include "gamestate/system/idle.h"
#include "gamestate/system/move.h"
#include "pathfinding/pathfinder.h"
#include "renderer/stages/world/render_entity.h"

namespace openage::gamestate {

namespace {

constexpr const char *HEALTH = "fvd.data.entities.Health";
constexpr double HORSE_ATTACK_RANGE = 4.0;
constexpr double HORSE_ATTACK_RANGE_SQUARED = HORSE_ATTACK_RANGE * HORSE_ATTACK_RANGE;
/** Furthest a Defensive engagement may stray from its engagement point. */
constexpr double HORSE_LEASH_RANGE = 6.0;
constexpr double HORSE_LEASH_RANGE_SQUARED = HORSE_LEASH_RANGE * HORSE_LEASH_RANGE;

/** Use stable ordering for simultaneous combat and lethal-hit decisions. */
std::vector<entity_id_t> entities_by_definition(
	const std::shared_ptr<GameState> &state, const std::string &definition) {
	std::vector<entity_id_t> ids;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->get_definition() == definition) {
			ids.push_back(id);
		}
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}

bool alive(const std::shared_ptr<GameEntity> &entity, const time::time_t &time) {
	if (not entity->has_component(component::component_t::LIVE)) {
		return false;
	}
	auto live = std::dynamic_pointer_cast<component::Live>(
		entity->get_component(component::component_t::LIVE));
	return live->get_attribute(time, HEALTH)->get(time) > 0;
}

double distance_squared(const coord::phys3 &first, const coord::phys3 &second) {
	auto delta = first - second;
	double x = delta.ne.to_double();
	double y = delta.se.to_double();
	return x * x + y * y;
}

coord::phys3 position_at(const std::shared_ptr<GameEntity> &entity,
                         const time::time_t &time) {
	return std::dynamic_pointer_cast<component::Position>(
		entity->get_component(component::component_t::POSITION))->get_positions().get(time);
}

double distance_squared(const std::shared_ptr<GameEntity> &first,
                        const std::shared_ptr<GameEntity> &second,
                        const time::time_t &time) {
	return distance_squared(position_at(first, time), position_at(second, time));
}

/** Whether a horse is still following a movement route at this time. */
bool travelling(const std::shared_ptr<GameEntity> &horse, const time::time_t &time) {
	auto move = std::dynamic_pointer_cast<component::Move>(
		horse->get_component(component::component_t::MOVE));
	return move->get_active_target() and move->get_active_arrival() > time;
}


/** Stop a previous route at the authoritative time of an engagement. */
void halt_for_engagement(const std::shared_ptr<GameEntity> &horse,
                         const time::time_t &time) {
	auto position = std::dynamic_pointer_cast<component::Position>(
		horse->get_component(component::component_t::POSITION));
	position->set_position(time, position->get_positions().get(time));
	auto activity = std::dynamic_pointer_cast<component::Activity>(
		horse->get_component(component::component_t::ACTIVITY));
	activity->cancel_events(time);
	system::Idle::idle(horse, time);
}

/**
 * Find the nearest reachable tile within firing range of a target.
 * Choose by travel proximity to avoid moving ranged horses to melee.
 */
std::optional<coord::phys3> ranged_approach(
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<GameEntity> &horse,
	const std::shared_ptr<GameEntity> &target,
	const time::time_t &time, size_t map_size) {
	const auto target_position = std::dynamic_pointer_cast<component::Position>(
		target->get_component(component::component_t::POSITION))->get_positions().get(time);
	const auto horse_position = std::dynamic_pointer_cast<component::Position>(
		horse->get_component(component::component_t::POSITION))->get_positions().get(time);
	const auto site = target_position.to_tile();
	const auto map = state->get_map();
	const auto grid = map->get_grid_id("fvd.data.terrain.Land");
	const auto pathfinder = map->get_pathfinder();

	std::vector<std::pair<double, coord::tile>> candidates;
	for (int ne = -4; ne <= 4; ++ne) {
		for (int se = -4; se <= 4; ++se) {
			if (ne == 0 and se == 0) {
				continue;
			}
			coord::tile_delta offset{ne, se};
			coord::tile tile{site.ne + offset.ne, site.se + offset.se};
			if (tile.ne < 0 or tile.se < 0
			    or tile.ne >= static_cast<coord::tile_t>(map_size)
			    or tile.se >= static_cast<coord::tile_t>(map_size)) {
				continue;
			}
			auto destination = tile.to_phys3_center();
			auto delta = destination - target_position;
			const double dx = delta.ne.to_double();
			const double dy = delta.se.to_double();
			if (dx * dx + dy * dy > HORSE_ATTACK_RANGE_SQUARED) {
				continue;
			}
			candidates.emplace_back((destination - horse_position).length(), tile);
		}
	}
	std::stable_sort(candidates.begin(), candidates.end(),
	                 [](const auto &a, const auto &b) {
		                 return a.first < b.first;
	                 });
	for (const auto &candidate : candidates) {
		const auto &tile = candidate.second;
		auto route = pathfinder->get_path(path::PathRequest{
			grid,
			horse_position.to_tile(),
			tile,
			time});
		if (route.status == path::PathResult::FOUND) {
			return tile.to_phys3_center();
		}
	}
	return std::nullopt;
}

} // namespace

std::vector<FVD::ArrowTrail> FVD::get_visible_arrows(const time::time_t &now) const {
	std::vector<ArrowTrail> result;
	std::lock_guard lock{this->arrow_visuals_mutex};
	result.reserve(this->arrow_visuals.size());
	for (const auto &arrow : this->arrow_visuals) {
		// Retain large pending histories for delivery but never submit
		// unbounded overlapping projectile visuals to Qt every frame.
		if (result.size() >= 2048) break;
		if (now < arrow.launch_time
		    or now >= arrow.impact_time + time::time_t::from_double(0.12)) {
			continue;
		}
		if (now >= arrow.impact_time) {
			result.push_back(ArrowTrail{
				arrow.destination, arrow.destination, 1.0, arrow.owner, true});
			continue;
		}
		const double duration = (arrow.impact_time - arrow.launch_time).to_double();
		if (duration <= 0.0) {
			continue;
		}
		const double progress = std::clamp(
			(now - arrow.launch_time).to_double() / duration, 0.0, 1.0);
		const double tail_progress = std::max(0.0, progress - 0.22);
		auto position_at = [&](double fraction) {
			auto lerp = [fraction](coord::phys_t first, coord::phys_t last) {
				return coord::phys_t::from_double(
					first.to_double() + (last.to_double() - first.to_double()) * fraction);
			};
			// A small ballistic arc reads as an arrow without spawning a
			// rendered game entity or affecting the hit calculation.
			const double lift = 4.0 * fraction * (1.0 - fraction) * 0.50;
			return coord::phys3{
				lerp(arrow.start.ne, arrow.destination.ne),
				lerp(arrow.start.se, arrow.destination.se),
				lerp(arrow.start.up, arrow.destination.up)
				    + coord::phys_t::from_double(lift)};
		};
		result.push_back(ArrowTrail{
			position_at(tail_progress), position_at(progress), progress, arrow.owner});
	}
	return result;
}

std::vector<FVDAttackOrderState> FVD::get_attack_order_state() const {
	std::vector<FVDAttackOrderState> result;
	result.reserve(this->attack_orders.size());
	for (const auto &[horse_id, order] : this->attack_orders) {
		result.push_back({horse_id,
		                  order.destination,
		                  order.target,
		                  order.reached_destination,
		                  order.focus_target});
	}
	std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
		return left.horse_id < right.horse_id;
	});
	return result;
}

void FVD::enable_projectile_replication() {
	std::lock_guard lock{this->arrow_visuals_mutex};
	this->projectile_replication_enabled = true;
}

void FVD::prune_delivered_projectiles(const time::time_t &now,
                                      uint64_t delivered_to_all) {
	std::lock_guard lock{this->arrow_visuals_mutex};
	std::erase_if(this->arrow_visuals, [&](const ArrowVisual &shot) {
		return shot.arrow_id <= delivered_to_all
		    and shot.impact_time + time::time_t::from_int(3) <= now;
	});
}

std::vector<FVDArrowState> FVD::get_arrow_state(
	const time::time_t &now, std::optional<uint64_t> delivered_through) const {
	std::vector<FVDArrowState> result;
	std::lock_guard lock{this->arrow_visuals_mutex};
	constexpr size_t max_batch = 2048;
	result.reserve(std::min(max_batch, this->arrow_visuals.size()));
	auto add = [&](const ArrowVisual &shot) {
		result.push_back({shot.arrow_id, shot.owner, shot.target_id,
		                  shot.start, shot.destination,
		                  shot.launch_time, shot.impact_time});
	};
	// Never skip an undelivered launch merely because it has already
	// impacted or more than a snapshot interval elapsed. Those events
	// remain in host history until every recipient has been sent them.
	if (delivered_through) {
		for (const auto &shot : this->arrow_visuals) {
			if (shot.arrow_id <= *delivered_through or shot.launch_time > now) continue;
			if (result.size() >= max_batch) return result;
			add(shot);
		}
	}
	// Re-include recent, already-delivered launches when capacity permits
	// so recovery snapshots and in-flight projectile rendering stay intact.
	for (const auto &shot : this->arrow_visuals) {
		if (shot.launch_time > now
		    or shot.impact_time + time::time_t::from_int(3) <= now
		    or (delivered_through and shot.arrow_id > *delivered_through)) continue;
		if (result.size() >= max_batch) break;
		add(shot);
	}
	std::sort(result.begin(), result.end(),
	          [](const auto &a, const auto &b) { return a.arrow_id < b.arrow_id; });
	return result;
}

uint64_t FVD::get_kills(player_id_t owner) const {
	return owner > 0 and owner < this->player_kills.size()
	         ? this->player_kills[owner].load() : 0;
}

std::optional<uint64_t> FVD::get_tower_kills(
	entity_id_t tower, player_id_t owner) const {
	std::lock_guard lock{this->tower_kills_mutex};
	auto it = this->tower_kills.find(tower);
	if (it == this->tower_kills.end() or it->second.owner != owner) {
		return std::nullopt;
	}
	return it->second.kills;
}

int64_t FVD::get_tower_damage() const {
	return this->tower_damage;
}

int64_t FVD::get_tower_armor() const {
	return this->tower_armor;
}

bool FVD::apply_damage(const std::shared_ptr<GameState> &state,
                       entity_id_t target, int64_t damage,
                       player_id_t owner, bool tower_shot,
                       const time::time_t &time) {
	auto found = state->get_game_entities().find(target);
	if (found == state->get_game_entities().end() or not alive(found->second, time)) {
		return false;
	}
	auto entity = found->second;
	const auto &definition = entity->get_definition();
	if (tower_shot) {
		if (owner < 2 or owner > this->player_count or definition != this->horse) {
			return false;
		}
	}
	else if (owner != 1 or (definition != this->tower and definition != this->builder)) {
		return false;
	}
	const int64_t armour = definition == this->tower ? this->tower_armor : 0;
	const int64_t actual_damage = std::max<int64_t>(1, damage - armour);
	auto live = std::dynamic_pointer_cast<component::Live>(
		entity->get_component(component::component_t::LIVE));
	const int64_t hp = live->get_attribute(time, HEALTH)->get(time);
	const int64_t new_hp = std::max<int64_t>(0, hp - actual_damage);
	live->set_attribute(time, HEALTH, new_hp);

	if (auto render = entity->get_render_entity()) {
		const int64_t max_health = definition == this->tower ? 100
		                           : definition == this->horse ? 90 : 25;
		render->update_health(*live->get_attribute(time, HEALTH), max_health, time);
	}
	if (new_hp > 0) {
		return false;
	}

	if (definition == this->tower) {
		this->tower_next_shot.erase(target);
		this->destroy_tower(state, target, time);
	}
	else {
		this->attack_orders.erase(target);
		if (definition == this->horse) {
			this->hunter_stances.erase(target);
			this->dead_horses.insert(target);
		}
		if (definition == this->builder) {
			this->cancel_build(state, target, time);
		}
		state->remove_game_entity(target, time);
	}
	return true;
}

void FVD::arrow_hit(const std::shared_ptr<GameState> &state,
                    entity_id_t target, player_id_t owner,
                    entity_id_t firing_tower, int64_t damage,
                    const time::time_t &time) {
	// An already deceased target cannot award a second kill. Retired tower
	// records survive so the last arrow still belongs to its firing tower.
	if (firing_tower != 0) {
		std::lock_guard lock{this->tower_kills_mutex};
		auto it = this->tower_kills.find(firing_tower);
		if (it == this->tower_kills.end() or it->second.owner != owner) {
			return;
		}
	}
	if (not this->apply_damage(state, target, damage, owner, true, time)) {
		return;
	}
	this->player_kills.at(owner).fetch_add(1);
	if (firing_tower != 0) {
		std::lock_guard lock{this->tower_kills_mutex};
		this->tower_kills.at(firing_tower).kills += 1;
	}
}

void FVD::horse_hit(const std::shared_ptr<GameState> &state,
                    entity_id_t horse_id, entity_id_t target,
                    int64_t damage, const time::time_t &time) {
	auto order_it = this->attack_orders.find(horse_id);
	if (order_it == this->attack_orders.end()) {
		return;
	}
	auto &order = order_it->second;
	if (not order.pending_hit or *order.pending_hit != time
	    or order.target != target) {
		return;
	}
	order.pending_hit.reset();
	auto horse = state->get_game_entities().find(horse_id);
	if (horse == state->get_game_entities().end()
	    or not alive(horse->second, time)
	    or horse->second->get_animation_action() != AnimationAction::ATTACK) {
		return;
	}

	this->apply_damage(state, target, damage, 1, false, time);
	if (state->get_game_entities().contains(target)) {
		return;
	}
	order.target.reset();
	if (order.focus_target) {
		halt_for_engagement(horse->second, time);
		this->attack_orders.erase(order_it);
	}
}

bool FVD::issue_focus_attack(const std::shared_ptr<GameState> &state,
                             player_id_t owner, const std::vector<entity_id_t> &ids,
                             entity_id_t enemy_id, const time::time_t &time) {
	if (owner != 1) {
		return false;
	}
	auto survivors = this->resolve_command_units(state, owner, ids, time);
	if (not survivors) {
		return false;
	}
	auto target_it = state->get_game_entities().find(enemy_id);
	if (target_it == state->get_game_entities().end()
	    or not alive(target_it->second, time)
	    or not this->can_see(state, FVDTeam::HUNTER, enemy_id, time)) {
		return false;
	}
	auto target = target_it->second;
	if (target->get_definition() != this->builder
	    and target->get_definition() != this->tower) {
		return false;
	}
	auto ownership = std::dynamic_pointer_cast<component::Ownership>(
		target->get_component(component::component_t::OWNERSHIP));
	if (not ownership or ownership->get_owners().get(time) < 2
	    or ownership->get_owners().get(time) > this->player_count) {
		return false;
	}
	if (target->get_definition() == this->tower) {
		auto footprint = this->tower_footprints.find(enemy_id);
		if (footprint == this->tower_footprints.end()
		    or footprint->second.under_construction) {
			return false;
		}
	}

	// Validate every living horse and its approach before changing orders.
	std::sort(survivors->begin(), survivors->end());
	std::vector<std::optional<coord::phys3>> approaches;
	approaches.reserve(survivors->size());
	for (auto horse_id : *survivors) {
		auto horse = state->get_game_entity(horse_id);
		if (distance_squared(horse, target, time) <= HORSE_ATTACK_RANGE_SQUARED) {
			approaches.push_back(std::nullopt);
			continue;
		}
		auto approach = ranged_approach(state, horse, target, time, this->map_size);
		if (not approach) {
			return false;
		}
		approaches.push_back(*approach);
	}

	for (size_t i = 0; i < survivors->size(); ++i) {
		auto horse = state->get_game_entity(survivors->at(i));
		halt_for_engagement(horse, time);
		auto queue = std::dynamic_pointer_cast<component::CommandQueue>(
			horse->get_component(component::component_t::COMMANDQUEUE));
		while (not queue->get_queue().empty(time)) {
			queue->pop_command(time);
		}
		auto activity = std::dynamic_pointer_cast<component::Activity>(
			horse->get_component(component::component_t::ACTIVITY));
		activity->init(time);
		horse->get_manager()->run_activity_system(time);
		auto current = std::dynamic_pointer_cast<component::Position>(
			horse->get_component(component::component_t::POSITION))->get_positions().get(time);
		this->attack_orders.insert_or_assign(
			horse->get_id(), AttackOrder{current, enemy_id, time, true, true});
		if (approaches[i]) {
			system::Move::move_default(horse, state, *approaches[i], time);
		}
	}
	return true;
}

void FVD::hunter_tower_hit(const std::shared_ptr<GameState> &state, entity_id_t target,
                           int64_t damage, const time::time_t &time) {
	// Damage only lands while the shooter stands; kills earn no Builder credit.
	if (not this->hunter_tower_id or not state->get_game_entities().contains(*this->hunter_tower_id)) {
		return;
	}
	this->apply_damage(state, target, damage, 1, false, time);
}

std::optional<entity_id_t> FVD::get_hunter_tower() const {
	return this->hunter_tower_id;
}

coord::phys3 FVD::get_horse_spawn_point() const {
	return this->horse_spawn_point;
}

bool FVD::set_hunter_stance(const std::shared_ptr<GameState> &state,
                            player_id_t owner, const std::vector<entity_id_t> &ids,
                            HunterStance stance, const time::time_t &time) {
	if (owner != 1) {
		return false;
	}
	auto survivors = this->resolve_command_units(state, owner, ids, time);
	if (not survivors) {
		return false;
	}
	for (auto id : *survivors) {
		if (this->get_hunter_stance(id) == stance) {
			continue;
		}
		if (stance == HunterStance::OBSERVE) {
			this->hunter_stances.erase(id);
		}
		else {
			this->hunter_stances.insert_or_assign(id, stance);
		}
		// The previous stance's engagement would otherwise outlive it,
		// e.g. an Observe horse still chasing an Aggressive target.
		auto order = this->attack_orders.find(id);
		if (order != this->attack_orders.end() and order->second.stance_engagement) {
			halt_for_engagement(state->get_game_entity(id), time);
			this->attack_orders.erase(order);
		}
	}
	return true;
}

HunterStance FVD::get_hunter_stance(entity_id_t horse) const {
	auto found = this->hunter_stances.find(horse);
	return found == this->hunter_stances.end() ? HunterStance::OBSERVE : found->second;
}

bool FVD::hunter_target_eligible(const std::shared_ptr<GameState> &state,
                                 entity_id_t candidate, const time::time_t &time) const {
	auto other = state->get_game_entities().find(candidate);
	if (other == state->get_game_entities().end() or not alive(other->second, time)
	    or not this->can_see(state, FVDTeam::HUNTER, candidate, time)) {
		return false;
	}
	if (other->second->get_definition() == this->tower) {
		auto footprint = this->tower_footprints.find(candidate);
		return footprint != this->tower_footprints.end()
		    and not footprint->second.under_construction;
	}
	return other->second->get_definition() == this->builder;
}

std::optional<entity_id_t> FVD::nearest_hunter_target(
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<GameEntity> &horse,
	const std::vector<entity_id_t> &towers,
	const std::vector<entity_id_t> &builders,
	double radius,
	const std::optional<coord::phys3> &anchor,
	double anchor_radius,
	const time::time_t &time) const {
	const double anchor_squared = anchor_radius * anchor_radius;
	// A tower within range takes priority over any Builder, even a closer one.
	for (const auto *candidates : {&towers, &builders}) {
		std::optional<entity_id_t> nearest;
		double closest = radius * radius;
		for (auto candidate : *candidates) {
			if (not this->hunter_target_eligible(state, candidate, time)) {
				continue;
			}
			auto entity = state->get_game_entity(candidate);
			auto distance = distance_squared(horse, entity, time);
			if (distance <= closest
			    and (not anchor
			         or distance_squared(position_at(entity, time), *anchor) <= anchor_squared)) {
				closest = distance;
				nearest = candidate;
			}
		}
		if (nearest) {
			return nearest;
		}
	}
	return std::nullopt;
}

bool FVD::begin_stance_engagement(const std::shared_ptr<GameState> &state,
                                  const std::shared_ptr<GameEntity> &horse,
                                  const std::vector<entity_id_t> &towers,
                                  const std::vector<entity_id_t> &builders,
                                  const time::time_t &time) {
	const auto stance = this->get_hunter_stance(horse->get_id());
	// Travelling horses are following a human Move; they do not stop to fight.
	if (stance == HunterStance::OBSERVE or travelling(horse, time)) {
		return false;
	}
	const auto here = position_at(horse, time);
	const auto target = stance == HunterStance::AGGRESSIVE
		? this->nearest_hunter_target(state, horse, towers, builders,
		                              this->vision_range(state, horse->get_definition()),
		                              std::nullopt, 0.0, time)
		: this->nearest_hunter_target(state, horse, towers, builders,
		                              HORSE_ATTACK_RANGE, here, HORSE_LEASH_RANGE, time);
	if (not target) {
		return false;
	}
	AttackOrder order{here, target, time};
	order.stance_engagement = true;
	this->attack_orders.insert_or_assign(horse->get_id(), order);
	halt_for_engagement(horse, time);
	return true;
}

void FVD::combat_tick(const std::shared_ptr<GameState> &state,
                      const time::time_t &time) {
	if (this->match_outcome) return;
	const auto horses = entities_by_definition(state, this->horse);
	const auto builders = entities_by_definition(state, this->builder);
	const auto towers = entities_by_definition(state, this->tower);
	this->update_vision(state, time);
	this->vision_cached_at = time;

	// AttackMove searches within four tiles, including after arrival.
	// Towers retain priority over Builders. Idle horses without an order
	// may instead start an engagement through their stance.
	for (auto id : horses) {
		auto order_it = this->attack_orders.find(id);
		if (order_it == this->attack_orders.end()) {
			auto idle = state->get_game_entities().find(id);
			if (idle == state->get_game_entities().end() or not alive(idle->second, time)
			    or not this->begin_stance_engagement(state, idle->second, towers, builders, time)) {
				continue;
			}
			order_it = this->attack_orders.find(id);
		}
		auto found = state->get_game_entities().find(id);
		if (found == state->get_game_entities().end() or not alive(found->second, time)) {
			this->attack_orders.erase(order_it);
			continue;
		}
		auto horse = found->second;
		auto &order = order_it->second;
		auto move = std::dynamic_pointer_cast<component::Move>(
			horse->get_component(component::component_t::MOVE));
		auto position = std::dynamic_pointer_cast<component::Position>(
			horse->get_component(component::component_t::POSITION));
		auto current = position->get_positions().get(time);
		auto remaining = current - order.destination;
		if (remaining.length() < 0.15 and not order.target and not order.patrol_return) {
			order.reached_destination = true;
		}
		const auto stance = this->get_hunter_stance(id);
		// A Defensive engagement guards its engagement point, the order's destination.
		const bool defending = order.stance_engagement and stance == HunterStance::DEFENSIVE;
		// On patrol the stance decides whether and how far the horse fights.
		const bool patrolling = order.patrol_return.has_value();
		if (patrolling and stance == HunterStance::OBSERVE and order.target) {
			halt_for_engagement(horse, time);
			order.target.reset();
			order.patrol_engagement.reset();
		}

		auto eligible = [&](entity_id_t candidate) {
			return this->hunter_target_eligible(state, candidate, time);
		};
		// A manual target is never preempted by a tower.
		// A tower entering firing range otherwise takes
		// precedence over a Builder already targeted by AttackMove.
		if (not order.focus_target and order.target and eligible(*order.target)
		    and state->get_game_entity(*order.target)->get_definition() == this->builder) {
			for (auto tower_id : towers) {
				if (not eligible(tower_id)) {
					continue;
				}
				auto tower_entity = state->get_game_entity(tower_id);
				auto distance = distance_squared(horse, tower_entity, time);
				if (distance <= HORSE_ATTACK_RANGE_SQUARED) {
					order.target.reset();
					break;
				}
			}
		}
		if (order.target and not eligible(*order.target)) {
			// A destroyed or vanished target must not leave an orphaned approach route.
			if (move->get_active_target() and move->get_active_arrival() > time) {
				halt_for_engagement(horse, time);
			}
			order.target.reset();
		}
		if (order.focus_target and not order.target) {
			// The explicit target died or became invalid. Do not convert
			// a focus command into automatic AttackMove targeting.
			halt_for_engagement(horse, time);
			this->attack_orders.erase(order_it);
			continue;
		}
		if (not order.target and not (patrolling and stance == HunterStance::OBSERVE)) {
			// A tower within firing range takes priority over a nearby
			// Builder, even if the original move destination was reached.
			// Aggressive horses look as far as they can see; Defensive ones
			// only accept enemies near the point they are guarding.
			const bool aggressive = stance == HunterStance::AGGRESSIVE
			                        and (order.stance_engagement or patrolling);
			std::optional<coord::phys3> anchor{};
			double anchor_radius = HORSE_LEASH_RANGE;
			if (defending) {
				anchor = order.destination;
			}
			else if (patrolling and order.patrol_engagement) {
				anchor = order.patrol_engagement;
			}
			const auto nearest = this->nearest_hunter_target(
				state, horse, towers, builders,
				aggressive ? this->vision_range(state, horse->get_definition()) : HORSE_ATTACK_RANGE,
				anchor, anchor_radius, time);
			if (nearest) {
				order.target = nearest;
				halt_for_engagement(horse, time);
				if (patrolling and stance == HunterStance::DEFENSIVE and not order.patrol_engagement) {
					order.patrol_engagement = current;
				}
			}
		}
		if (patrolling and order.target) {
			const auto enemy_at = position_at(state->get_game_entity(*order.target), time);
			if (stance == HunterStance::AGGRESSIVE
			    and std::sqrt(distance_squared(current, enemy_at))
			            > this->vision_range(state, horse->get_definition())) {
				// An Aggressive patrol has no leash, but once its quarry is out
				// of its own sight it gives up and carries on along the route.
				halt_for_engagement(horse, time);
				order.target.reset();
			}
			// A Defensive patrol keeps the stationary Defensive leash, measured
			// from where it left the route.
			if (stance == HunterStance::DEFENSIVE and order.patrol_engagement
			    and (distance_squared(current, *order.patrol_engagement) > HORSE_LEASH_RANGE_SQUARED
			         or distance_squared(enemy_at, *order.patrol_engagement) > HORSE_LEASH_RANGE_SQUARED)) {
				halt_for_engagement(horse, time);
				order.target.reset();
			}
		}
		if (defending and order.target) {
			// Give up once the fight drifts too far from the guarded point;
			// the horse then walks back, still defending on the way.
			const auto enemy_at = position_at(state->get_game_entity(*order.target), time);
			if (distance_squared(current, order.destination) > HORSE_LEASH_RANGE_SQUARED
			    or distance_squared(enemy_at, order.destination) > HORSE_LEASH_RANGE_SQUARED) {
				halt_for_engagement(horse, time);
				order.target.reset();
			}
		}
		if (order.target) {
			const auto target = *order.target;
			auto enemy = state->get_game_entities().find(target);
			if (enemy == state->get_game_entities().end()) {
				order.target.reset();
				continue;
			}
			auto distance = distance_squared(horse, enemy->second, time);
			if (distance > HORSE_ATTACK_RANGE_SQUARED) {
				// Pursue only until the target enters firing range.
				// A moving Builder may require another approach after
				// the current route reaches its last-known position.
				if (not move->get_active_target()
				    or move->get_active_arrival() <= time) {
					auto approach = ranged_approach(
						state, horse, enemy->second, time, this->map_size);
					if (approach) {
						system::Move::move_default(horse, state, *approach, time);
					}
					else {
						order.target.reset();
					}
				}
				continue;
			}

			// A horse may enter firing range before its approach curve ends.
			if (move->get_active_target() and move->get_active_arrival() > time) {
				halt_for_engagement(horse, time);
			}
			if (time >= order.next_attack) {
				auto enemy_position = std::dynamic_pointer_cast<component::Position>(
					enemy->second->get_component(component::component_t::POSITION));
				auto direction = enemy_position->get_positions().get(time) - current;
				position->set_angle(time, direction.to_angle());
				horse->render_update(
					time, this->horse_attack_animation, AnimationAction::ATTACK);
				auto loop = this->event_loop.lock();
				if (not loop) [[unlikely]] {
					throw Error{ERR << "Cannot schedule an FVD Hunter hit without an event loop"};
				}
				const auto hit_time = time + this->horse_hit_delay;
				order.pending_hit = hit_time;
				// The Hunter begins the attack animation now and schedules
				// its impact at 75% of the 0.6-second attack cycle. Publish
				// one immutable, timed launch to all recipients immediately.
				// Its flight and spark are cosmetic: only horse_hit applies HP.
				auto start = current;
				start.up += coord::phys_t::from_double(0.90);
				auto destination = enemy_position->get_positions().get(hit_time);
				destination.up += coord::phys_t::from_double(
					enemy->second->get_definition() == this->tower ? 1.0 : 0.55);
				{
					std::lock_guard lock{this->arrow_visuals_mutex};
					if (not this->projectile_replication_enabled) {
						std::erase_if(this->arrow_visuals, [&](const ArrowVisual &shot) {
							return shot.impact_time + time::time_t::from_int(3) <= time;
						});
					}
					// An undelivered launch may no longer be silently evicted.
					// If every recipient stalls for an extreme duration,
					// fail explicitly rather than pretend that all effects arrived.
					if (this->arrow_visuals.size() >= 65536) {
						throw Error{ERR << "FVD projectile backlog exceeded 65536 unsent records"};
					}
					this->arrow_visuals.push_back(
						ArrowVisual{this->next_arrow_id++, player_id_t{1}, target,
						            start, destination, time, hit_time});
				}
				loop->create_event(
					"game.fvd_horse_hit", this->horse_spawn_target, state, hit_time,
					{{"horse", id}, {"target", target}, {"damage", this->horse_damage}});
				order.next_attack = time + this->horse_cooldown;
			}
			continue;
		}
		if (horse->get_animation_action() == AnimationAction::ATTACK) {
			system::Idle::idle(horse, time);
		}

		// With nothing left to fight, an Aggressive horse searches again
		// from where it stands, and a Defensive horse returns to its post.
		if (order.stance_engagement) {
			if (defending and remaining.length() >= 0.15) {
				if (not travelling(horse, time)) {
					system::Move::move_default(horse, state, order.destination, time);
				}
				continue;
			}
			if (move->get_active_target()) {
				system::Idle::idle(horse, time);
			}
			this->attack_orders.erase(order_it);
			continue;
		}

		// A Defensive patrol walks back to where it left its route first.
		if (order.patrol_engagement) {
			if ((current - *order.patrol_engagement).length() >= 0.15) {
				if (not travelling(horse, time)) {
					system::Move::move_default(horse, state, *order.patrol_engagement, time);
				}
				continue;
			}
			order.patrol_engagement.reset();
		}

		// After reaching the destination, remain alert to enemies within
		// four tiles until a human issues another command, unless a
		// non-Observe stance takes over from here.
		if (order.reached_destination) {
			if (move->get_active_target()) {
				system::Idle::idle(horse, time);
			}
			if (stance != HunterStance::OBSERVE) {
				this->attack_orders.erase(order_it);
			}
			continue;
		}
		if (remaining.length() < 0.15) {
			if (order.patrol_return) {
				std::swap(order.destination, *order.patrol_return);
				system::Move::move_default(horse, state, order.destination, time);
			}
			else {
				order.reached_destination = true;
				system::Idle::idle(horse, time);
			}
		}
		else if (not move->get_active_target()
		         or move->get_active_arrival() <= time) {
			system::Move::move_default(horse, state, order.destination, time);
		}
	}

	// The Hunter's invulnerable central tower shoots the nearest Builder unit
	// in range and in sight, never a tower. It stays silent until the first
	// horse is due, so Builders keep their 20-second head start.
	auto central = this->hunter_tower_id ? state->get_game_entities().find(*this->hunter_tower_id)
	                                     : state->get_game_entities().end();
	if (central != state->get_game_entities().end() and this->hunter_tower_active_from
	    and time >= *this->hunter_tower_active_from and time >= this->hunter_tower_next_shot) {
		static const std::vector<entity_id_t> no_towers{};
		const auto target = this->nearest_hunter_target(
			state, central->second, no_towers, builders, this->hunter_tower_range,
			std::nullopt, 0.0, time);
		if (target) {
			auto loop = this->event_loop.lock();
			if (not loop) [[unlikely]] {
				throw Error{ERR << "Cannot schedule a Hunter tower shot without an event loop"};
			}
			auto start = position_at(central->second, time);
			start.up += coord::phys_t::from_double(1.3);
			auto destination = position_at(state->get_game_entity(*target), time);
			destination.up += coord::phys_t::from_double(0.55);
			const auto impact = time + this->arrow_travel;
			{
				std::lock_guard lock{this->arrow_visuals_mutex};
				if (this->arrow_visuals.size() >= 65536) {
					throw Error{ERR << "FVD projectile backlog exceeded 65536 unsent records"};
				}
				this->arrow_visuals.push_back(
					ArrowVisual{this->next_arrow_id++, player_id_t{1}, *target,
					            start, destination, time, impact});
			}
			loop->create_event("game.fvd_hunter_tower_hit", this->horse_spawn_target, state, impact,
			                   {{"target", *target}, {"damage", this->hunter_tower_damage}});
			this->hunter_tower_next_shot = time + this->hunter_tower_cooldown;
		}
	}

	// Completed towers fire automatically, with a deterministic target order.
	for (auto tower_id : towers) {
		auto tower_it = state->get_game_entities().find(tower_id);
		if (tower_it == state->get_game_entities().end()
		    or not alive(tower_it->second, time)) {
			continue;
		}
		auto footprint = this->tower_footprints.find(tower_id);
		if (footprint == this->tower_footprints.end() or footprint->second.under_construction) {
			continue;
		}
		auto ready = this->tower_next_shot.find(tower_id);
		if (ready != this->tower_next_shot.end() and time < ready->second) {
			continue;
		}
		entity_id_t nearest = 0;
		double closest = 7.0 * 7.0;
		for (auto horse_id : horses) {
			auto horse_it = state->get_game_entities().find(horse_id);
			if (horse_it == state->get_game_entities().end()
			    or not alive(horse_it->second, time)
			    or not this->can_see(state, FVDTeam::BUILDERS, horse_id, time)) {
				continue;
			}
			const double d = distance_squared(tower_it->second, horse_it->second, time);
			if (d < closest) {
				closest = d;
				nearest = horse_id;
			}
		}
		if (nearest == 0) {
			continue;
		}
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			tower_it->second->get_component(component::component_t::OWNERSHIP));
		const auto owner = ownership->get_owners().get(time);
		auto loop = this->event_loop.lock();
		// Add a visual trail for the existing authoritative arrow shot.
		// Copy both endpoints at launch so the arrow stays visible even
		// if its target moves or dies before the scheduled impact.
		auto start = std::dynamic_pointer_cast<component::Position>(
			tower_it->second->get_component(component::component_t::POSITION))
			->get_positions().get(time);
		auto destination = std::dynamic_pointer_cast<component::Position>(
			state->get_game_entity(nearest)->get_component(component::component_t::POSITION))
			->get_positions().get(time);
		start.up += coord::phys_t::from_double(1.3);
		destination.up += coord::phys_t::from_double(0.45);
		{
			std::lock_guard lock{this->arrow_visuals_mutex};
			if (not this->projectile_replication_enabled) {
				std::erase_if(this->arrow_visuals, [&](const ArrowVisual &shot) {
					return shot.impact_time + time::time_t::from_int(3) <= time;
				});
			}
			if (this->arrow_visuals.size() >= 65536) {
				throw Error{ERR << "FVD projectile backlog exceeded 65536 unsent records"};
			}
			this->arrow_visuals.push_back(
				ArrowVisual{this->next_arrow_id++, owner, nearest, start, destination,
				            time, time + this->arrow_travel});
		}
		loop->create_event("game.fvd_arrow_hit", this->horse_spawn_target, state,
		                   time + this->arrow_travel,
		                   {{"target", nearest}, {"owner", owner}, {"firing_tower", tower_id},
		                    {"damage", this->tower_damage}});
		this->tower_next_shot.insert_or_assign(tower_id, time + this->tower_cooldown);
	}
	this->vision_cached_at.reset();
}

} // namespace openage::gamestate
