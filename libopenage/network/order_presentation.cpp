// Copyright 2026 the openage authors. See copying.md for legal info.

#include "order_presentation.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

#include "error/error.h"
#include "network/world_delta.h"

namespace openage::network {

std::vector<AttackOrderPresentation> make_attack_order_presentation(
	const WorldState &world,
	const time::time_t &presentation_time) {
	if (world.attack_orders.empty()) {
		return {};
	}

	std::unordered_set<gamestate::entity_id_t> referenced_entities{};
	referenced_entities.reserve(world.attack_orders.size() * 2);
	for (const auto &order : world.attack_orders) {
		referenced_entities.insert(order.horse_id);
		if (order.target_id) {
			referenced_entities.insert(*order.target_id);
		}
	}

	std::unordered_map<gamestate::entity_id_t, const EntityState *> entities{};
	entities.reserve(referenced_entities.size());
	for (const auto &entity : world.entities) {
		if (not referenced_entities.contains(entity.entity_id)) {
			continue;
		}
		if (entity.entity_id == 0
		    or not entities.emplace(entity.entity_id, &entity).second) {
			throw Error{MSG(err) << "FVD order presentation has invalid entity identity"};
		}
	}

	std::unordered_set<gamestate::entity_id_t> horses{};
	std::vector<AttackOrderPresentation> result{};
	result.reserve(world.attack_orders.size());
	for (const auto &order : world.attack_orders) {
		auto horse = entities.find(order.horse_id);
		if (not horses.insert(order.horse_id).second
		    or horse == entities.end()
		    or horse->second->owner_id != 1
		    or horse->second->kind() != EntityKind::HORSE
		    or (order.focus_target and not order.target_id)) {
			throw Error{MSG(err) << "FVD order presentation has an invalid Hunter order"};
		}
		auto marker_position = order.destination;
		if (order.target_id) {
			auto target = entities.find(*order.target_id);
			if (*order.target_id == 0 or target == entities.end()) {
				throw Error{MSG(err) << "FVD order presentation is missing an attack target"};
			}
			marker_position = target->second->position;
		}
		result.push_back({
			order.horse_id,
			position_at(*horse->second, world.simulation_time, presentation_time),
			marker_position,
			order.target_id,
			order.focus_target,
			order.reached_destination,
		});
		if (order.target_id) {
			result.back().marker_position = position_at(
				*entities.at(*order.target_id), world.simulation_time, presentation_time);
		}
	}
	std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
		return a.horse_id < b.horse_id;
	});
	return result;
}

} // namespace openage::network
