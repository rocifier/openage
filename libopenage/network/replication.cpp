// Copyright 2026 the openage authors. See copying.md for legal info.

#include "replication.h"

#include "error/error.h"

namespace openage::network {

ReplicationTracker::ReplicationTracker(gamestate::player_id_t assigned_player) :
	assigned_player{assigned_player} {
	if (assigned_player == 0 or assigned_player > MAX_STATE_PLAYERS) {
		throw Error{MSG(err) << "FVD replication requires an assigned playable player"};
	}
}

gamestate::player_id_t ReplicationTracker::get_local_player_id() const {
	return this->assigned_player;
}

std::optional<uint64_t> ReplicationTracker::get_local_kills() const {
	if (not this->displayed_kills_valid.load()) {
		return std::nullopt;
	}
	return this->displayed_kills.load();
}

std::optional<uint64_t> ReplicationTracker::get_tower_kills(
	gamestate::entity_id_t tower) const {
	std::lock_guard lock{this->tower_kills_mutex};
	if (not this->displayed_kills_valid.load()) {
		return std::nullopt;
	}
	auto it = this->displayed_tower_kills.find(tower);
	if (it == this->displayed_tower_kills.end()) {
		return std::nullopt;
	}
	return it->second;
}

namespace {

std::unordered_map<gamestate::entity_id_t, uint64_t> local_tower_kills(
	const WorldState &state) {
	std::unordered_map<gamestate::entity_id_t, uint64_t> result;
	for (const auto &entity : state.entities) {
		if (entity.tower_kills) {
			if (entity.owner_id != state.local_player_id
			    or entity.definition != "fvd.data.entities.Tower"
			    or not result.emplace(entity.entity_id, *entity.tower_kills).second) {
				throw Error{MSG(err) << "FVD replicated tower kills are not owner-private"};
			}
		}
	}
	return result;
}

} // namespace

void ReplicationTracker::accept_snapshot(const WorldState &snapshot) {
	std::lock_guard lock{this->tower_kills_mutex};
	if (not this->host_connected) {
		throw Error{MSG(err) << "FVD replication cannot resume after host disconnect"};
	}
	if (snapshot.local_player_id != this->assigned_player) {
		throw Error{MSG(err) << "FVD snapshot is for a different player"};
	}
	if (snapshot.base_revision != 0) {
		throw Error{MSG(err) << "FVD world snapshot must have base revision zero"};
	}
	if (this->initialized and snapshot.world_revision < this->revision) {
		throw Error{MSG(err) << "FVD world snapshot revision "
		                     << snapshot.world_revision
		                     << " is older than applied revision " << this->revision};
	}
	// A recovery snapshot must never silently undo or rewrite a result that
	// this recipient has already accepted. A new match uses a new tracker.
	if (this->initialized and this->reconstructed_world.match_outcome
	    and snapshot.match_outcome != this->reconstructed_world.match_outcome) {
		throw Error{MSG(err) << "FVD recovery snapshot changed terminal match outcome"};
	}

	auto towers = local_tower_kills(snapshot);
	this->reconstructed_world = snapshot;
	++this->snapshot_generation;
	this->displayed_tower_kills = std::move(towers);
	this->displayed_kills.store(snapshot.local_kill_count);
	this->displayed_kills_valid.store(true);
	this->revision = snapshot.world_revision;
	this->initialized = true;
	this->awaiting_snapshot = false;
}

std::optional<WorldState> ReplicationTracker::get_world_state() const {
	auto presentation = this->get_presentation_state();
	if (not presentation) {
		return std::nullopt;
	}
	return std::move(presentation->world);
}

std::optional<ReplicatedPresentationState> ReplicationTracker::get_presentation_state() const {
	std::lock_guard lock{this->tower_kills_mutex};
	if (not this->displayed_kills_valid.load()) {
		return std::nullopt;
	}
	return ReplicatedPresentationState{this->reconstructed_world, this->snapshot_generation};
}

DeltaDisposition ReplicationTracker::accept_delta(const WorldDelta &delta) {
	std::lock_guard lock{this->tower_kills_mutex};
	if (not this->host_connected) {
		throw Error{MSG(err) << "FVD replication cannot accept deltas after host disconnect"};
	}
	if (delta.changed.local_player_id != this->assigned_player) {
		throw Error{MSG(err) << "FVD delta is for a different player"};
	}
	if (delta.changed.world_revision <= delta.changed.base_revision) {
		throw Error{MSG(err) << "FVD world delta revision must advance beyond its base"};
	}
	if (this->awaiting_snapshot) {
		return DeltaDisposition::AWAIT_SNAPSHOT;
	}
	if (not this->initialized or delta.changed.base_revision != this->revision) {
		this->awaiting_snapshot = true;
		return DeltaDisposition::REQUEST_SNAPSHOT;
	}

	auto next = apply_world_delta(this->reconstructed_world, delta);
	auto towers = local_tower_kills(next);
	this->reconstructed_world = std::move(next);
	this->displayed_tower_kills = std::move(towers);
	this->displayed_kills.store(this->reconstructed_world.local_kill_count);
	this->displayed_kills_valid.store(true);
	this->revision = delta.changed.world_revision;
	return DeltaDisposition::APPLY;
}

void ReplicationTracker::host_disconnected() {
	std::lock_guard lock{this->tower_kills_mutex};
	this->host_connected = false;
	this->initialized = false;
	this->awaiting_snapshot = false;
	this->displayed_kills_valid.store(false);
	this->displayed_tower_kills.clear();
	this->reconstructed_world = {};
}

bool ReplicationTracker::is_host_connected() const {
	std::lock_guard lock{this->tower_kills_mutex};
	return this->host_connected;
}

bool ReplicationTracker::has_state() const {
	std::lock_guard lock{this->tower_kills_mutex};
	return this->initialized;
}

bool ReplicationTracker::is_awaiting_snapshot() const {
	std::lock_guard lock{this->tower_kills_mutex};
	return this->awaiting_snapshot;
}

uint64_t ReplicationTracker::get_revision() const {
	std::lock_guard lock{this->tower_kills_mutex};
	return this->revision;
}

SnapshotRequest ReplicationTracker::make_snapshot_request() const {
	std::lock_guard lock{this->tower_kills_mutex};
	if (not this->host_connected) {
		throw Error{MSG(err) << "FVD cannot request a snapshot after host disconnect"};
	}
	return {this->revision};
}

} // namespace openage::network
