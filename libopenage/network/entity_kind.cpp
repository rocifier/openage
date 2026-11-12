// Copyright 2026 the openage authors. See copying.md for legal info.

#include "entity_kind.h"

#include <algorithm>
#include <array>

#include "error/error.h"


namespace openage::network {

namespace {

struct KindDefinition {
	EntityKind kind;
	std::string definition;
};

const std::array<KindDefinition, 8> &kind_definitions() {
	static const std::array<KindDefinition, 8> definitions{{
		{EntityKind::BUILDER, "fvd.data.entities.Builder"},
		{EntityKind::HORSE, "fvd.data.entities.Horse"},
		{EntityKind::FOUNDATION, "fvd.data.entities.Foundation"},
		{EntityKind::WOOD_TOWER, "fvd.data.entities.MedievalTowerLevel1"},
		{EntityKind::STONE_TOWER, "fvd.data.entities.MedievalTowerLevel2"},
		{EntityKind::TOWER_DEMOLITION, "fvd.data.entities.TowerDemolition"},
		{EntityKind::HUNTER_TOWER, "fvd.data.entities.HunterTower"},
		{EntityKind::HUNTER_RALLY_POINT, "fvd.data.entities.HunterRallyPoint"},
	}};
	return definitions;
}

} // namespace


const std::string &definition_of(EntityKind kind) {
	const auto &definitions = kind_definitions();
	const auto found = std::find_if(definitions.begin(), definitions.end(), [kind](const KindDefinition &entry) {
		return entry.kind == kind;
	});
	if (found == definitions.end()) {
		throw Error{MSG(err) << "FVD entity kind " << static_cast<int>(kind)
		                     << " has no nyan definition"};
	}
	return found->definition;
}

EntityKind classify(std::string_view definition) {
	const auto &definitions = kind_definitions();
	const auto found = std::find_if(definitions.begin(), definitions.end(), [definition](const KindDefinition &entry) {
		return entry.definition == definition;
	});
	return found == definitions.end() ? EntityKind::UNKNOWN : found->kind;
}

bool is_tower(EntityKind kind) {
	return kind == EntityKind::WOOD_TOWER or kind == EntityKind::STONE_TOWER;
}

bool is_tower_definition(std::string_view definition) {
	return is_tower(classify(definition));
}

bool is_unit(EntityKind kind) {
	return kind == EntityKind::BUILDER or kind == EntityKind::HORSE;
}

} // namespace openage::network
