// Copyright 2026 the openage authors. See copying.md for legal info.

#include "fvd.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "error/error.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"

namespace openage::gamestate {

namespace {

constexpr const char *HEALTH = "fvd.data.entities.Health";

size_t team_index(FVDTeam team) {
	return static_cast<size_t>(team) - 1;
}

bool test_bit(const std::vector<uint64_t> &bits, size_t index) {
	return (bits[index / 64] >> (index % 64)) & 1;
}

void set_bit(std::vector<uint64_t> &bits, size_t index) {
	bits[index / 64] |= uint64_t{1} << (index % 64);
}

} // namespace

FVDTeam FVD::team_of(player_id_t player) {
	return player == 1 ? FVDTeam::HUNTER : FVDTeam::BUILDERS;
}

double FVD::vision_range(const std::shared_ptr<GameState> &state,
                         const std::string &definition) const {
	auto cached = this->vision_ranges.find(definition);
	if (cached != this->vision_ranges.end()) {
		return cached->second;
	}
	double range = 0.0;
	auto db = state->get_db_view();
	for (const auto &ability : db->get_object(definition).get_set("GameEntity.abilities")) {
		auto ability_object = db->get_object(
			std::dynamic_pointer_cast<nyan::ObjectValue>(ability.get_ptr())->get_name());
		if (ability_object.get_parents()[0] != "engine.ability.type.LineOfSight") {
			continue;
		}
		range = ability_object.get_float("LineOfSight.range");
		if (range < 1.0 or range > 32.0) {
			throw Error{ERR << "Invalid FVD LineOfSight range " << range << " for "
			                << definition << ": expected 1..32 tiles"};
		}
	}
	this->vision_ranges.emplace(definition, range);
	return range;
}

std::vector<std::pair<coord::phys3, double>> FVD::vision_sources(
	const std::shared_ptr<GameState> &state, FVDTeam team, const time::time_t &time) const {
	std::vector<std::pair<coord::phys3, double>> sources;
	for (const auto &[id, entity] : state->get_game_entities()) {
		const auto &definition = entity->get_definition();
		if (this->is_garrisoned(id)) continue;
		const double radius = this->vision_range(state, definition);
		if (radius <= 0.0) {
			continue;
		}
		// A tower only watches the map once it is built.
		if (definition == this->tower) {
			auto footprint = this->tower_footprints.find(id);
			if (footprint == this->tower_footprints.end() or footprint->second.under_construction) {
				continue;
			}
		}
		if (not entity->has_component(component::component_t::OWNERSHIP)
		    or not entity->has_component(component::component_t::LIVE)) {
			continue;
		}
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		auto live = std::dynamic_pointer_cast<component::Live>(
			entity->get_component(component::component_t::LIVE));
		if (team_of(ownership->get_owners().get(time)) != team
		    or live->get_attribute(time, HEALTH)->get(time) <= 0) {
			continue;
		}
		auto position = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		sources.emplace_back(position->get_positions().get(time), radius);
	}
	return sources;
}

bool FVD::tile_visible(const std::shared_ptr<GameState> &state, FVDTeam team,
                       const coord::tile &tile, const time::time_t &time) const {
	if (tile.ne < 0 or tile.se < 0
	    or tile.ne >= static_cast<coord::tile_t>(this->map_size)
	    or tile.se >= static_cast<coord::tile_t>(this->map_size)) {
		return false;
	}
	// The combat tick refreshes the bitsets first, so its many queries are cheap.
	if (this->vision_cached_at == time) {
		const auto index = static_cast<size_t>(tile.ne) * this->map_size + tile.se;
		return test_bit(this->team_vision[team_index(team)].visible, index);
	}
	const auto centre = tile.to_phys3_center();
	for (const auto &[source, radius] : this->vision_sources(state, team, time)) {
		const auto delta = centre - source;
		const double ne = delta.ne.to_double();
		const double se = delta.se.to_double();
		if (ne * ne + se * se <= radius * radius) {
			return true;
		}
	}
	return false;
}

bool FVD::can_see(const std::shared_ptr<GameState> &state, FVDTeam team,
                  entity_id_t entity_id, const time::time_t &time) const {
	auto found = state->get_game_entities().find(entity_id);
	if (found == state->get_game_entities().end()
	    or not found->second->has_component(component::component_t::POSITION)) {
		return false;
	}
	if (this->is_garrisoned(entity_id)) return false;
	const auto &entity = found->second;
	if (entity->has_component(component::component_t::OWNERSHIP)) {
		auto ownership = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		const auto owner = ownership->get_owners().get(time);
		if (owner != 0 and team_of(owner) == team) {
			return true;
		}
	}
	auto position = std::dynamic_pointer_cast<component::Position>(
		entity->get_component(component::component_t::POSITION));
	return this->tile_visible(state, team, position->get_positions().get(time).to_tile(), time);
}

void FVD::update_vision(const std::shared_ptr<GameState> &state, const time::time_t &time) {
	const auto size = static_cast<coord::tile_t>(this->map_size);
	for (auto team : {FVDTeam::HUNTER, FVDTeam::BUILDERS}) {
		auto &vision = this->team_vision[team_index(team)];
		std::fill(vision.visible.begin(), vision.visible.end(), 0);
		for (const auto &[source, radius] : this->vision_sources(state, team, time)) {
			const double source_ne = source.ne.to_double();
			const double source_se = source.se.to_double();
			const auto reach = static_cast<coord::tile_t>(std::ceil(radius));
			const auto centre = source.to_tile();
			for (auto ne = std::max<coord::tile_t>(0, centre.ne - reach);
			     ne <= std::min<coord::tile_t>(size - 1, centre.ne + reach); ++ne) {
				for (auto se = std::max<coord::tile_t>(0, centre.se - reach);
				     se <= std::min<coord::tile_t>(size - 1, centre.se + reach); ++se) {
					const double dne = ne + 0.5 - source_ne;
					const double dse = se + 0.5 - source_se;
					if (dne * dne + dse * dse <= radius * radius) {
						const auto index = static_cast<size_t>(ne) * this->map_size + se;
						set_bit(vision.visible, index);
						set_bit(vision.explored, index);
					}
				}
			}
		}

		// Remember enemy buildings while seen, and forget them once their
		// tile is seen again without them.
		auto visible_at = [&](const coord::phys3 &position) {
			const auto tile = position.to_tile();
			return tile.ne >= 0 and tile.se >= 0 and tile.ne < size and tile.se < size
			   and test_bit(vision.visible, static_cast<size_t>(tile.ne) * this->map_size + tile.se);
		};
		for (const auto &[id, entity] : state->get_game_entities()) {
			const auto &definition = entity->get_definition();
			if ((definition != this->tower and definition != this->foundation
			     and definition != this->hunter_tower)
			    or not entity->has_component(component::component_t::OWNERSHIP)) {
				continue;
			}
			auto ownership = std::dynamic_pointer_cast<component::Ownership>(
				entity->get_component(component::component_t::OWNERSHIP));
			const auto owner = ownership->get_owners().get(time);
			auto position = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION))->get_positions().get(time);
			if (team_of(owner) == team or not visible_at(position)) {
				continue;
			}
			vision.remembered.insert_or_assign(id, FVDRememberedBuilding{id, owner, definition, position});
		}
		std::erase_if(vision.remembered, [&](const auto &entry) {
			return visible_at(entry.second.position)
			   and not state->get_game_entities().contains(entry.first);
		});
	}
}

FVDTeamVision FVD::get_vision(FVDTeam team) const {
	const auto &vision = this->team_vision[team_index(team)];
	FVDTeamVision result{vision.explored, vision.visible, {}};
	result.remembered.reserve(vision.remembered.size());
	for (const auto &[id, building] : vision.remembered) {
		result.remembered.push_back(building);
	}
	return result;
}

} // namespace openage::gamestate
