// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "network/messages.h"

namespace openage::network {

/**
 * A WorldDelta contains changed records and removals for persistent objects.
 * Arrows have a known end time and expire locally without network tombstones.
 * Record sets use stable identities; headers and score updates are authoritative.
 */
struct WorldDelta {
	WorldState changed{};
	bool local_kills_changed{false};
	bool first_hunter_spawn_time_changed{false};
	std::vector<gamestate::player_id_t> removed_players{};
	std::vector<gamestate::entity_id_t> removed_entities{};
	std::vector<coord::tile> removed_reservations{};
	std::vector<uint64_t> removed_constructions{};
	std::vector<gamestate::entity_id_t> removed_attack_orders{};
};

/** An arrow is a timed visual: no new network data is needed for expiry. */
bool arrow_visible_at(const ArrowState &arrow, const time::time_t &time);

/** Evaluate a transmitted movement curve without running gameplay systems. */
coord::phys3 position_at(const EntityState &entity,
	                     time::time_t origin,
	                     time::time_t now);

/** Compare two complete, recipient-specific states and produce a true patch. */
WorldDelta make_world_delta(const WorldState &previous, const WorldState &current);

/** Visual feedback from confirmed authoritative HP changes and removals.
 * Never infer hit/death effects from projectile trajectories. */
struct ConfirmedCombatEffect {
	gamestate::entity_id_t target_id;
	gamestate::player_id_t owner_id;
	std::string definition;
	coord::phys3 position;
	coord::phys_angle_t angle;
	bool death;
};

std::vector<ConfirmedCombatEffect> confirmed_combat_effects(
	const WorldState &previous, const WorldState &current);

/** Apply a contiguous patch, validating identity and all removals atomically. */
WorldState apply_world_delta(const WorldState &previous, const WorldDelta &delta);

/** Distinct wire format for WorldDelta (not the full snapshot encoding). */
std::vector<uint8_t> encode_world_delta(const WorldDelta &delta);
WorldDelta decode_world_delta(const std::vector<uint8_t> &payload);

/** Host-side per-recipient baseline. Seed on snapshots and advance on deltas. */
class WorldDeltaBaseline {
public:
	void accept_snapshot(WorldState snapshot);
	WorldDelta advance(WorldState complete);
	bool initialized() const;

	/** Latest full recipient world used as the outbound delta baseline. */
	const WorldState &current_snapshot() const;

private:
	std::optional<WorldState> previous{};
};

} // namespace openage::network
