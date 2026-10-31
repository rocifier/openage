// Copyright 2026 the openage authors. See copying.md for legal info.

#include "messages.h"

#include "network/world_delta.h"

#include <string>
#include <vector>

#include "testing/testing.h"

namespace openage::network::tests {

void messages() {
	ClientHello hello{};
	hello.minimum_protocol_minor = 0;
	hello.participant_name = "Builder";
	hello.build_version = "fvd-test";
	for (size_t i = 0; i < hello.asset_digest.size(); ++i) {
		hello.asset_digest[i] = static_cast<uint8_t>(i);
	}
	auto encoded_hello = encode_client_hello(hello);
	auto decoded_hello = decode_client_hello(encoded_hello);
	TESTEQUALS(decoded_hello.minimum_protocol_minor, hello.minimum_protocol_minor);
	TESTEQUALS(decoded_hello.participant_name, hello.participant_name);
	TESTEQUALS(decoded_hello.build_version, hello.build_version);
	decoded_hello.asset_digest == hello.asset_digest or TESTFAIL;

	auto invalid_hello_flags = encoded_hello;
	invalid_hello_flags[3] = 1;
	TESTTHROWS(decode_client_hello(invalid_hello_flags));
	auto truncated_hello = encoded_hello;
	truncated_hello.pop_back();
	TESTTHROWS(decode_client_hello(truncated_hello));
	auto trailing_hello = encoded_hello;
	trailing_hello.push_back(0);
	TESTTHROWS(decode_client_hello(trailing_hello));
	ClientHello invalid_hello{};
	invalid_hello.build_version = "fvd-test";
	TESTTHROWS(encode_client_hello(invalid_hello));
	invalid_hello.participant_name = std::string(MAX_PARTICIPANT_NAME_SIZE + 1, 'x');
	TESTTHROWS(encode_client_hello(invalid_hello));

	ServerAccept accept{42, 3};
	auto decoded_accept = decode_server_accept(encode_server_accept(accept));
	TESTEQUALS(decoded_accept.session_id, accept.session_id);
	TESTEQUALS(decoded_accept.player_id, accept.player_id);
	TESTTHROWS(encode_server_accept({0, 3}));
	TESTTHROWS(encode_server_accept({42, 0}));
	auto trailing_accept = encode_server_accept(accept);
	trailing_accept.push_back(0);
	TESTTHROWS(decode_server_accept(trailing_accept));

	MatchReady ready{2, 12};
	auto ready_bytes = encode_match_ready(ready);
	auto ready_roundtrip = decode_match_ready(ready_bytes);
	TESTEQUALS(ready_roundtrip.player_id, gamestate::player_id_t{2});
	TESTEQUALS(ready_roundtrip.snapshot_revision, uint64_t{12});
	TESTTHROWS(encode_match_ready({0, 12}));
	TESTTHROWS(encode_match_ready({2, 0}));
	auto ready_short = ready_bytes;
	ready_short.pop_back();
	TESTTHROWS(decode_match_ready(ready_short));
	auto ready_trailing = ready_bytes;
	ready_trailing.push_back(0);
	TESTTHROWS(decode_match_ready(ready_trailing));

	ServerReject reject{"gameplay asset manifest does not match host"};
	TESTEQUALS(decode_server_reject(encode_server_reject(reject)).reason,
	           reject.reason);
	TESTTHROWS(encode_server_reject({}));
	TESTTHROWS(encode_server_reject(
		{std::string(MAX_HANDSHAKE_REJECTION_SIZE + 1, 'x')}));

	CommandIntent intent{};
	intent.command_id = 0x0102030405060708;
	intent.kind = CommandKind::MOVE;
	intent.entity_ids = {0x1112131415161718, 0x2122232425262728};
	intent.target = {1.5, -2.25, 0};
	auto encoded_intent = encode_command_intent(intent);
	const std::vector<uint8_t> expected_intent{
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
		0x00, 0x01,
		0x00, 0x00,
		0x00, 0x00, 0x00, 0x02,
		0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
		0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x80, 0x00,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xfd, 0xc0, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	encoded_intent == expected_intent or TESTFAIL;

	auto decoded_intent = decode_command_intent(encoded_intent);
	TESTEQUALS(decoded_intent.command_id, intent.command_id);
	decoded_intent.kind == intent.kind or TESTFAIL;
	decoded_intent.entity_ids == intent.entity_ids or TESTFAIL;
	TESTEQUALS(decoded_intent.target, intent.target);

	for (const auto kind : {CommandKind::FOCUS_ATTACK,
	                        CommandKind::PRIORITIZE_FOUNDATION}) {
		auto targeted = intent;
		targeted.kind = kind;
		targeted.target_entity = uint64_t{501};
		auto bytes = encode_command_intent(targeted);
		auto roundtrip = decode_command_intent(bytes);
		TESTEQUALS(roundtrip.target_entity.has_value(), true);
		TESTEQUALS(*roundtrip.target_entity, uint64_t{501});
		TESTEQUALS(roundtrip.kind == kind, true);
		TESTEQUALS(roundtrip.queue, false);
		auto missing = targeted;
		missing.target_entity.reset();
		TESTTHROWS(encode_command_intent(missing));
		bytes.pop_back();
		TESTTHROWS(decode_command_intent(bytes));
	}

	auto queued = intent;
	queued.kind = CommandKind::BUILD_TOWER;
	queued.queue = true;
	auto queued_bytes = encode_command_intent(queued);
	auto decoded_queue = decode_command_intent(queued_bytes);
	TESTEQUALS(decoded_queue.queue, true);
	TESTEQUALS(decoded_queue.target_entity.has_value(), false);
	queued.target = {61.5, 66.5, 0};
	queued.build_line = {{61.5, 66.5, 0}, {62.5, 66.5, 0}, {63.5, 66.5, 0}};
	auto line_bytes = encode_command_intent(queued);
	auto decoded_line = decode_command_intent(line_bytes);
	TESTEQUALS(decoded_line.build_line.size(), size_t{3});
	TESTEQUALS(decoded_line.build_line.front(), queued.build_line.front());
	TESTEQUALS(decoded_line.build_line.back(), queued.build_line.back());
	auto truncated_line = line_bytes;
	truncated_line.pop_back();
	TESTTHROWS(decode_command_intent(truncated_line));
	auto excessive_line = queued;
	excessive_line.build_line.resize(MAX_BUILD_LINE_SITES + 1, queued.target);
	TESTTHROWS(encode_command_intent(excessive_line));
	auto wrong_start = queued;
	wrong_start.build_line.front() = {60.5, 66.5, 0};
	TESTTHROWS(encode_command_intent(wrong_start));
	auto wrong_flag = queued;
	wrong_flag.queue = false;
	TESTTHROWS(encode_command_intent(wrong_flag));
	queued.kind = CommandKind::MOVE;
	TESTTHROWS(encode_command_intent(queued));
	auto invalid_target_flag = encoded_intent;
	invalid_target_flag[11] = 1;
	TESTTHROWS(decode_command_intent(invalid_target_flag));

	// Demolition carries exactly one selected tower ID and no optional intent.
	CommandIntent demolition{12, CommandKind::DEMOLISH_TOWER, {201}};
	auto demolition_roundtrip = decode_command_intent(encode_command_intent(demolition));
	TESTEQUALS(demolition_roundtrip.kind == CommandKind::DEMOLISH_TOWER, true);
	TESTEQUALS(demolition_roundtrip.entity_ids.size(), size_t{1});
	TESTEQUALS(demolition_roundtrip.entity_ids.front(), gamestate::entity_id_t{201});
	TESTEQUALS(demolition_roundtrip.stance.has_value(), false);
	TESTEQUALS(demolition_roundtrip.target_entity.has_value(), false);
	demolition.entity_ids.clear();
	TESTTHROWS(encode_command_intent(demolition));
	demolition.entity_ids = {201, 202};
	TESTTHROWS(encode_command_intent(demolition));
	auto invalid_demolition = encode_command_intent(CommandIntent{
		12, CommandKind::DEMOLISH_TOWER, {201}});
	// Entity count starts at offset 12 in the command intent.
	invalid_demolition[15] = 0;
	TESTTHROWS(decode_command_intent(invalid_demolition));

	// Garrison uses the existing targeted command envelope and never trusts
	// the caller's claimed ownership. The host validates both IDs later.
	CommandIntent garrison{13, CommandKind::GARRISON, {201}};
	garrison.target_entity = 202;
	auto decoded_garrison = decode_command_intent(encode_command_intent(garrison));
	TESTEQUALS(decoded_garrison.kind == CommandKind::GARRISON, true);
	TESTEQUALS(decoded_garrison.entity_ids, std::vector<gamestate::entity_id_t>{201});
	TESTEQUALS(decoded_garrison.target_entity, std::optional<gamestate::entity_id_t>{202});
	garrison.target_entity.reset();
	TESTTHROWS(encode_command_intent(garrison));
	garrison.target_entity = 202;
	garrison.entity_ids = {201, 203};
	TESTTHROWS(encode_command_intent(garrison));

	CommandIntent ungarrison{14, CommandKind::UNGARRISON, {202}};
	auto decoded_ungarrison = decode_command_intent(encode_command_intent(ungarrison));
	TESTEQUALS(decoded_ungarrison.kind == CommandKind::UNGARRISON, true);
	TESTEQUALS(decoded_ungarrison.entity_ids.front(), gamestate::entity_id_t{202});
	ungarrison.target_entity = 201;
	TESTTHROWS(encode_command_intent(ungarrison));
	ungarrison.target_entity.reset();
	ungarrison.entity_ids.clear();
	TESTTHROWS(encode_command_intent(ungarrison));

	auto invalid_kind = encoded_intent;
	invalid_kind[8] = 0;
	invalid_kind[9] = 6;
	TESTTHROWS(decode_command_intent(invalid_kind));

	auto invalid_flags = encoded_intent;
	invalid_flags[11] = 1;
	TESTTHROWS(decode_command_intent(invalid_flags));

	auto excessive_entities = encoded_intent;
	excessive_entities[12] = 0;
	excessive_entities[13] = 0;
	excessive_entities[14] = 4;
	excessive_entities[15] = 1;
	TESTTHROWS(decode_command_intent(excessive_entities));

	auto truncated_intent = encoded_intent;
	truncated_intent.pop_back();
	TESTTHROWS(decode_command_intent(truncated_intent));

	auto trailing_intent = encoded_intent;
	trailing_intent.push_back(0);
	TESTTHROWS(decode_command_intent(trailing_intent));

	CommandResult result{};
	result.command_id = intent.command_id;
	result.status = CommandStatus::REJECTED;
	result.world_revision = 7;
	result.simulation_time = time::time_t::from_double(12.5);
	result.reason = "entity is not owned by this session";
	auto decoded_result = decode_command_result(encode_command_result(result));
	TESTEQUALS(decoded_result.command_id, result.command_id);
	decoded_result.status == result.status or TESTFAIL;
	TESTEQUALS(decoded_result.world_revision, result.world_revision);
	TESTEQUALS(decoded_result.simulation_time, result.simulation_time);
	TESTEQUALS(decoded_result.reason, result.reason);

	auto encoded_result = encode_command_result(result);
	auto invalid_status = encoded_result;
	invalid_status[9] = 3;
	TESTTHROWS(decode_command_result(invalid_status));

	auto invalid_result_flags = encoded_result;
	invalid_result_flags[11] = 1;
	TESTTHROWS(decode_command_result(invalid_result_flags));

	auto truncated_result = encoded_result;
	truncated_result.pop_back();
	TESTTHROWS(decode_command_result(truncated_result));

	auto trailing_result = encoded_result;
	trailing_result.push_back(0);
	TESTTHROWS(decode_command_result(trailing_result));

	CommandResult oversized_reason{};
	oversized_reason.reason = std::string(MAX_REJECTION_REASON_SIZE + 1, 'x');
	TESTTHROWS(encode_command_result(oversized_reason));

	WorldState state{};
	state.base_revision = 4;
	state.world_revision = 5;
	state.simulation_time = time::time_t::from_double(3.25);
	state.map_size = 128;
	state.local_player_id = 2;
	state.local_kill_count = 3;
	state.first_hunter_spawn_time = time::time_t::from_int(20);
	state.players = {
		{1, PlayerRole::HUNTER},
		{2, PlayerRole::BUILDER, false},
	};
	state.entities = {
		{100, 1, "fvd.data.entities.Horse", {10.5, 20.25, 0}, 17, 25},
		{200, 2, "fvd.data.entities.Foundation", {-1.0, 2.0, 3.0}},
		{201, 2, "fvd.data.entities.Tower", {12.0, 20.0, 0}, 100, 100, 2},
		{202, 1, "fvd.data.entities.Tower", {13.0, 20.0, 0}, 100, 100},
	};
	state.entities[2].garrisoned_builder = gamestate::entity_id_t{300};
	state.entities.front().motion_keys = {
		{time::time_t::from_double(3.75), {11.0, 20.5, 0}},
		{time::time_t::from_double(4.25), {12.0, 21.0, 0}},
	};
	state.entities.front().animation_action = gamestate::AnimationAction::ATTACK;
	state.entities.front().animation_started_at = time::time_t::from_int(2);
	state.entities.front().angle = coord::phys_angle_t::from_int(315);
	state.entities.front().facing_keys = {
		{time::time_t::from_double(3.75), coord::phys_angle_t::from_int(270)},
		{time::time_t::from_double(4.25), coord::phys_angle_t::from_int(180)},
	};
	CommandIntent point{15, CommandKind::SET_UNGARRISON_POINT, {201}, {60, 66, 0}};
	TESTEQUALS(decode_command_intent(encode_command_intent(point)).kind,
	           CommandKind::SET_UNGARRISON_POINT);
	point.entity_ids = {201, 202};
	TESTTHROWS(encode_command_intent(point));
	point.entity_ids = {201};
	point.target_entity = gamestate::entity_id_t{202};
	TESTTHROWS(encode_command_intent(point));
	point.target_entity.reset();

	state.reservations = {{{10, 20}, 300}};
	state.constructions = {{
		7, 2, 300, {10, 20}, ConstructionPhase::PLANNED,
		time::time_t::from_double(4.5), time::time_t::from_double(4.5), 200}};
	state.attack_orders = {{
		100, {12.0, 21.0, 0}, 300, false, true}};
	state.arrows = {{
		9, 2, 100, {10.0, 20.0, 1.3}, {10.5, 20.25, 0.45},
		time::time_t::from_double(3.25), time::time_t::from_double(3.45)}};
	auto decoded_state = decode_world_state(encode_world_state(state));
	TESTEQUALS(decoded_state.base_revision, state.base_revision);
	TESTEQUALS(decoded_state.world_revision, state.world_revision);
	TESTEQUALS(decoded_state.simulation_time, state.simulation_time);
	TESTEQUALS(decoded_state.map_size, state.map_size);
	TESTEQUALS(decoded_state.local_player_id, state.local_player_id);
	TESTEQUALS(decoded_state.local_kill_count, state.local_kill_count);
	TESTEQUALS(decoded_state.first_hunter_spawn_time,
	           state.first_hunter_spawn_time);
	TESTEQUALS(decoded_state.players.size(), state.players.size());
	for (size_t i = 0; i < state.players.size(); ++i) {
		TESTEQUALS(decoded_state.players[i].player_id, state.players[i].player_id);
		decoded_state.players[i].role == state.players[i].role or TESTFAIL;
		TESTEQUALS(decoded_state.players[i].connected,
		           state.players[i].connected);
	}
	TESTEQUALS(decoded_state.entities.size(), state.entities.size());
	for (size_t i = 0; i < state.entities.size(); ++i) {
		TESTEQUALS(decoded_state.entities[i].entity_id, state.entities[i].entity_id);
		TESTEQUALS(decoded_state.entities[i].owner_id, state.entities[i].owner_id);
		TESTEQUALS(decoded_state.entities[i].definition, state.entities[i].definition);
		TESTEQUALS(decoded_state.entities[i].garrisoned_builder,
		           state.entities[i].garrisoned_builder);
		TESTEQUALS(decoded_state.entities[i].position, state.entities[i].position);
		TESTEQUALS(decoded_state.entities[i].angle, state.entities[i].angle);
		TESTEQUALS(decoded_state.entities[i].animation_action,
		           state.entities[i].animation_action);
		TESTEQUALS(decoded_state.entities[i].animation_started_at,
		           state.entities[i].animation_started_at);
		TESTEQUALS(decoded_state.entities[i].facing_keys.size(),
		           state.entities[i].facing_keys.size());
		for (size_t key = 0; key < state.entities[i].facing_keys.size(); ++key) {
			TESTEQUALS(decoded_state.entities[i].facing_keys[key].time,
			           state.entities[i].facing_keys[key].time);
			TESTEQUALS(decoded_state.entities[i].facing_keys[key].angle,
			           state.entities[i].facing_keys[key].angle);
		}
		TESTEQUALS(decoded_state.entities[i].motion_keys.size(),
		           state.entities[i].motion_keys.size());
		for (size_t key = 0; key < state.entities[i].motion_keys.size(); ++key) {
			TESTEQUALS(decoded_state.entities[i].motion_keys[key].time,
			           state.entities[i].motion_keys[key].time);
			TESTEQUALS(decoded_state.entities[i].motion_keys[key].position,
			           state.entities[i].motion_keys[key].position);
		}
		TESTEQUALS(decoded_state.entities[i].health.has_value(),
		           state.entities[i].health.has_value());
		TESTEQUALS(decoded_state.entities[i].tower_kills.has_value(),
		           state.entities[i].tower_kills.has_value());
		if (state.entities[i].tower_kills) {
			TESTEQUALS(decoded_state.entities[i].tower_kills.value(),
			           state.entities[i].tower_kills.value());
		}
		if (state.entities[i].health) {
			TESTEQUALS(*decoded_state.entities[i].health, *state.entities[i].health);
			TESTEQUALS(*decoded_state.entities[i].maximum_health,
			           *state.entities[i].maximum_health);
		}
	}
	TESTEQUALS(decoded_state.reservations.size(), size_t{1});
	TESTEQUALS(decoded_state.reservations.front().tile, (coord::tile{10, 20}));
	TESTEQUALS(decoded_state.reservations.front().builder_id,
	           gamestate::entity_id_t{300});
	TESTEQUALS(decoded_state.constructions.size(), size_t{1});
	TESTEQUALS(decoded_state.constructions.front().order_id, uint64_t{7});
	decoded_state.constructions.front().phase == ConstructionPhase::PLANNED
		or TESTFAIL;
	TESTEQUALS(decoded_state.constructions.front().phase_end_time,
	           time::time_t::from_double(4.5));
	TESTEQUALS(decoded_state.constructions.front().phase_start_time,
	           time::time_t::from_double(4.5));
	TESTEQUALS(decoded_state.constructions.front().progress_percent(
		           time::time_t::from_double(4.2)), 0);
	TESTEQUALS(decoded_state.constructions.front().site_entity_id,
	           gamestate::entity_id_t{200});
	TESTEQUALS(decoded_state.attack_orders.size(), size_t{1});
	TESTEQUALS(decoded_state.attack_orders.front().horse_id,
	           gamestate::entity_id_t{100});
	decoded_state.attack_orders.front().target_id.has_value() or TESTFAIL;
	TESTEQUALS(*decoded_state.attack_orders.front().target_id,
	           gamestate::entity_id_t{300});
	TESTEQUALS(decoded_state.attack_orders.front().focus_target, true);
	TESTEQUALS(decoded_state.arrows.size(), size_t{1});
	TESTEQUALS(decoded_state.arrows.front().arrow_id, uint64_t{9});
	TESTEQUALS(decoded_state.arrows.front().target_id,
	           gamestate::entity_id_t{100});
	TESTEQUALS(decoded_state.arrows.front().impact_time,
	           time::time_t::from_double(3.45));
	// Hunter launches reuse the same immutable event wire, keyed by
	// owner_id==1, without granting recipients any authority over HP.
	auto hunter_launch = state;
	hunter_launch.arrows.push_back({
		10, 1, 201, {10.5, 20.25, 0.9}, {12.0, 20.0, 1.0},
		time::time_t::from_double(3.25),
		time::time_t::from_double(3.70)});
	const auto roundtrip_hunter = decode_world_state(encode_world_state(hunter_launch));
	TESTEQUALS(roundtrip_hunter.arrows.size(), size_t{2});
	TESTEQUALS(roundtrip_hunter.arrows.back().owner_id, gamestate::player_id_t{1});
	TESTEQUALS(roundtrip_hunter.arrows.back().impact_time,
	           time::time_t::from_double(3.70));
	// Match outcomes are host-authored and public: snapshots must reject
	// unknown reasons, impossible winner/reason pairs and future end times.
	auto winner_state = state;
	winner_state.match_outcome = {MatchWinner::BUILDERS,
	                              MatchEndReason::TIME_LIMIT,
	                              state.simulation_time};
	auto winner_bytes = encode_world_state(winner_state);
	auto winner_roundtrip = decode_world_state(winner_bytes);
	TESTEQUALS(winner_roundtrip.match_outcome, winner_state.match_outcome);
	auto invalid_winner = winner_state;
	invalid_winner.match_outcome->winner = MatchWinner::HUNTER;
	TESTTHROWS(encode_world_state(invalid_winner));
	invalid_winner = winner_state;
	// The obsolete tower-count victory code must not be accepted.
	invalid_winner.match_outcome->reason = static_cast<MatchEndReason>(3);
	TESTTHROWS(encode_world_state(invalid_winner));
	invalid_winner.match_outcome->reason = static_cast<MatchEndReason>(99);
	TESTTHROWS(encode_world_state(invalid_winner));
	invalid_winner = winner_state;
	invalid_winner.match_outcome->ended_at = time::time_t::from_int(99);
	TESTTHROWS(encode_world_state(invalid_winner));
	// v17 always includes 12 bytes of result metadata, with a flag that
	// distinguishes an absent outcome from the legitimate zero timestamp.
	auto invalid_winner_wire = winner_bytes;
	invalid_winner_wire[57] = 9; // Invalid winner enum in the fixed header.
	TESTTHROWS(decode_world_state(invalid_winner_wire));
	auto false_outcome = encode_world_state(state);
	false_outcome[57] = 1; // Result fields without outcome flag.
	TESTTHROWS(decode_world_state(false_outcome));
	auto expired_spawn = state;
	expired_spawn.first_hunter_spawn_time = time::time_t::from_int(3);
	TESTTHROWS(encode_world_state(expired_spawn));
	auto invalid_focus_order = state;
	invalid_focus_order.attack_orders.front().target_id.reset();
	TESTTHROWS(encode_world_state(invalid_focus_order));
	auto duplicate_horse_order = state;
	duplicate_horse_order.attack_orders.push_back(state.attack_orders.front());
	TESTTHROWS(encode_world_state(duplicate_horse_order));

	// Curves cannot include past, simultaneous, unordered or unbounded
	// keyframes; this is checked before transmission to every client.
	auto past_motion = state;
	past_motion.entities.front().motion_keys.front().time = state.simulation_time;
	TESTTHROWS(encode_world_state(past_motion));
	auto unordered_motion = state;
	unordered_motion.entities.front().motion_keys[1].time =
		unordered_motion.entities.front().motion_keys[0].time;
	TESTTHROWS(encode_world_state(unordered_motion));
	auto oversized_motion = state;
	oversized_motion.entities.front().motion_keys.resize(MAX_ENTITY_MOTION_KEYS + 1);
	TESTTHROWS(encode_world_state(oversized_motion));
	auto past_facing = state;
	past_facing.entities.front().facing_keys.front().time = state.simulation_time;
	TESTTHROWS(encode_world_state(past_facing));
	auto unordered_facing = state;
	unordered_facing.entities.front().facing_keys[1].time =
		unordered_facing.entities.front().facing_keys[0].time;
	TESTTHROWS(encode_world_state(unordered_facing));
	auto excessive_facing = state;
	excessive_facing.entities.front().facing_keys.resize(MAX_ENTITY_FACING_KEYS + 1);
	TESTTHROWS(encode_world_state(excessive_facing));
	auto future_animation = state;
	future_animation.entities.front().animation_started_at =
		time::time_t::from_int(4);
	TESTTHROWS(encode_world_state(future_animation));
	auto unknown_animation = state;
	unknown_animation.entities.front().animation_action =
		static_cast<gamestate::AnimationAction>(77);
	TESTTHROWS(encode_world_state(unknown_animation));

	// A timed build uses the authoritative phase interval, not packet arrival.
	auto building = state.constructions.front();
	building.phase = ConstructionPhase::UNDER_CONSTRUCTION;
	building.phase_start_time = time::time_t::from_double(3.5);
	TESTEQUALS(building.progress_percent(time::time_t::from_double(3.0)), 0);
	TESTEQUALS(building.progress_percent(time::time_t::from_double(4.0)), 50);
	TESTEQUALS(building.progress_percent(time::time_t::from_double(5.0)), 100);
	auto building_state = state;
	building_state.constructions.front() = building;
	auto roundtrip_build = decode_world_state(encode_world_state(building_state));
	TESTEQUALS(roundtrip_build.constructions.front().phase_start_time,
	           time::time_t::from_double(3.5));
	auto invalid_planned = state;
	invalid_planned.constructions.front().phase_start_time = time::time_t::from_int(4);
	TESTTHROWS(encode_world_state(invalid_planned));
	auto invalid_building = building_state;
	invalid_building.constructions.front().phase_start_time = time::time_t::from_double(4.5);
	TESTTHROWS(encode_world_state(invalid_building));

	auto truncated_state = encode_world_state(state);
	truncated_state.resize(truncated_state.size() - 8);
	TESTTHROWS(decode_world_state(truncated_state));

	auto trailing_state = encode_world_state(state);
	trailing_state.push_back(0);
	TESTTHROWS(decode_world_state(trailing_state));
	auto unsupported_player_flags = encode_world_state(state);
	unsupported_player_flags[79] |= 2;
	TESTTHROWS(decode_world_state(unsupported_player_flags));
	auto invalid_health = state;
	invalid_health.entities.front().health = 26;
	TESTTHROWS(encode_world_state(invalid_health));
	auto foreign_local_player = state;
	foreign_local_player.local_player_id = 8;
	TESTTHROWS(encode_world_state(foreign_local_player));
	auto invalid_arrow = state;
	invalid_arrow.arrows.front().impact_time = invalid_arrow.arrows.front().launch_time;
	TESTTHROWS(encode_world_state(invalid_arrow));
	auto score_leak = state;
	score_leak.entities.back().tower_kills = 7;
	TESTTHROWS(encode_world_state(score_leak));
	auto nonscore_entity = state;
	nonscore_entity.entities.front().tower_kills = 1;
	TESTTHROWS(encode_world_state(nonscore_entity));

	WorldState maximum_state{};
	maximum_state.map_size = 1;
	maximum_state.local_player_id = 1;
	maximum_state.players = {{1, PlayerRole::HUNTER}};
	maximum_state.entities.resize(MAX_STATE_ENTITIES + 1);
	TESTTHROWS(encode_world_state(maximum_state));
	WorldState missing_map{};
	TESTTHROWS(encode_world_state(missing_map));

	SnapshotRequest snapshot_request{17};
	auto encoded_snapshot_request = encode_snapshot_request(snapshot_request);
	TESTEQUALS(encoded_snapshot_request.size(), size_t{8});
	TESTEQUALS(decode_snapshot_request(encoded_snapshot_request).last_applied_revision,
	           snapshot_request.last_applied_revision);
	encoded_snapshot_request.push_back(0);
	TESTTHROWS(decode_snapshot_request(encoded_snapshot_request));

	ParticipantDisconnected disconnected{3, DisconnectReason::CONNECTION_LOST};
	auto encoded_disconnected = encode_participant_disconnected(disconnected);
	TESTEQUALS(encoded_disconnected.size(), size_t{12});
	auto decoded_disconnected = decode_participant_disconnected(encoded_disconnected);
	TESTEQUALS(decoded_disconnected.player_id, disconnected.player_id);
	decoded_disconnected.reason == disconnected.reason or TESTFAIL;
	TESTTHROWS(encode_participant_disconnected(
		{0, DisconnectReason::CONNECTION_LOST}));
	TESTTHROWS(encode_participant_disconnected(
		{3, static_cast<DisconnectReason>(100)}));
	auto invalid_disconnect_flags = encoded_disconnected;
	invalid_disconnect_flags[11] = 1;
	TESTTHROWS(decode_participant_disconnected(invalid_disconnect_flags));
	auto trailing_disconnected = encoded_disconnected;
	trailing_disconnected.push_back(0);
	TESTTHROWS(decode_participant_disconnected(trailing_disconnected));
}

void stance_messages() {
	CommandIntent stance{7, CommandKind::SET_STANCE, {100, 101}};
	stance.stance = gamestate::HunterStance::DEFENSIVE;
	auto encoded_stance = encode_command_intent(stance);
	auto decoded_stance = decode_command_intent(encoded_stance);
	decoded_stance.kind == CommandKind::SET_STANCE or TESTFAIL;
	TESTEQUALS(decoded_stance.entity_ids.size(), size_t{2});
	(decoded_stance.stance == gamestate::HunterStance::DEFENSIVE) or TESTFAIL;

	// A stance belongs to exactly the SET_STANCE command.
	CommandIntent missing_stance{8, CommandKind::SET_STANCE, {100}};
	TESTTHROWS(encode_command_intent(missing_stance));
	CommandIntent misplaced_stance{9, CommandKind::MOVE, {100}};
	misplaced_stance.stance = gamestate::HunterStance::AGGRESSIVE;
	TESTTHROWS(encode_command_intent(misplaced_stance));
	CommandIntent unknown_stance{10, CommandKind::SET_STANCE, {100}};
	unknown_stance.stance = static_cast<gamestate::HunterStance>(3);
	TESTTHROWS(encode_command_intent(unknown_stance));
	auto forged_stance = encoded_stance;
	forged_stance[forged_stance.size() - 1] = 3;
	TESTTHROWS(decode_command_intent(forged_stance));
	auto truncated_stance = encoded_stance;
	truncated_stance.pop_back();
	TESTTHROWS(decode_command_intent(truncated_stance));

	CommandIntent patrol{11, CommandKind::PATROL, {100}, {20.0, 30.0, 0}};
	auto decoded_patrol = decode_command_intent(encode_command_intent(patrol));
	decoded_patrol.kind == CommandKind::PATROL or TESTFAIL;
	TESTEQUALS(decoded_patrol.target == patrol.target, true);
	TESTEQUALS(decoded_patrol.stance.has_value(), false);

	// A rally point names exactly the Hunter tower and the clicked point.
	CommandIntent rally{12, CommandKind::SET_RALLY_POINT, {5}, {80.5, 64.5, 0}};
	auto decoded_rally = decode_command_intent(encode_command_intent(rally));
	decoded_rally.kind == CommandKind::SET_RALLY_POINT or TESTFAIL;
	TESTEQUALS(decoded_rally.target == rally.target, true);
	TESTEQUALS(decoded_rally.entity_ids.size(), size_t{1});
	CommandIntent rally_without_tower{13, CommandKind::SET_RALLY_POINT, {}, {80.5, 64.5, 0}};
	TESTTHROWS(encode_command_intent(rally_without_tower));
	CommandIntent rally_with_horses{14, CommandKind::SET_RALLY_POINT, {5, 100}, {80.5, 64.5, 0}};
	TESTTHROWS(encode_command_intent(rally_with_horses));

	// Stances travel only to the Hunter that owns the horses.
	WorldState hunter{};
	hunter.base_revision = 1;
	hunter.world_revision = 2;
	hunter.map_size = 128;
	hunter.local_player_id = 1;
	hunter.players = {{1, PlayerRole::HUNTER}, {2, PlayerRole::BUILDER}};
	hunter.entities = {
		{100, 1, "fvd.data.entities.Horse", {10.0, 20.0, 0}},
		{101, 1, "fvd.data.entities.Horse", {11.0, 20.0, 0}},
	};
	hunter.entities[0].hunter_stance = gamestate::HunterStance::AGGRESSIVE;
	hunter.entities[1].hunter_stance = gamestate::HunterStance::OBSERVE;
	auto decoded_hunter = decode_world_state(encode_world_state(hunter));
	TESTEQUALS(decoded_hunter.entities.size(), size_t{2});
	(decoded_hunter.entities[0].hunter_stance == gamestate::HunterStance::AGGRESSIVE) or TESTFAIL;
	(decoded_hunter.entities[1].hunter_stance == gamestate::HunterStance::OBSERVE) or TESTFAIL;

	auto builder = hunter;
	builder.local_player_id = 2;
	TESTTHROWS(encode_world_state(builder));
	builder.entities[0].hunter_stance.reset();
	builder.entities[1].hunter_stance.reset();
	auto decoded_builder = decode_world_state(encode_world_state(builder));
	TESTEQUALS(decoded_builder.entities[0].hunter_stance.has_value(), false);
	TESTEQUALS(decoded_builder.entities[1].hunter_stance.has_value(), false);

	auto tower_stance = hunter;
	tower_stance.entities[1].definition = "fvd.data.entities.Tower";
	TESTTHROWS(encode_world_state(tower_stance));
}

void fog_messages() {
	WorldState builder{};
	builder.base_revision = 1;
	builder.world_revision = 2;
	builder.map_size = 16;
	builder.local_player_id = 2;
	builder.players = {{1, PlayerRole::HUNTER}, {2, PlayerRole::BUILDER}};
	TESTEQUALS(fog_word_count(16), size_t{4});
	TESTEQUALS(fog_word_count(128), size_t{256});
	TeamFog fog{PlayerRole::BUILDER, std::vector<uint64_t>(4, 0), std::vector<uint64_t>(4, 0), {}};
	fog.explored[0] = 0b111;
	fog.visible[0] = 0b011;
	builder.fog = {fog};
	builder.entities = {{300, 1, "fvd.data.entities.Horse", {1.0, 1.0, 0}}};
	auto decoded = decode_world_state(encode_world_state(builder));
	TESTEQUALS(decoded.fog.size(), size_t{1});
	TESTEQUALS(decoded.fog.front() == fog, true);
	TESTEQUALS(fog_tile_set(decoded.fog.front().visible, 16, {0, 1}), true);
	TESTEQUALS(fog_tile_set(decoded.fog.front().visible, 16, {0, 2}), false);
	TESTEQUALS(fog_tile_set(decoded.fog.front().explored, 16, {0, 2}), true);
	TESTEQUALS(fog_tile_set(decoded.fog.front().explored, 16, {16, 0}), false);

	// A visible tile is always explored, bitsets match the map, and a
	// recipient only ever receives its own side's fog.
	auto unexplored = builder;
	unexplored.fog.front().visible[1] = 1;
	TESTTHROWS(encode_world_state(unexplored));
	auto short_fog = builder;
	short_fog.fog.front().explored.pop_back();
	TESTTHROWS(encode_world_state(short_fog));
	auto both_sides = builder;
	both_sides.fog.push_back(fog);
	TESTTHROWS(encode_world_state(both_sides));
	auto hunter_fog = builder;
	hunter_fog.fog.front().team = PlayerRole::HUNTER;
	TESTTHROWS(encode_world_state(hunter_fog));
	auto local_only = builder;
	local_only.fog.front().remembered = {300};
	TESTTHROWS(encode_world_state(local_only));

	// Remembered entities are bare enemy buildings.
	WorldState hunter = builder;
	hunter.local_player_id = 1;
	hunter.fog.front().team = PlayerRole::HUNTER;
	hunter.entities = {{400, 2, "fvd.data.entities.Tower", {5.0, 5.0, 0}}};
	hunter.entities.front().remembered = true;
	auto remembered = decode_world_state(encode_world_state(hunter));
	TESTEQUALS(remembered.entities.front().remembered, true);
	auto with_health = hunter;
	with_health.entities.front().health = 50;
	with_health.entities.front().maximum_health = 100;
	TESTTHROWS(encode_world_state(with_health));
	auto own_building = hunter;
	own_building.entities.front().owner_id = 1;
	TESTTHROWS(encode_world_state(own_building));
	auto remembered_unit = hunter;
	remembered_unit.entities.front().definition = "fvd.data.entities.Builder";
	TESTTHROWS(encode_world_state(remembered_unit));

	// A delta carries fog only when it changed.
	auto next = builder;
	next.world_revision = 3;
	next.base_revision = 0;
	auto unchanged = make_world_delta(builder, next);
	TESTEQUALS(unchanged.changed.fog.empty(), true);
	next.fog.front().explored[2] = 1;
	auto changed = make_world_delta(builder, next);
	TESTEQUALS(changed.changed.fog.size(), size_t{1});
	auto applied = apply_world_delta(builder, decode_world_delta(encode_world_delta(changed)));
	TESTEQUALS(applied.fog.front() == next.fog.front(), true);
}

} // namespace openage::network::tests
