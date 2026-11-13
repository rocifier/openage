// Copyright 2026 the openage authors. See copying.md for legal info.

#include "entity_kind.h"

#include <set>
#include <string>

#include "network/messages.h"
#include "testing/testing.h"

namespace openage::network::tests {

void entity_kind() {
	const EntityKind known[] = {
		EntityKind::BUILDER,
		EntityKind::HORSE,
		EntityKind::ESCAPE_CART,
		EntityKind::FOUNDATION,
		EntityKind::WOOD_TOWER,
		EntityKind::STONE_TOWER,
		EntityKind::TOWER_DEMOLITION,
		EntityKind::HUNTER_TOWER,
		EntityKind::HUNTER_RALLY_POINT,
	};
	std::set<std::string> definitions{};
	for (const auto kind : known) {
		const auto &definition = definition_of(kind);
		TESTEQUALS(classify(definition) == kind, true);
		TESTEQUALS(definitions.insert(definition).second, true);

		EntityState entity{};
		entity.definition = definition;
		TESTEQUALS(entity.kind() == kind, true);
	}
	TESTTHROWS(definition_of(EntityKind::UNKNOWN));

	TESTEQUALS(definition_of(EntityKind::WOOD_TOWER), "fvd.data.entities.MedievalTowerLevel1");
	TESTEQUALS(definition_of(EntityKind::STONE_TOWER), "fvd.data.entities.MedievalTowerLevel2");

	// The pre-tier tower name, ability paths and near-misses are not entities.
	for (const auto *other : {"", "fvd.data.entities.Tower", "fvd.data.entities.Builder.Idle", "fvd.data.entities.builder", "fvd.data.entities.Health", "Builder"}) {
		TESTEQUALS(classify(other) == EntityKind::UNKNOWN, true);
		TESTEQUALS(is_tower_definition(other), false);
	}
	TESTEQUALS(EntityState{}.kind() == EntityKind::UNKNOWN, true);

	for (const auto kind : known) {
		const bool tower = kind == EntityKind::WOOD_TOWER or kind == EntityKind::STONE_TOWER;
		const bool unit = kind == EntityKind::BUILDER or kind == EntityKind::HORSE or kind == EntityKind::ESCAPE_CART;
		TESTEQUALS(is_tower(kind), tower);
		TESTEQUALS(is_tower_definition(definition_of(kind)), tower);
		TESTEQUALS(is_unit(kind), unit);
	}
	TESTEQUALS(is_tower(EntityKind::UNKNOWN), false);
	TESTEQUALS(is_unit(EntityKind::UNKNOWN), false);
}

} // namespace openage::network::tests
