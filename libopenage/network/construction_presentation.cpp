// Copyright 2026 the openage authors. See copying.md for legal info.

#include "construction_presentation.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "error/error.h"

namespace openage::network {

ConstructionPresentation make_construction_presentation(const WorldState &world) {
	ConstructionPresentation result{};
	std::unordered_map<gamestate::entity_id_t, gamestate::player_id_t> builders{};
	for (const auto &entity : world.entities) {
		if (entity.definition == "fvd.data.entities.Builder") {
			if (entity.owner_id < 2
			    or not builders.emplace(entity.entity_id, entity.owner_id).second) {
				throw Error{MSG(err) << "FVD presentation has invalid Builder ownership"};
			}
		}
	}

	using tile_key_t = std::pair<coord::tile_t, coord::tile_t>;
	auto key = [](const coord::tile &tile) {
		return tile_key_t{tile.ne, tile.se};
	};
	std::map<tile_key_t, gamestate::entity_id_t> reserved_by{};
	result.reservations.reserve(world.reservations.size());
	for (const auto &reservation : world.reservations) {
		auto builder = builders.find(reservation.builder_id);
		if (builder == builders.end()
		    or not reserved_by.emplace(key(reservation.tile), reservation.builder_id).second) {
			throw Error{MSG(err) << "FVD presentation has invalid reserved tile"};
		}
		result.reservations.push_back({reservation.tile, builder->second});
	}

	std::unordered_map<gamestate::entity_id_t, const ConstructionState *> active{};
	for (const auto &construction : world.constructions) {
		if (construction.site_entity_id == 0
		    or not active.emplace(construction.site_entity_id, &construction).second) {
			throw Error{MSG(err) << "FVD presentation has duplicate construction site"};
		}
	}

	result.sites.reserve(world.entities.size());
	for (const auto &entity : world.entities) {
		const bool foundation = entity.definition == "fvd.data.entities.Foundation";
		const bool tower = entity.definition == "fvd.data.entities.MedievalTowerLevel1"
		                  or entity.definition == "fvd.data.entities.MedievalTowerLevel2";
		if (not foundation and not tower) {
			continue;
		}
		auto site = entity.position.to_tile();
		auto order = active.find(entity.entity_id);
		if (order != active.end()) {
			const auto &construction = *order->second;
			if (entity.owner_id != construction.owner_id
			    or site != construction.target
			    or (foundation and construction.phase != ConstructionPhase::PLANNED)
			    or (tower and construction.phase != ConstructionPhase::UNDER_CONSTRUCTION)
			    or not builders.contains(construction.builder_id)
			    or builders.at(construction.builder_id) != entity.owner_id
			    or not reserved_by.contains(key(site))
			    or reserved_by.at(key(site)) != construction.builder_id) {
				throw Error{MSG(err) << "FVD presentation construction disagrees with world"};
			}
			result.sites.push_back({
				entity.entity_id, entity.owner_id, construction.builder_id, site,
				foundation ? SitePresentationPhase::PLANNED
				           : SitePresentationPhase::BUILDING});
			active.erase(order);
		}
		else if (foundation) {
			// A queued foundation retains its entity and center-tile reservation.
			const auto reserved = reserved_by.find(key(site));
			if (reserved == reserved_by.end()) {
				result.sites.push_back({
					entity.entity_id, entity.owner_id, 0, site,
					SitePresentationPhase::ORPHANED});
				continue;
			}
			const auto builder = builders.find(reserved->second);
			if (builder == builders.end() or builder->second != entity.owner_id) {
				throw Error{MSG(err) << "FVD queued foundation has a different owner"};
			}
			result.sites.push_back({
				entity.entity_id, entity.owner_id, builder->first, site,
				SitePresentationPhase::QUEUED});
		}
		else {
			result.sites.push_back({
				entity.entity_id, entity.owner_id, 0, site,
				SitePresentationPhase::COMPLETE});
		}
	}
	if (not active.empty()) {
		throw Error{MSG(err) << "FVD presentation is missing an active construction entity"};
	}
	std::sort(result.sites.begin(), result.sites.end(),
	          [](const auto &a, const auto &b) { return a.entity_id < b.entity_id; });
	std::unordered_map<gamestate::entity_id_t, std::vector<size_t>> queues{};
	for (size_t index = 0; index < result.sites.size(); ++index) {
		const auto &site = result.sites[index];
		if (site.phase == SitePresentationPhase::QUEUED
		    or site.phase == SitePresentationPhase::PLANNED
		    or site.phase == SitePresentationPhase::BUILDING) {
			queues[site.builder_id].push_back(index);
		}
	}
	for (auto &[builder, entries] : queues) {
		std::stable_sort(entries.begin(), entries.end(), [&](size_t a, size_t b) {
			const bool queued_a = result.sites[a].phase == SitePresentationPhase::QUEUED;
			const bool queued_b = result.sites[b].phase == SitePresentationPhase::QUEUED;
			if (queued_a != queued_b) return not queued_a;
			return result.sites[a].entity_id < result.sites[b].entity_id;
		});
		for (size_t index = 0; index < entries.size(); ++index) {
			result.sites[entries[index]].queue_position = static_cast<uint32_t>(index + 1);
		}
	}
	return result;
}

} // namespace openage::network
