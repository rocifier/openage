// Copyright 2026 the openage authors. See copying.md for legal info.

#include "world_delta.h"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <type_traits>
#include <utility>

#include "error/error.h"
#include "network/serialization.h"

namespace openage::network {

coord::phys3 position_at(const EntityState &entity,
                         time::time_t origin, time::time_t now) {
	auto from = entity.position;
	auto from_time = origin;
	if (now <= origin) return from;
	for (const auto &key : entity.motion_keys) {
		if (now <= key.time) {
			const auto span = key.time - from_time;
			if (span <= time::TIME_ZERO) return key.position;
			return from + (key.position - from) / span * (now - from_time);
		}
		from = key.position;
		from_time = key.time;
	}
	return from;
}

namespace {

using player_id = gamestate::player_id_t;
using entity_id = gamestate::entity_id_t;

auto player_key = [](const PlayerState &r) { return r.player_id; };
auto entity_key = [](const EntityState &r) { return r.entity_id; };
auto reservation_key = [](const TileReservation &r) { return r.tile; };
auto construction_key = [](const ConstructionState &r) { return r.order_id; };
auto attack_key = [](const AttackOrderState &r) { return r.horse_id; };
auto arrow_key = [](const ArrowState &r) { return r.arrow_id; };

bool same(const PlayerState &a, const PlayerState &b) {
	return a.player_id == b.player_id and a.role == b.role and a.connected == b.connected;
}
coord::phys_angle_t facing_at(const EntityState &entity, time::time_t now) {
	auto angle = entity.angle;
	for (const auto &key : entity.facing_keys) {
		if (key.time > now) break;
		angle = key.angle;
	}
	return angle;
}

bool same_facing_at(const EntityState &before, const EntityState &after,
                    time::time_t now) {
	if (after.angle != facing_at(before, now)) return false;
	size_t remaining = 0;
	for (const auto &key : before.facing_keys) {
		if (key.time <= now) continue;
		if (remaining >= after.facing_keys.size()
		    or after.facing_keys[remaining].time != key.time
		    or after.facing_keys[remaining].angle != key.angle) return false;
		++remaining;
	}
	return remaining == after.facing_keys.size();
}

bool same_motion_at(const EntityState &before, const EntityState &after,
                    time::time_t before_time, time::time_t after_time) {
	if (after.position != position_at(before, before_time, after_time)) return false;
	size_t remaining = 0;
	for (const auto &key : before.motion_keys) {
		if (key.time <= after_time) continue;
		if (remaining >= after.motion_keys.size()
		    or key.time != after.motion_keys[remaining].time
		    or key.position != after.motion_keys[remaining].position) return false;
		++remaining;
	}
	return remaining == after.motion_keys.size();
}

void difference_entities(const WorldState &before, const WorldState &after,
                         std::vector<EntityState> &changed,
                         std::vector<entity_id> &removed) {
	std::unordered_map<entity_id, const EntityState *> old{};
	std::unordered_set<entity_id> seen{};
	for (const auto &entry : before.entities) {
		if (not old.emplace(entry.entity_id, &entry).second) {
			throw Error{MSG(err) << "Duplicate entity identity in FVD previous world state"};
		}
	}
	for (const auto &entry : after.entities) {
		if (not seen.insert(entry.entity_id).second) {
			throw Error{MSG(err) << "Duplicate entity identity in FVD next world state"};
		}
		const auto it = old.find(entry.entity_id);
		if (it == old.end()) {
			changed.push_back(entry);
			continue;
		}
		const auto &previous = *it->second;
		// Compare properties independently of the interpolated position.
		// A new order, damage or owning player still generates a patch.
		if (previous.owner_id != entry.owner_id
		    or previous.definition != entry.definition
		    or previous.health != entry.health
		    or previous.maximum_health != entry.maximum_health
		    or previous.tower_kills != entry.tower_kills
		    or previous.hunter_stance != entry.hunter_stance
		    or previous.garrisoned_builder != entry.garrisoned_builder
		    or previous.ungarrison_source_tower != entry.ungarrison_source_tower
		    or previous.remembered != entry.remembered
		    or previous.animation_action != entry.animation_action
		    or previous.animation_started_at != entry.animation_started_at
		    or not same_facing_at(previous, entry, after.simulation_time)
		    or not same_motion_at(previous, entry, before.simulation_time,
		                          after.simulation_time)) {
			changed.push_back(entry);
		}
	}
	for (const auto &entry : before.entities) {
		if (not seen.contains(entry.entity_id)) {
			removed.push_back(entry.entity_id);
		}
	}
}

bool same(const TileReservation &a, const TileReservation &b) {
	return a.tile == b.tile and a.builder_id == b.builder_id;
}
bool same(const ConstructionState &a, const ConstructionState &b) {
	return a.order_id == b.order_id and a.owner_id == b.owner_id
	    and a.builder_id == b.builder_id and a.target == b.target
	    and a.phase == b.phase and a.phase_end_time == b.phase_end_time
	    and a.phase_start_time == b.phase_start_time
	    and a.site_entity_id == b.site_entity_id;
}
bool same(const AttackOrderState &a, const AttackOrderState &b) {
	return a.horse_id == b.horse_id and a.destination == b.destination
	    and a.target_id == b.target_id and a.reached_destination == b.reached_destination
	    and a.focus_target == b.focus_target;
}
bool same(const ArrowState &a, const ArrowState &b) {
	return a.arrow_id == b.arrow_id and a.owner_id == b.owner_id
	    and a.target_id == b.target_id and a.start == b.start
	    and a.destination == b.destination and a.launch_time == b.launch_time
	    and a.impact_time == b.impact_time;
}

template<typename Record, typename Key, typename GetKey>
void difference(const std::vector<Record> &before, const std::vector<Record> &after,
                std::vector<Record> &changed, std::vector<Key> &removed, GetKey key) {
	std::unordered_map<Key, const Record *> old;
	std::unordered_set<Key> next;
	for (const auto &r : before) {
		if (not old.emplace(key(r), &r).second) {
			throw Error{MSG(err) << "Duplicate identity in FVD previous world state"};
		}
	}
	for (const auto &r : after) {
		const auto id = key(r);
		if (not next.insert(id).second) {
			throw Error{MSG(err) << "Duplicate identity in FVD next world state"};
		}
		auto it = old.find(id);
		if (it == old.end() or not same(*it->second, r)) {
			changed.push_back(r);
		}
	}
	for (const auto &r : before) {
		if (not next.contains(key(r))) {
			removed.push_back(key(r));
		}
	}
}

template<typename Record, typename Key, typename GetKey>
void patch(std::vector<Record> &records, const std::vector<Record> &changes,
           const std::vector<Key> &removals, GetKey key) {
	// Most records are unchanged on a typical FVD tick. Avoid building
	// lookup tables and sorting thousands of entities on empty patches.
	if (changes.empty() and removals.empty()) {
		return;
	}
	std::unordered_map<Key, size_t> existing;
	for (size_t i = 0; i < records.size(); ++i) {
		if (not existing.emplace(key(records[i]), i).second) {
			throw Error{MSG(err) << "Duplicate identity in FVD world baseline"};
		}
	}
	std::unordered_set<Key> removed;
	for (const auto &id : removals) {
		if (not removed.insert(id).second or not existing.contains(id)) {
			throw Error{MSG(err) << "Unknown or duplicate FVD delta removal"};
		}
	}
	std::unordered_set<Key> updated;
	for (const auto &change : changes) {
		auto id = key(change);
		if (not updated.insert(id).second or removed.contains(id)) {
			throw Error{MSG(err) << "Duplicate or contradictory FVD delta update"};
		}
		auto it = existing.find(id);
		if (it == existing.end()) {
			records.push_back(change);
		}
		else {
			records[it->second] = change;
		}
	}
	std::erase_if(records, [&](const auto &record) {
		return removed.contains(key(record));
	});
	std::sort(records.begin(), records.end(), [&](const auto &a, const auto &b) {
		// Stable order independent of hash iteration or update arrival order.
		if constexpr (std::is_same_v<Key, coord::tile>) {
			return std::pair{key(a).ne, key(a).se} < std::pair{key(b).ne, key(b).se};
		}
		else {
			return key(a) < key(b);
		}
	});
}

void validate_header(const WorldState &previous, const WorldState &next) {
	if (previous.local_player_id == 0
	    or next.local_player_id != previous.local_player_id
	    or next.map_size != previous.map_size
	    or next.world_revision <= previous.world_revision
	    or next.simulation_time < previous.simulation_time) {
		throw Error{MSG(err) << "FVD delta identity, map size or revision mismatch"};
	}
}

constexpr uint32_t MAX_REMOVALS[] = {
	MAX_STATE_PLAYERS, MAX_STATE_ENTITIES, MAX_STATE_RESERVATIONS,
	MAX_STATE_CONSTRUCTIONS, MAX_STATE_ATTACK_ORDERS
};
constexpr size_t DELTA_HEADER_BYTES = 28;
constexpr uint16_t LOCAL_KILLS_CHANGED = 1;
constexpr uint16_t FIRST_HUNTER_SPAWN_TIME_CHANGED = 1 << 1;
constexpr uint16_t MATCH_OUTCOME_CHANGED = 1 << 2;

void check_count(size_t count, uint32_t limit) {
	if (count > limit) {
		throw Error{MSG(err) << "FVD world delta removal limit exceeded"};
	}
}
void check_room(const std::vector<uint8_t> &bytes, size_t offset, size_t length) {
	if (offset > bytes.size() or length > bytes.size() - offset) {
		throw Error{MSG(err) << "Truncated FVD world delta"};
	}
}

} // namespace

std::vector<ConfirmedCombatEffect> confirmed_combat_effects(
	const WorldState &previous, const WorldState &current) {
	std::unordered_map<gamestate::entity_id_t, const EntityState *> remaining;
	remaining.reserve(current.entities.size());
	for (const auto &entity : current.entities) {
		remaining.emplace(entity.entity_id, &entity);
	}
	std::vector<ConfirmedCombatEffect> effects;
	for (const auto &prior : previous.entities) {
		if (not prior.health or *prior.health <= 0) continue;
		auto next = remaining.find(prior.entity_id);
		if (next == remaining.end()) {
			// The host removed a previously living entity. The cue is
			// cosmetic; only the host decides death and kill attribution.
			// An entity merely walking into the recipient's fog is not dead.
			const bool own_side = current.local_player_id != 0
			                   and prior.owner_id != 0
			                   and (current.local_player_id == 1) == (prior.owner_id == 1);
			if (not own_side and current.fog.size() == 1
			    and not fog_tile_set(current.fog.front().visible, current.map_size,
			                         prior.position.to_tile())) {
				continue;
			}
			effects.push_back({
				prior.entity_id,
				prior.owner_id,
				prior.definition,
				prior.position,
				prior.angle,
				true,
			});
		}
		else if (next->second->health
		         and *next->second->health < *prior.health) {
			// A zero-HP entity retained until removal is already dead:
			// display the death effect once, never a nonlethal hit flash.
			effects.push_back({
				prior.entity_id,
				next->second->owner_id,
				next->second->definition,
				next->second->position,
				next->second->angle,
				*next->second->health <= 0,
			});
		}
	}
	return effects;
}

bool arrow_visible_at(const ArrowState &arrow, const time::time_t &now) {
	return now >= arrow.launch_time and now < arrow.impact_time;
}

WorldDelta make_world_delta(const WorldState &previous, const WorldState &current) {
	validate_header(previous, current);
	WorldDelta delta{};
	delta.changed.base_revision = previous.world_revision;
	delta.changed.world_revision = current.world_revision;
	delta.changed.simulation_time = current.simulation_time;
	delta.changed.map_size = current.map_size;
	delta.changed.local_player_id = current.local_player_id;
	delta.local_kills_changed = previous.local_kill_count != current.local_kill_count;
	if (delta.local_kills_changed) {
		delta.changed.local_kill_count = current.local_kill_count;
	}
	delta.first_hunter_spawn_time_changed =
		previous.first_hunter_spawn_time != current.first_hunter_spawn_time;
	if (delta.first_hunter_spawn_time_changed) {
		delta.changed.first_hunter_spawn_time = current.first_hunter_spawn_time;
	}
	if (previous.match_outcome and previous.match_outcome != current.match_outcome) {
		throw Error{MSG(err) << "An authoritative FVD match result is terminal"};
	}
	delta.match_outcome_changed = previous.match_outcome != current.match_outcome;
	if (delta.match_outcome_changed) {
		delta.changed.match_outcome = current.match_outcome;
	}
	difference(previous.players, current.players,
	           delta.changed.players, delta.removed_players, player_key);
	difference_entities(previous, current,
	                  delta.changed.entities, delta.removed_entities);
	difference(previous.reservations, current.reservations,
	           delta.changed.reservations, delta.removed_reservations, reservation_key);
	difference(previous.constructions, current.constructions,
	           delta.changed.constructions, delta.removed_constructions, construction_key);
	difference(previous.attack_orders, current.attack_orders,
	           delta.changed.attack_orders, delta.removed_attack_orders, attack_key);
	if (previous.fog != current.fog) {
		delta.changed.fog = current.fog;
	}
	// Arrows are immutable launch events. The client discards them at impact_time,
	// so an arrow's absence from the next host capture needs no tombstone.
	std::unordered_map<uint64_t, const ArrowState *> old_arrows;
	for (const auto &arrow : previous.arrows) {
		if (not old_arrows.emplace(arrow.arrow_id, &arrow).second) {
			throw Error{MSG(err) << "Duplicate FVD arrow in previous world state"};
		}
	}
	std::unordered_set<uint64_t> seen;
	for (const auto &arrow : current.arrows) {
		if (not seen.insert(arrow.arrow_id).second) {
			throw Error{MSG(err) << "Duplicate FVD arrow in next world state"};
		}
		auto existing = old_arrows.find(arrow.arrow_id);
		if (existing == old_arrows.end() or not same(*existing->second, arrow)) {
			delta.changed.arrows.push_back(arrow);
		}
	}
	return delta;
}

WorldState apply_world_delta(const WorldState &previous, const WorldDelta &delta) {
	const auto &changed = delta.changed;
	validate_header(previous, changed);
	if (changed.base_revision != previous.world_revision
	    or (not delta.local_kills_changed and changed.local_kill_count != 0)
	    or (not delta.first_hunter_spawn_time_changed
	        and changed.first_hunter_spawn_time)
	    or (delta.match_outcome_changed != changed.match_outcome.has_value())
	    or (previous.match_outcome and delta.match_outcome_changed)) {
		throw Error{MSG(err) << "FVD delta is not contiguous or its score flags are invalid"};
	}
	WorldState next = previous; // Apply to a copy; malformed patches never mutate the baseline.
	next.base_revision = 0;
	next.world_revision = changed.world_revision;
	next.simulation_time = changed.simulation_time;
	if (delta.local_kills_changed) {
		next.local_kill_count = changed.local_kill_count;
	}
	if (delta.first_hunter_spawn_time_changed) {
		next.first_hunter_spawn_time = changed.first_hunter_spawn_time;
	}
	if (delta.match_outcome_changed) {
		const auto &result = *changed.match_outcome;
		if (result.ended_at > next.simulation_time
		    or not ((result.winner == MatchWinner::HUNTER
		             and result.reason == MatchEndReason::ALL_BUILDERS_DEAD)
		            or (result.winner == MatchWinner::BUILDERS
		                and result.reason == MatchEndReason::TIME_LIMIT))) {
			throw Error{MSG(err) << "Invalid FVD match result in delta"};
		}
		next.match_outcome = result;
	}
	if (next.first_hunter_spawn_time
	    and *next.first_hunter_spawn_time <= next.simulation_time) {
		throw Error{MSG(err) << "FVD delta retained an expired Hunter spawn deadline"};
	}
	patch(next.players, changed.players, delta.removed_players, player_key);
	// Advance the already-authoritative movement plan before applying
	// explicit corrections or retasks. Unchanged moving entities need no
	// position payload merely because the world clock advanced.
	for (auto &entity : next.entities) {
		entity.position = position_at(entity, previous.simulation_time,
		                              next.simulation_time);
		entity.angle = facing_at(entity, next.simulation_time);
	}
	patch(next.entities, changed.entities, delta.removed_entities, entity_key);
	// Motion keyframes are timed segments, like arrows. A no-change delta
	// must still retire segments the guest has already played. The host
	// captures and transmits the next bounded window on an actual change.
	for (auto &entity : next.entities) {
		std::erase_if(entity.motion_keys, [&](const MotionKeyframe &key) {
			return key.time <= next.simulation_time;
		});
		std::erase_if(entity.facing_keys, [&](const FacingKeyframe &key) {
			return key.time <= next.simulation_time;
		});
	}
	patch(next.reservations, changed.reservations, delta.removed_reservations, reservation_key);
	patch(next.constructions, changed.constructions, delta.removed_constructions, construction_key);
	patch(next.attack_orders, changed.attack_orders, delta.removed_attack_orders, attack_key);
	if (not changed.fog.empty()) {
		next.fog = changed.fog;
	}
	// The host can deliver older projectile batches after congestion.
	// Preserve newly received IDs for at least this delta so the guest
	// presenter can replay their cosmetic flight once on receipt. Existing
	// stale IDs expire on the next delta. Neither path applies damage.
	const auto event_grace = time::time_t::from_int(3);
	std::unordered_set<uint64_t> newly_received_arrows;
	for (const auto &arrow : changed.arrows) {
		if (arrow.launch_time > changed.simulation_time
		    or not newly_received_arrows.insert(arrow.arrow_id).second) {
			throw Error{MSG(err) << "FVD delta includes a future or duplicate projectile"};
		}
	}
	static const std::vector<uint64_t> no_removed_arrows{};
	patch(next.arrows, changed.arrows, no_removed_arrows, arrow_key);
	std::erase_if(next.arrows, [&](const ArrowState &arrow) {
		return arrow.impact_time + event_grace <= changed.simulation_time
		    and not newly_received_arrows.contains(arrow.arrow_id);
	});
	if (std::none_of(next.players.begin(), next.players.end(), [&](const auto &player) {
		return player.player_id == next.local_player_id;
	})) {
		throw Error{MSG(err) << "FVD delta removed the assigned player"};
	}
	for (const auto &entity : next.entities) {
		if (entity.tower_kills and (entity.owner_id != next.local_player_id
		    or entity.definition != "fvd.data.entities.Tower")) {
			throw Error{MSG(err) << "FVD delta disclosed an opponent's tower kills"};
		}
		if (entity.hunter_stance and (entity.owner_id != next.local_player_id
		    or entity.definition != "fvd.data.entities.Horse")) {
			throw Error{MSG(err) << "FVD delta disclosed an opponent's Hunter stance"};
		}
	}
	return next;
}

std::vector<uint8_t> encode_world_delta(const WorldDelta &delta) {
	if (delta.changed.local_player_id == 0
	    or delta.changed.world_revision <= delta.changed.base_revision
	    or (not delta.local_kills_changed and delta.changed.local_kill_count != 0)
	    or (not delta.first_hunter_spawn_time_changed
	        and delta.changed.first_hunter_spawn_time)
	    or (delta.match_outcome_changed != delta.changed.match_outcome.has_value())) {
		throw Error{MSG(err) << "Invalid FVD world delta header"};
	}
	auto body = encode_world_state(delta.changed, true);
	if (body.size() > std::numeric_limits<uint32_t>::max()) {
		throw Error{MSG(err) << "FVD world delta body too large"};
	}
	const std::array<size_t, 5> counts{
		delta.removed_players.size(), delta.removed_entities.size(),
		delta.removed_reservations.size(), delta.removed_constructions.size(),
		delta.removed_attack_orders.size()};
	for (size_t i = 0; i < counts.size(); ++i) {
		check_count(counts[i], MAX_REMOVALS[i]);
	}
	std::vector<uint8_t> bytes;
	append_u16(bytes,
	           (delta.local_kills_changed ? LOCAL_KILLS_CHANGED : 0)
	               | (delta.first_hunter_spawn_time_changed
	                      ? FIRST_HUNTER_SPAWN_TIME_CHANGED : 0)
	               | (delta.match_outcome_changed ? MATCH_OUTCOME_CHANGED : 0));
	append_u16(bytes, 0);
	append_u32(bytes, static_cast<uint32_t>(body.size()));
	for (auto count : counts) {
		append_u32(bytes, static_cast<uint32_t>(count));
	}
	bytes.insert(bytes.end(), body.begin(), body.end());
	for (auto id : delta.removed_players) append_u64(bytes, id);
	for (auto id : delta.removed_entities) append_u64(bytes, id);
	for (auto tile : delta.removed_reservations) {
		append_i64(bytes, tile.ne);
		append_i64(bytes, tile.se);
	}
	for (auto id : delta.removed_constructions) append_u64(bytes, id);
	for (auto id : delta.removed_attack_orders) append_u64(bytes, id);
	if (bytes.size() > MAX_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "FVD world delta exceeds frame payload"};
	}
	return bytes;
}

WorldDelta decode_world_delta(const std::vector<uint8_t> &bytes) {
	check_room(bytes, 0, DELTA_HEADER_BYTES);
	const auto flags = read_u16(bytes.data());
	if ((flags & ~(LOCAL_KILLS_CHANGED | FIRST_HUNTER_SPAWN_TIME_CHANGED
	               | MATCH_OUTCOME_CHANGED)) != 0
	    or read_u16(bytes.data() + 2) != 0) {
		throw Error{MSG(err) << "Unsupported FVD world delta flags"};
	}
	const size_t body_size = read_u32(bytes.data() + 4);
	std::array<uint32_t, 5> counts{};
	size_t trailing_bytes = 0;
	for (size_t i = 0; i < counts.size(); ++i) {
		counts[i] = read_u32(bytes.data() + 8 + i * 4);
		check_count(counts[i], MAX_REMOVALS[i]);
		trailing_bytes += static_cast<size_t>(counts[i]) * (i == 2 ? 16 : 8);
	}
	if (body_size > MAX_PAYLOAD_SIZE or trailing_bytes > MAX_PAYLOAD_SIZE
	    or body_size + trailing_bytes > MAX_PAYLOAD_SIZE
	    or bytes.size() != DELTA_HEADER_BYTES + body_size + trailing_bytes) {
		throw Error{MSG(err) << "Invalid FVD world delta length"};
	}
	std::vector<uint8_t> body(bytes.begin() + DELTA_HEADER_BYTES,
	                         bytes.begin() + DELTA_HEADER_BYTES + body_size);
	WorldDelta delta{};
	delta.local_kills_changed = (flags & LOCAL_KILLS_CHANGED) != 0;
	delta.first_hunter_spawn_time_changed =
		(flags & FIRST_HUNTER_SPAWN_TIME_CHANGED) != 0;
	delta.match_outcome_changed = (flags & MATCH_OUTCOME_CHANGED) != 0;
	delta.changed = decode_world_state(body, true);
	if (delta.changed.world_revision <= delta.changed.base_revision
	    or (not delta.local_kills_changed and delta.changed.local_kill_count != 0)
	    or (not delta.first_hunter_spawn_time_changed
	        and delta.changed.first_hunter_spawn_time)
	    or (delta.match_outcome_changed != delta.changed.match_outcome.has_value())) {
		throw Error{MSG(err) << "Invalid FVD world delta revision or score"};
	}
	size_t offset = DELTA_HEADER_BYTES + body_size;
	auto next_u64 = [&]() {
		const auto value = read_u64(bytes.data() + offset);
		offset += 8;
		return value;
	};
	for (uint32_t i = 0; i < counts[0]; ++i) delta.removed_players.push_back(next_u64());
	for (uint32_t i = 0; i < counts[1]; ++i) delta.removed_entities.push_back(next_u64());
	for (uint32_t i = 0; i < counts[2]; ++i) {
		auto ne = read_i64(bytes.data() + offset);
		auto se = read_i64(bytes.data() + offset + 8);
		offset += 16;
		delta.removed_reservations.push_back({ne, se});
	}
	for (uint32_t i = 0; i < counts[3]; ++i) delta.removed_constructions.push_back(next_u64());
	for (uint32_t i = 0; i < counts[4]; ++i) delta.removed_attack_orders.push_back(next_u64());
	return delta;
}

void WorldDeltaBaseline::accept_snapshot(WorldState snapshot) {
	if (snapshot.base_revision != 0 or snapshot.local_player_id == 0) {
		throw Error{MSG(err) << "FVD delta baseline requires a full recipient snapshot"};
	}
	this->previous = std::move(snapshot);
}

WorldDelta WorldDeltaBaseline::advance(WorldState complete) {
	if (not this->previous) {
		throw Error{MSG(err) << "FVD delta baseline must receive a snapshot first"};
	}
	auto result = make_world_delta(*this->previous, complete);
	this->previous = std::move(complete);
	return result;
}

bool WorldDeltaBaseline::initialized() const {
	return this->previous.has_value();
}

const WorldState &WorldDeltaBaseline::current_snapshot() const {
	if (not this->previous) {
		throw Error{MSG(err) << "FVD delta baseline has no state to recover"};
	}
	return *this->previous;
}

} // namespace openage::network
