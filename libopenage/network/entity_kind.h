// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <string>
#include <string_view>


namespace openage::network {

/**
 * FVD game entity kinds that clients reason about.
 *
 * Replicated entities carry their nyan definition name. This vocabulary is
 * the single mapping between those names and the kinds that presenter,
 * input and network code distinguish; the FVD scenario is validated
 * against it when a match starts.
 */
enum class EntityKind {
	UNKNOWN,
	BUILDER,
	HORSE,
	FOUNDATION,
	WOOD_TOWER,
	STONE_TOWER,
	TOWER_DEMOLITION,
	HUNTER_TOWER,
	HUNTER_RALLY_POINT,
};

/**
 * Get the nyan definition name of an entity kind.
 *
 * @param kind Known entity kind.
 *
 * @return Fully qualified nyan object name.
 *
 * @throws Error if \p kind is EntityKind::UNKNOWN.
 */
const std::string &definition_of(EntityKind kind);

/**
 * Classify a nyan definition name.
 *
 * @param definition Fully qualified nyan object name.
 *
 * @return Matching entity kind, or EntityKind::UNKNOWN for any other name.
 */
EntityKind classify(std::string_view definition);

/**
 * Check whether a kind is one of the Builder tower tiers.
 */
bool is_tower(EntityKind kind);

/**
 * Check whether a definition names one of the Builder tower tiers.
 */
bool is_tower_definition(std::string_view definition);

/**
 * Check whether a kind is a movable unit, i.e. a Builder or a Horse.
 */
bool is_unit(EntityKind kind);

} // namespace openage::network
