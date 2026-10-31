// Copyright 2026 the openage authors. See copying.md for legal info.

#include "replication.h"

#include "construction_presentation.h"
#include "order_presentation.h"

#include "testing/testing.h"

namespace openage::network::tests {

void replication() {
	ReplicationTracker tracker{2};
	TESTEQUALS(tracker.get_local_player_id(), gamestate::player_id_t{2});
	TESTEQUALS(tracker.get_local_kills().has_value(), false);
	TESTEQUALS(tracker.get_tower_kills(100).has_value(), false);
	TESTEQUALS(tracker.get_world_state().has_value(), false);
	TESTEQUALS(tracker.get_presentation_state().has_value(), false);
	TESTTHROWS(ReplicationTracker{0});
	TESTTHROWS(ReplicationTracker{9});
	TESTEQUALS(tracker.is_host_connected(), true);
	TESTEQUALS(tracker.has_state(), false);
	TESTEQUALS(tracker.get_revision(), uint64_t{0});

	WorldState initial{};
	initial.map_size = 128;
	initial.local_player_id = 2;
	initial.world_revision = 4;
	initial.local_kill_count = 1;
	initial.players = {{1, PlayerRole::HUNTER}, {2, PlayerRole::BUILDER},
	                   {3, PlayerRole::BUILDER}};
	initial.entities = {
		{100, 2, "fvd.data.entities.Tower", {1, 1, 0}, 100, 100, 1},
		{200, 3, "fvd.data.entities.Tower", {2, 2, 0}, 100, 100},
	};
	auto foreign_snapshot = initial;
	foreign_snapshot.local_player_id = 3;
	TESTTHROWS(tracker.accept_snapshot(foreign_snapshot));
	TESTEQUALS(tracker.get_local_kills().has_value(), false);
	tracker.accept_snapshot(initial);
	const auto initial_generation = tracker.get_presentation_state()->snapshot_generation;
	TESTEQUALS(tracker.get_local_kills().value(), uint64_t{1});
	TESTEQUALS(tracker.get_tower_kills(100).value(), uint64_t{1});
	TESTEQUALS(tracker.get_tower_kills(200).has_value(), false);
	TESTEQUALS(tracker.has_state(), true);
	TESTEQUALS(tracker.is_awaiting_snapshot(), false);
	TESTEQUALS(tracker.get_revision(), uint64_t{4});

	WorldState contiguous = initial;
	contiguous.world_revision = 6;
	contiguous.local_kill_count = 3;
	contiguous.entities = {
		{100, 2, "fvd.data.entities.Tower", {1, 1, 0}, 100, 100, 2},
		{101, 2, "fvd.data.entities.Tower", {3, 3, 0}, 100, 100, 1},
	};
	auto delta = make_world_delta(initial, contiguous);
	auto foreign = delta;
	foreign.changed.local_player_id = 3;
	TESTTHROWS(tracker.accept_delta(foreign));
	TESTEQUALS(tracker.get_local_kills().value(), uint64_t{1});
	TESTEQUALS(tracker.get_revision(), uint64_t{4});
	TESTEQUALS(tracker.accept_delta(delta) == DeltaDisposition::APPLY, true);
	TESTEQUALS(tracker.get_revision(), uint64_t{6});
	TESTEQUALS(tracker.get_local_kills().value(), uint64_t{3});
	TESTEQUALS(tracker.get_tower_kills(100).value(), uint64_t{2});
	TESTEQUALS(tracker.get_tower_kills(101).value(), uint64_t{1});
	TESTEQUALS(tracker.get_tower_kills(200).has_value(), false);
	TESTEQUALS(tracker.get_world_state()->entities.size(), size_t{2});
	TESTEQUALS(tracker.get_presentation_state()->snapshot_generation, initial_generation);

	auto invalid = delta;
	invalid.changed.base_revision = 6;
	invalid.changed.world_revision = 6;
	TESTTHROWS(tracker.accept_delta(invalid));
	TESTEQUALS(tracker.get_revision(), uint64_t{6});

	WorldState future = contiguous;
	future.world_revision = 8;
	future.local_kill_count = 11;
	auto gap = make_world_delta(contiguous, future);
	gap.changed.base_revision = 7;
	TESTEQUALS(tracker.accept_delta(gap) == DeltaDisposition::REQUEST_SNAPSHOT, true);
	TESTEQUALS(tracker.is_awaiting_snapshot(), true);
	TESTEQUALS(tracker.make_snapshot_request().last_applied_revision, uint64_t{6});
	TESTEQUALS(tracker.get_local_kills().value(), uint64_t{3});
	TESTEQUALS(tracker.get_tower_kills(100).value(), uint64_t{2});
	TESTEQUALS(tracker.accept_delta(gap) == DeltaDisposition::AWAIT_SNAPSHOT, true);

	auto stale = initial;
	stale.world_revision = 5;
	TESTTHROWS(tracker.accept_snapshot(stale));
	TESTEQUALS(tracker.is_awaiting_snapshot(), true);

	WorldState recovered = future;
	recovered.world_revision = 9;
	recovered.local_kill_count = 7;
	recovered.entities = {
		{101, 2, "fvd.data.entities.Tower", {3, 3, 0}, 100, 100, 4},
	};
	tracker.accept_snapshot(recovered);
	TESTEQUALS(tracker.get_presentation_state()->snapshot_generation, initial_generation + 1);
	TESTEQUALS(tracker.is_awaiting_snapshot(), false);
	TESTEQUALS(tracker.get_revision(), uint64_t{9});
	TESTEQUALS(tracker.get_local_kills().value(), uint64_t{7});
	TESTEQUALS(tracker.get_tower_kills(100).has_value(), false);
	TESTEQUALS(tracker.get_tower_kills(101).value(), uint64_t{4});
	TESTEQUALS(tracker.get_world_state()->entities.size(), size_t{1});

	// Bad removal must not partly mutate the client's reconstructed world.
	auto no_change = recovered;
	no_change.world_revision = 10;
	auto corrupt = make_world_delta(recovered, no_change);
	corrupt.removed_entities = {9999};
	TESTTHROWS(tracker.accept_delta(corrupt));
	TESTEQUALS(tracker.get_revision(), uint64_t{9});
	TESTEQUALS(tracker.get_tower_kills(101).value(), uint64_t{4});

	// No score update means the previously accepted total is retained.
	auto heartbeat = make_world_delta(recovered, no_change);
	TESTEQUALS(heartbeat.local_kills_changed, false);
	TESTEQUALS(tracker.accept_delta(heartbeat) == DeltaDisposition::APPLY, true);
	// Recovery remains visible even if a delta arrives before the next render frame.
	TESTEQUALS(tracker.get_presentation_state()->snapshot_generation, initial_generation + 1);
	TESTEQUALS(tracker.get_presentation_state()->world.world_revision, uint64_t{10});
	tracker.accept_snapshot(no_change);
	TESTEQUALS(tracker.get_presentation_state()->snapshot_generation, initial_generation + 2);
	TESTEQUALS(tracker.get_local_kills().value(), uint64_t{7});

	// A recipient must see the identical authoritative terminal result
	// through both a delta and a later full recovery snapshot. It never
	// derives a different winner from its fog-filtered local entity list.
	auto finished_world = no_change;
	finished_world.world_revision = 11;
	finished_world.simulation_time = time::time_t::from_int(2700);
	finished_world.match_outcome = {MatchWinner::BUILDERS,
	                                MatchEndReason::TIME_LIMIT,
	                                finished_world.simulation_time};
	auto result_delta = decode_world_delta(encode_world_delta(
		make_world_delta(no_change, finished_world)));
	TESTEQUALS(tracker.accept_delta(result_delta) == DeltaDisposition::APPLY, true);
	TESTEQUALS(tracker.get_world_state()->match_outcome,
	           finished_world.match_outcome);
	auto finished_snapshot = finished_world;
	finished_snapshot.world_revision = 12;
	finished_snapshot.simulation_time = time::time_t::from_int(2701);
	tracker.accept_snapshot(decode_world_state(encode_world_state(finished_snapshot)));
	TESTEQUALS(tracker.get_world_state()->match_outcome,
	           finished_world.match_outcome);
	// Even a newer authenticated recovery snapshot cannot erase or change
	// a terminal result accepted earlier in the same match.
	auto erased_result = finished_snapshot;
	erased_result.world_revision = 13;
	erased_result.match_outcome.reset();
	TESTTHROWS(tracker.accept_snapshot(erased_result));
	erased_result.match_outcome = MatchOutcome{MatchWinner::HUNTER,
	                                           MatchEndReason::ALL_BUILDERS_DEAD,
	                                           finished_world.simulation_time};
	TESTTHROWS(tracker.accept_snapshot(erased_result));
	TESTEQUALS(tracker.get_revision(), uint64_t{12});

	auto bad_snapshot = recovered;
	bad_snapshot.base_revision = 1;
	TESTTHROWS(tracker.accept_snapshot(bad_snapshot));

	// Host loss invalidates both the total and individual tower scores.
	tracker.host_disconnected();
	TESTEQUALS(tracker.is_host_connected(), false);
	TESTEQUALS(tracker.has_state(), false);
	TESTEQUALS(tracker.is_awaiting_snapshot(), false);
	TESTEQUALS(tracker.get_local_kills().has_value(), false);
	TESTEQUALS(tracker.get_tower_kills(101).has_value(), false);
	TESTEQUALS(tracker.get_world_state().has_value(), false);
	TESTEQUALS(tracker.get_presentation_state().has_value(), false);
	TESTEQUALS(tracker.get_revision(), uint64_t{10});
	TESTTHROWS(tracker.accept_snapshot(recovered));
	TESTTHROWS(tracker.accept_delta(heartbeat));
	TESTTHROWS(tracker.make_snapshot_request());

	// Recipients receive only their own private tower scores.
	ReplicationTracker other_player{3};
	auto other_snapshot = recovered;
	other_snapshot.local_player_id = 3;
	other_snapshot.local_kill_count = 12;
	other_snapshot.entities = {{
		200, 3, "fvd.data.entities.Tower", {2, 2, 0}, 100, 100, 12}};
	other_player.accept_snapshot(other_snapshot);
	TESTEQUALS(other_player.get_local_kills().value(), uint64_t{12});
	TESTEQUALS(other_player.get_tower_kills(200).value(), uint64_t{12});
	TESTEQUALS(other_player.get_tower_kills(100).has_value(), false);

	// Every participant sees identical site identities and ownership. A
	// queued foundation has a reservation but no active construction order.
	WorldState sites_world{};
	sites_world.map_size = 128;
	sites_world.local_player_id = 2;
	sites_world.world_revision = 1;
	sites_world.players = {{1, PlayerRole::HUNTER},
	                       {2, PlayerRole::BUILDER}, {3, PlayerRole::BUILDER}};
	sites_world.entities = {
		{10, 2, "fvd.data.entities.Builder", {8, 8, 0}},
		{11, 3, "fvd.data.entities.Builder", {9, 9, 0}},
		{20, 2, "fvd.data.entities.Foundation", {15.5, 15.5, 0}},
		{21, 2, "fvd.data.entities.Foundation", {18.5, 18.5, 0}},
		{22, 3, "fvd.data.entities.Tower", {25.5, 25.5, 0}},
		{23, 3, "fvd.data.entities.Tower", {30.5, 30.5, 0}},
	};
	sites_world.reservations = {
		{{15, 15}, 10}, {{18, 18}, 10}, {{25, 25}, 11}};
	sites_world.constructions = {
		{7, 2, 10, {18, 18}, ConstructionPhase::PLANNED,
		 time::time_t::from_int(12), time::time_t::from_int(12), 21},
		{8, 3, 11, {25, 25}, ConstructionPhase::UNDER_CONSTRUCTION,
		 time::time_t::from_int(20), time::time_t::from_int(10), 22},
	};
	const auto views = make_construction_presentation(sites_world);
	TESTEQUALS(views.sites.size(), size_t{4});
	TESTEQUALS(views.reservations.size(), size_t{3});
	TESTEQUALS(views.sites[0].phase == SitePresentationPhase::QUEUED, true);
	TESTEQUALS(views.sites[0].builder_id, gamestate::entity_id_t{10});
	TESTEQUALS(views.sites[1].phase == SitePresentationPhase::PLANNED, true);
	TESTEQUALS(views.sites[2].phase == SitePresentationPhase::BUILDING, true);
	TESTEQUALS(views.sites[2].owner_id, gamestate::player_id_t{3});
	TESTEQUALS(views.sites[3].phase == SitePresentationPhase::COMPLETE, true);
	auto hunter_view = sites_world;
	hunter_view.local_player_id = 1;
	const auto hunter_sites = make_construction_presentation(hunter_view);
	TESTEQUALS(hunter_sites.sites.size(), views.sites.size());
	TESTEQUALS(hunter_sites.sites[0].entity_id, views.sites[0].entity_id);
	TESTEQUALS(hunter_sites.sites[2].owner_id, views.sites[2].owner_id);

	// Cancellation removes both the queued marker and its footprint,
	// including after applying a revision-contiguous network delta.
	auto after_cancel = sites_world;
	after_cancel.world_revision = 2;
	after_cancel.entities.erase(after_cancel.entities.begin() + 2);
	after_cancel.reservations.erase(after_cancel.reservations.begin());
	auto cancel_delta = make_world_delta(sites_world, after_cancel);
	auto applied_cancel = apply_world_delta(sites_world, cancel_delta);
	auto remaining_sites = make_construction_presentation(applied_cancel);
	TESTEQUALS(remaining_sites.sites.size(), size_t{3});
	TESTEQUALS(remaining_sites.reservations.size(), size_t{2});
	TESTEQUALS(remaining_sites.sites.front().entity_id, gamestate::entity_id_t{21});

	// Prioritizing the queued site replaces the previous active order.
	auto retasked_world = sites_world;
	retasked_world.constructions.front().site_entity_id = 20;
	retasked_world.constructions.front().target = {15, 15};
	auto retasked = make_construction_presentation(retasked_world);
	TESTEQUALS(retasked.sites.front().phase == SitePresentationPhase::PLANNED, true);
	TESTEQUALS(retasked.sites[1].phase == SitePresentationPhase::QUEUED, true);
	auto mismatched = retasked_world;
	mismatched.constructions[0].owner_id = 3;
	TESTTHROWS(make_construction_presentation(mismatched));

	// Active order overlays resolve target positions from the same accepted
	// world as movement and health, so a moving target cannot leave a stale
	// client-authored marker behind.
	WorldState order_world{};
	order_world.entities = {
		{0, 0, "fvd.data.entities.Placeholder", {0, 0, 0}},
		{100, 1, "fvd.data.entities.Horse", {10, 11, 0}},
		{101, 1, "fvd.data.entities.Horse", {12, 13, 0}},
		{200, 2, "fvd.data.entities.Tower", {20, 21, 0}},
	};
	order_world.attack_orders = {
		{100, {18, 19, 0}, 200, false, true},
		{101, {30, 31, 0}, std::nullopt, false, false},
	};
	order_world.entities[1].motion_keys = {
		{time::time_t::from_int(1), {12, 13, 0}},
	};
	const auto order_view = make_attack_order_presentation(
		order_world, time::time_t::from_double(0.5));
	TESTEQUALS(order_view.size(), size_t{2});
	TESTEQUALS(order_view[0].horse_position, (coord::phys3{11, 12, 0}));
	TESTEQUALS(order_view[0].marker_position, (coord::phys3{20, 21, 0}));
	TESTEQUALS(order_view[0].focus_target, true);
	TESTEQUALS(order_view[1].marker_position, (coord::phys3{30, 31, 0}));
	auto missing_target = order_world;
	missing_target.attack_orders.front().target_id = 999;
	TESTTHROWS(make_attack_order_presentation(
		missing_target, time::TIME_ZERO));
	auto invalid_focus = order_world;
	invalid_focus.attack_orders.front().target_id.reset();
	TESTTHROWS(make_attack_order_presentation(
		invalid_focus, time::TIME_ZERO));

	WorldState startup_world{};
	startup_world.entities = {
		{0, 0, "fvd.data.entities.Placeholder", {0, 0, 0}},
		{0, 0, "fvd.data.entities.Placeholder", {1, 1, 0}},
	};
	TESTEQUALS(make_attack_order_presentation(
		           startup_world, time::TIME_ZERO)
		           .size(),
	           size_t{0});
}

} // namespace openage::network::tests
