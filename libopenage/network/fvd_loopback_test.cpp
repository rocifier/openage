// Copyright 2026 the openage authors. See copying.md for legal info.

#include "command_bridge.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "assets/mod_manager.h"
#include "config.h"
#include "error/error.h"
#include "event/event_loop.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/process_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/wait.h"
#include "gamestate/fvd.h"
#include "gamestate/fvd_test_support.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/terrain_factory.h"
#include "network/command_mailbox.h"
#include "network/connection_mailbox.h"
#include "network/handshake.h"
#include "network/match_connection_state.h"
#include "network/match_disconnect_coordinator.h"
#include "network/messages.h"
#include "network/protocol.h"
#include "network/replication.h"
#include "network/construction_presentation.h"
#include "network/world_delta.h"
#include "network/session.h"
#include "network/transport.h"
#include "testing/testing.h"
#include "util/fslike/directory.h"
#include "util/path.h"

namespace openage::network::tests {

namespace {

AssetDigest test_asset_digest() {
	AssetDigest digest{};
	for (size_t i = 0; i < digest.size(); ++i) {
		digest[i] = static_cast<uint8_t>(i);
	}
	return digest;
}

const EntityState &find_entity(const WorldState &state,
                                  gamestate::entity_id_t entity_id) {
	auto found = std::find_if(
		state.entities.begin(), state.entities.end(),
		[entity_id](const auto &entity) { return entity.entity_id == entity_id; });
	found != state.entities.end() or TESTFAIL;
	return *found;
}

const EntityState &find_owned_entity(const WorldState &state,
                                        gamestate::player_id_t owner_id) {
	auto found = std::find_if(
		state.entities.begin(), state.entities.end(),
		[owner_id](const auto &entity) { return entity.owner_id == owner_id; });
	found != state.entities.end() or TESTFAIL;
	return *found;
}

const EntityState &find_defined_entity(const WorldState &state,
                                          const std::string &definition) {
	auto found = std::find_if(
		state.entities.begin(), state.entities.end(),
		[&definition](const auto &entity) { return entity.definition == definition; });
	found != state.entities.end() or TESTFAIL;
	return *found;
}

size_t count_defined_entities(const WorldState &state,
                              const std::string &definition) {
	return static_cast<size_t>(std::count_if(
		state.entities.begin(), state.entities.end(),
		[&definition](const auto &entity) { return entity.definition == definition; }));
}

const PlayerState &find_player(const WorldState &state,
                               gamestate::player_id_t player_id) {
	auto found = std::find_if(
		state.players.begin(), state.players.end(),
		[player_id](const auto &player) { return player.player_id == player_id; });
	found != state.players.end() or TESTFAIL;
	return *found;
}

size_t count_owned_entities(const WorldState &state,
                            gamestate::player_id_t owner_id) {
	return static_cast<size_t>(std::count_if(
		state.entities.begin(), state.entities.end(),
		[owner_id](const auto &entity) { return entity.owner_id == owner_id; }));
}

void run_host(TcpListener &listener, bool test_hunter_session) {
	auto root = util::Path{
		std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir)};
	auto mods = std::make_shared<assets::ModManager>(root / "assets");
	mods->register_modpack(root / "assets" / "fvd" / "engine_api" / "modpack.toml");
	mods->register_modpack(root / "assets" / "fvd" / "modpack.toml");
	mods->activate_modpacks({"engine", "fvd"});
	auto loop = std::make_shared<event::EventLoop>();
	loop->add_event_handler(std::make_shared<gamestate::event::SendCommandHandler>());
	loop->add_event_handler(std::make_shared<gamestate::event::ProcessCommandHandler>());
	loop->add_event_handler(std::make_shared<gamestate::event::WaitHandler>());
	auto entities = std::make_shared<gamestate::EntityFactory>();
	auto terrain = std::make_shared<gamestate::TerrainFactory>();
	gamestate::Game game{loop, mods, entities, terrain};
	auto state = game.get_state();
	gamestate::tests::use_centre_test_layout(state);
	auto commander = std::make_shared<gamestate::event::Commander>(loop);
	MatchConnectionState connections{state->get_fvd()->get_player_count()};
	MatchDisconnectCoordinator lifecycle_coordinator{connections};

	std::optional<gamestate::entity_id_t> builder_id{};
	std::optional<gamestate::entity_id_t> other_builder_id{};
	for (const auto &[entity_id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<gamestate::component::Ownership>(
			entity->get_component(gamestate::component::component_t::OWNERSHIP));
		auto owner = ownership->get_owners().get(time::TIME_ZERO);
		if (owner == 2) {
			builder_id = entity_id;
		}
		else if (owner == 3) {
			other_builder_id = entity_id;
		}
	}
	builder_id.has_value() or TESTFAIL;
	other_builder_id.has_value() or TESTFAIL;
	auto horse = entities->spawn_game_entity(
		loop,
		state,
		1,
		"fvd.data.entities.Horse",
		state->get_fvd()->get_centre(),
		time::TIME_ZERO);
	state->get_fvd()->start_hunter_wave(state, time::TIME_ZERO);

	auto connection = listener.accept();
	auto accepted_session = host_handshake(connection, 2, test_asset_digest());
	accepted_session.has_value() or TESTFAIL;
	auto &session = *accepted_session;
	OutboundMailbox builder_outbound{4};
	lifecycle_coordinator.register_remote(session.get_identity(), builder_outbound);
	CommandMailbox command_mailbox{16};
	auto initial_state = capture_fvd_state(state, connections, time::TIME_ZERO, 0, 0, 2);
	TESTEQUALS(initial_state.first_hunter_spawn_time.value(),
	           time::time_t::from_int(20));
	WorldDeltaBaseline builder_baseline{};
	builder_baseline.accept_snapshot(initial_state);
	lifecycle_coordinator.record_snapshot(initial_state);
	auto encode_builder_delta = [&](WorldState complete) {
		return encode_world_delta(builder_baseline.advance(std::move(complete)));
	};
	auto snapshot_frame = session.make_frame(
		MessageType::WORLD_SNAPSHOT,
		encode_world_state(initial_state));
	connection.send_frame(snapshot_frame);
	auto retained_time = time::time_t::from_int(40);

	auto command_frame = connection.receive_frame();
	command_frame.has_value() or TESTFAIL;
	session.validate_incoming(*command_frame);
	command_frame->message_type == MessageType::COMMAND_INTENT or TESTFAIL;
	command_mailbox.try_push({
		session.get_identity(),
		decode_command_intent(command_frame->payload)})
		or TESTFAIL;
	auto commands = command_mailbox.drain(1);
	TESTEQUALS(commands.size(), size_t{1});
	auto &command = commands.front();
	auto &intent = command.intent;

	std::optional<bool> accepted{};
	submit_authenticated_command(
		command, connections,
		loop,
		commander,
		state,
		time::TIME_ZERO,
		[&](bool result) { accepted = result; });
	loop->reach_time(time::TIME_ZERO, state);
	accepted.has_value() or TESTFAIL;

	CommandResult command_result{};
	command_result.command_id = intent.command_id;
	command_result.status = *accepted ? CommandStatus::ACCEPTED : CommandStatus::REJECTED;
	command_result.world_revision = *accepted ? 1 : 0;
	command_result.simulation_time = time::TIME_ZERO;
	command_result.reason = *accepted ? "" : "command rejected by authoritative simulation";
	auto result_frame = session.make_frame(
		MessageType::COMMAND_RESULT,
		encode_command_result(command_result));
	connection.send_frame(result_frame);

	if (*accepted) {
		auto before_wave_time = time::time_t::from_int(19);
		loop->reach_time(before_wave_time, state);
		auto before_wave = decode_world_state(encode_world_state(
			capture_fvd_state(state, connections, before_wave_time, 0, 1, 2)));
		TESTEQUALS(count_defined_entities(
			before_wave, "fvd.data.entities.Horse"), size_t{1});
		TESTEQUALS(before_wave.first_hunter_spawn_time.value(),
		           time::time_t::from_int(20));
		before_wave.attack_orders.empty() or TESTFAIL;

		auto first_spawn_time = time::time_t::from_int(20);
		loop->reach_time(first_spawn_time, state);
		auto first_spawn = decode_world_state(encode_world_state(
			capture_fvd_state(state, connections, first_spawn_time, 0, 1, 2)));
		TESTEQUALS(count_defined_entities(
			first_spawn, "fvd.data.entities.Horse"), size_t{2});
		TESTEQUALS(first_spawn.first_hunter_spawn_time.has_value(), false);
		first_spawn.attack_orders.empty() or TESTFAIL;

		auto second_spawn_time = time::time_t::from_int(21);
		loop->reach_time(second_spawn_time, state);
		auto second_spawn = decode_world_state(encode_world_state(
			capture_fvd_state(state, connections, second_spawn_time, 0, 1, 2)));
		TESTEQUALS(count_defined_entities(
			second_spawn, "fvd.data.entities.Horse"), size_t{3});
		second_spawn.attack_orders.empty() or TESTFAIL;

		auto sample_time = time::time_t::from_int(30);
		loop->reach_time(sample_time, state);
		auto delta_frame = session.make_frame(
			MessageType::WORLD_DELTA,
			encode_builder_delta(capture_fvd_state(state, connections, sample_time, 0, 1, 2)));
		connection.send_frame(delta_frame);

		auto spoofed_frame = connection.receive_frame();
		spoofed_frame.has_value() or TESTFAIL;
		session.validate_incoming(*spoofed_frame);
		spoofed_frame->message_type == MessageType::COMMAND_INTENT or TESTFAIL;
		command_mailbox.try_push({
			session.get_identity(),
			decode_command_intent(spoofed_frame->payload)})
			or TESTFAIL;
		auto spoofed_commands = command_mailbox.drain(1);
		TESTEQUALS(spoofed_commands.size(), size_t{1});
		auto &spoofed_command = spoofed_commands.front();
		auto &spoofed = spoofed_command.intent;
		auto before = capture_fvd_state(state, connections, sample_time, 0, 1, 2);
		std::optional<bool> spoofed_accepted{};
		submit_authenticated_command(
			spoofed_command, connections,
			loop,
			commander,
			state,
			sample_time,
			[&](bool result) { spoofed_accepted = result; });
		loop->reach_time(sample_time, state);
		spoofed_accepted.has_value() or TESTFAIL;
		TESTEQUALS(*spoofed_accepted, false);
		auto after = capture_fvd_state(state, connections, sample_time, 0, 1, 2);
		TESTEQUALS(find_entity(after, spoofed.entity_ids.front()).position,
		           find_entity(before, spoofed.entity_ids.front()).position);

		CommandResult spoofed_result{};
		spoofed_result.command_id = spoofed.command_id;
		spoofed_result.status = CommandStatus::REJECTED;
		spoofed_result.world_revision = 1;
		spoofed_result.simulation_time = sample_time;
		spoofed_result.reason = "entity is not owned by this session";
		auto spoofed_result_frame = session.make_frame(
			MessageType::COMMAND_RESULT,
			encode_command_result(spoofed_result));
		connection.send_frame(spoofed_result_frame);

		auto reject_forged_hunter_command = [&](CommandKind expected_kind) {
			auto forged_hunter_frame = connection.receive_frame();
			forged_hunter_frame.has_value() or TESTFAIL;
			session.validate_incoming(*forged_hunter_frame);
			forged_hunter_frame->message_type == MessageType::COMMAND_INTENT or TESTFAIL;
			command_mailbox.try_push({
				session.get_identity(),
				decode_command_intent(forged_hunter_frame->payload)})
				or TESTFAIL;
			auto forged_hunter_commands = command_mailbox.drain(1);
			TESTEQUALS(forged_hunter_commands.size(), size_t{1});
			auto &forged_hunter = forged_hunter_commands.front();
			TESTEQUALS(static_cast<uint16_t>(forged_hunter.intent.kind),
			           static_cast<uint16_t>(expected_kind));
			auto horse_before = capture_fvd_state(state, connections, sample_time, 0, 1, 2);
			std::optional<bool> forged_hunter_accepted{};
			submit_authenticated_command(
				forged_hunter, connections,
				loop,
				commander,
				state,
				sample_time,
				[&](bool result) { forged_hunter_accepted = result; });
			loop->reach_time(sample_time, state);
			forged_hunter_accepted.has_value() or TESTFAIL;
			TESTEQUALS(*forged_hunter_accepted, false);
			auto horse_after = capture_fvd_state(state, connections, sample_time, 0, 1, 2);
			TESTEQUALS(find_entity(horse_after, forged_hunter.intent.entity_ids.front()).position,
			           find_entity(horse_before, forged_hunter.intent.entity_ids.front()).position);

			CommandResult forged_hunter_result{};
			forged_hunter_result.command_id = forged_hunter.intent.command_id;
			forged_hunter_result.status = CommandStatus::REJECTED;
			forged_hunter_result.world_revision = 1;
			forged_hunter_result.simulation_time = sample_time;
			forged_hunter_result.reason = "entity is not owned by this session";
			auto forged_hunter_result_frame = session.make_frame(
				MessageType::COMMAND_RESULT,
				encode_command_result(forged_hunter_result));
			connection.send_frame(forged_hunter_result_frame);
		};
		reject_forged_hunter_command(CommandKind::ATTACK_MOVE);
		reject_forged_hunter_command(CommandKind::MOVE);

		auto build_frame = connection.receive_frame();
		build_frame.has_value() or TESTFAIL;
		session.validate_incoming(*build_frame);
		build_frame->message_type == MessageType::COMMAND_INTENT or TESTFAIL;
		command_mailbox.try_push({
			session.get_identity(),
			decode_command_intent(build_frame->payload)})
			or TESTFAIL;
		auto build_commands = command_mailbox.drain(1);
		TESTEQUALS(build_commands.size(), size_t{1});
		auto &build = build_commands.front();
		std::optional<bool> build_accepted{};
		submit_authenticated_command(
			build, connections, loop, commander, state, sample_time,
			[&](bool result) { build_accepted = result; });
		loop->reach_time(sample_time, state);
		build_accepted.has_value() or TESTFAIL;
		TESTEQUALS(*build_accepted, true);

		CommandResult build_result{};
		build_result.command_id = build.intent.command_id;
		build_result.status = CommandStatus::ACCEPTED;
		build_result.world_revision = 2;
		build_result.simulation_time = sample_time;
		connection.send_frame(session.make_frame(
			MessageType::COMMAND_RESULT,
			encode_command_result(build_result)));

		auto planned = capture_fvd_state(state, connections, sample_time, 1, 2, 2);
		TESTEQUALS(planned.constructions.size(), size_t{1});
		planned.constructions.front().phase == ConstructionPhase::PLANNED
			or TESTFAIL;
		not planned.reservations.empty() or TESTFAIL;
		connection.send_frame(session.make_frame(
			MessageType::WORLD_DELTA,
			encode_builder_delta(planned)));

		auto arrival = planned.constructions.front().phase_end_time;
		TESTEQUALS(planned.constructions.front().phase_start_time, arrival);
		loop->reach_time(arrival, state);
		auto constructing = capture_fvd_state(state, connections, arrival, 2, 3, 2);
		TESTEQUALS(constructing.constructions.size(), size_t{1});
		constructing.constructions.front().phase
			== ConstructionPhase::UNDER_CONSTRUCTION
			or TESTFAIL;
		TESTEQUALS(constructing.constructions.front().phase_start_time, arrival);
		TESTEQUALS(constructing.constructions.front().progress_percent(arrival), 0);
		connection.send_frame(session.make_frame(
			MessageType::WORLD_DELTA,
			encode_builder_delta(constructing)));

		auto completion = constructing.constructions.front().phase_end_time;
		loop->reach_time(completion, state);
		auto completed = capture_fvd_state(state, connections, completion, 3, 4, 2);
		completed.constructions.empty() or TESTFAIL;
		completed.reservations.empty() or TESTFAIL;
		find_defined_entity(completed, "fvd.data.entities.MedievalTowerLevel1");
		connection.send_frame(session.make_frame(
			MessageType::WORLD_DELTA,
			encode_builder_delta(completed)));

		const auto &tower = find_defined_entity(
			completed, "fvd.data.entities.MedievalTowerLevel1");
		TESTEQUALS(tower.tower_kills.value(), uint64_t{0});
		auto victim_position = tower.position;
		victim_position.ne += coord::phys_t::from_double(0.5);
		auto victim = entities->spawn_game_entity(
			loop, state, 1, "fvd.data.entities.Horse",
			victim_position, completion);
		auto spawned = capture_fvd_state(state, connections, completion, 4, 5, 2);
		const auto &spawned_victim = find_entity(spawned, victim->get_id());
		spawned_victim.health.has_value() or TESTFAIL;
		spawned_victim.maximum_health.has_value() or TESTFAIL;
		TESTEQUALS(*spawned_victim.health, *spawned_victim.maximum_health);
		TESTEQUALS(*spawned_victim.health > 0, true);
		TESTEQUALS(spawned.local_player_id, gamestate::player_id_t{2});
		TESTEQUALS(spawned.local_kill_count, uint64_t{0});
		spawned.arrows.empty() or TESTFAIL;
		connection.send_frame(session.make_frame(
			MessageType::WORLD_DELTA,
			encode_builder_delta(spawned)));

		state->get_fvd()->combat_tick(state, completion);
		auto fired = capture_fvd_state(state, connections, completion, 5, 6, 2);
		auto first_arrow = std::find_if(
			fired.arrows.begin(), fired.arrows.end(),
			[&](const auto &arrow) { return arrow.target_id == victim->get_id(); });
		first_arrow != fired.arrows.end() or TESTFAIL;
		TESTEQUALS(first_arrow->owner_id, gamestate::player_id_t{2});
		connection.send_frame(session.make_frame(
			MessageType::WORLD_DELTA,
			encode_builder_delta(fired)));

		loop->reach_time(first_arrow->impact_time, state);
		auto impacted = capture_fvd_state(
			state, connections, first_arrow->impact_time, 6, 7, 2);
		const auto &wounded = find_entity(impacted, victim->get_id());
		wounded.health.has_value() or TESTFAIL;
		TESTEQUALS(*wounded.health,
		           *spawned_victim.health - state->get_fvd()->get_tower_damage());
		TESTEQUALS(impacted.arrows.size(), size_t{1});
		TESTEQUALS(arrow_visible_at(impacted.arrows.front(), first_arrow->impact_time),
		           false);
		TESTEQUALS(impacted.local_kill_count, uint64_t{0});
		connection.send_frame(session.make_frame(
			MessageType::WORLD_DELTA,
			encode_builder_delta(impacted)));

		auto next_launch = first_arrow->launch_time
		                 + time::time_t::from_double(0.8);
		const auto tower_damage = state->get_fvd()->get_tower_damage();
		const auto shots_to_kill = (*spawned_victim.health + tower_damage - 1)
		                         / tower_damage;
		for (int64_t shot = 1;
		     shot < shots_to_kill and state->get_game_entities().contains(victim->get_id());
		     ++shot) {
			loop->reach_time(next_launch, state);
			state->get_fvd()->combat_tick(state, next_launch);
			auto arrows = state->get_fvd()->get_arrow_state(next_launch);
			auto arrow = std::find_if(
				arrows.begin(), arrows.end(),
				[&](const auto &candidate) {
					return candidate.target_id == victim->get_id()
					    and candidate.launch_time == next_launch;
				});
			arrow != arrows.end() or TESTFAIL;
			loop->reach_time(arrow->impact_time, state);
			next_launch = arrow->launch_time + time::time_t::from_double(0.8);
		}
		state->get_game_entities().contains(victim->get_id()) and TESTFAIL;
		auto killed = capture_fvd_state(
			state, connections,
			next_launch - time::time_t::from_double(0.6), 7, 8, 2);
		if (killed.simulation_time > retained_time) {
			retained_time = killed.simulation_time;
		}
		TESTEQUALS(std::none_of(
			killed.entities.begin(), killed.entities.end(),
			[&](const auto &entity) { return entity.entity_id == victim->get_id(); }),
		           true);
		TESTEQUALS(killed.local_kill_count, uint64_t{1});
		TESTEQUALS(find_entity(killed, tower.entity_id).tower_kills.value(),
		           uint64_t{1});
		auto other_recipient = capture_fvd_state(
			state, connections, next_launch - time::time_t::from_double(0.6),
			0, 8, 3);
		TESTEQUALS(other_recipient.local_kill_count, uint64_t{0});
		TESTEQUALS(find_entity(other_recipient, tower.entity_id).tower_kills.has_value(),
		           false);
		connection.send_frame(session.make_frame(
			MessageType::WORLD_DELTA,
			encode_builder_delta(killed)));
	}

	auto retained_before = capture_fvd_state(
		state, connections, retained_time, 0, 8, 1);
	auto retained_builder_entities = count_owned_entities(retained_before, 2);
	auto retained_tower = find_defined_entity(
		retained_before, "fvd.data.entities.MedievalTowerLevel1");
	connections.accepts_commands(session.get_identity()) or TESTFAIL;
	auto after_close = connection.receive_frame();
	not after_close.has_value() or TESTFAIL;
	ConnectionEventMailbox lifecycle{1};
	lifecycle.try_push({
		session.get_identity(),
		DisconnectReason::CONNECTION_LOST})
		or TESTFAIL;
	uint64_t disconnect_revision = 8;
	TESTEQUALS(lifecycle_coordinator.process_closures(
		lifecycle, state, retained_time, disconnect_revision, 1),
		size_t{1});
	TESTEQUALS(disconnect_revision, uint64_t{9});
	auto disconnected = ParticipantDisconnected{
		session.get_identity().player_id, DisconnectReason::CONNECTION_LOST};
	not connections.accepts_commands(session.get_identity()) or TESTFAIL;
	TESTEQUALS(builder_outbound.size(), size_t{0});
	// A command decoded before the socket closed but drained afterwards
	// must fail without ever reaching movement or construction gameplay.
	AuthenticatedCommand buffered{};
	buffered.identity = session.get_identity();
	buffered.intent.command_id = 999;
	buffered.intent.kind = CommandKind::MOVE;
	buffered.intent.entity_ids = {*builder_id};
	buffered.intent.target = {64, 64, 0};
	bool buffered_accepted = true;
	submit_authenticated_command(
		buffered, connections, loop, commander, state,
		retained_time,
		[&](bool result) { buffered_accepted = result; });
	TESTEQUALS(buffered_accepted, false);
	TESTTHROWS(connections.bind_remote(session.get_identity()));
	auto retained_after = capture_fvd_state(
		state, connections, retained_time, 8, 9, 1);
	TESTEQUALS(find_player(retained_after, 2).connected, false);
	TESTEQUALS(count_owned_entities(retained_after, 2), retained_builder_entities);
	TESTEQUALS(find_entity(retained_after, retained_tower.entity_id).owner_id,
	           gamestate::player_id_t{2});
	TESTEQUALS(find_entity(retained_after, retained_tower.entity_id).position,
	           retained_tower.position);

	if (not test_hunter_session) {
		return;
	}

	auto hunter_connection = listener.accept();
	auto accepted_hunter_session = host_handshake(
		hunter_connection, 1, test_asset_digest());
	accepted_hunter_session.has_value() or TESTFAIL;
	auto &hunter_session = *accepted_hunter_session;
	OutboundMailbox hunter_outbound{4};
	lifecycle_coordinator.register_remote(
		hunter_session.get_identity(), hunter_outbound);
	auto hunter_time = retained_time;
	auto hunter_snapshot_state = capture_fvd_state(
		state, connections, hunter_time, 0, 9, 1);
	WorldDeltaBaseline hunter_baseline{};
	hunter_baseline.accept_snapshot(hunter_snapshot_state);
	lifecycle_coordinator.record_snapshot(hunter_snapshot_state);
	auto hunter_snapshot_frame = hunter_session.make_frame(
		MessageType::WORLD_SNAPSHOT,
		encode_world_state(hunter_snapshot_state));
	hunter_connection.send_frame(hunter_snapshot_frame);
	hunter_connection.send_frame(hunter_session.make_frame(
		MessageType::PARTICIPANT_DISCONNECTED,
		encode_participant_disconnected(disconnected)));

	auto attack_frame = hunter_connection.receive_frame();
	attack_frame.has_value() or TESTFAIL;
	hunter_session.validate_incoming(*attack_frame);
	attack_frame->message_type == MessageType::COMMAND_INTENT or TESTFAIL;
	command_mailbox.try_push({
		hunter_session.get_identity(),
		decode_command_intent(attack_frame->payload)})
		or TESTFAIL;
	auto attack_commands = command_mailbox.drain(1);
	TESTEQUALS(attack_commands.size(), size_t{1});
	auto &attack = attack_commands.front();
	std::optional<bool> attack_accepted{};
	submit_authenticated_command(
		attack, connections,
		loop,
		commander,
		state,
		hunter_time,
		[&](bool result) { attack_accepted = result; });
	loop->reach_time(hunter_time, state);
	attack_accepted.has_value() or TESTFAIL;
	TESTEQUALS(*attack_accepted, true);

	CommandResult attack_result{};
	attack_result.command_id = attack.intent.command_id;
	attack_result.status = CommandStatus::ACCEPTED;
	attack_result.world_revision = 10;
	attack_result.simulation_time = hunter_time;
	auto attack_result_frame = hunter_session.make_frame(
		MessageType::COMMAND_RESULT,
		encode_command_result(attack_result));
	hunter_connection.send_frame(attack_result_frame);

	auto attack_sample_time = hunter_time + time::time_t::from_int(1);
	loop->reach_time(attack_sample_time, state);
	auto attack_delta_frame = hunter_session.make_frame(
		MessageType::WORLD_DELTA,
		encode_world_delta(hunter_baseline.advance(capture_fvd_state(
			state, connections, attack_sample_time, 9, 10, 1))));
	hunter_connection.send_frame(attack_delta_frame);

	auto hunter_build_frame = hunter_connection.receive_frame();
	hunter_build_frame.has_value() or TESTFAIL;
	hunter_session.validate_incoming(*hunter_build_frame);
	hunter_build_frame->message_type == MessageType::COMMAND_INTENT or TESTFAIL;
	command_mailbox.try_push({
		hunter_session.get_identity(),
		decode_command_intent(hunter_build_frame->payload)})
		or TESTFAIL;
	auto hunter_build_commands = command_mailbox.drain(1);
	TESTEQUALS(hunter_build_commands.size(), size_t{1});
	auto &hunter_build = hunter_build_commands.front();
	auto entity_count = state->get_game_entities().size();
	std::optional<bool> hunter_build_accepted{};
	submit_authenticated_command(
		hunter_build, connections,
		loop,
		commander,
		state,
		attack_sample_time,
		[&](bool result) { hunter_build_accepted = result; });
	loop->reach_time(attack_sample_time, state);
	hunter_build_accepted.has_value() or TESTFAIL;
	TESTEQUALS(*hunter_build_accepted, false);
	TESTEQUALS(state->get_game_entities().size(), entity_count);
	TESTEQUALS(state->get_fvd()->is_tile_reserved({10, 10}), false);

	CommandResult hunter_build_result{};
	hunter_build_result.command_id = hunter_build.intent.command_id;
	hunter_build_result.status = CommandStatus::REJECTED;
	hunter_build_result.world_revision = 10;
	hunter_build_result.simulation_time = attack_sample_time;
	hunter_build_result.reason = "Hunter cannot construct towers";
	auto hunter_build_result_frame = hunter_session.make_frame(
		MessageType::COMMAND_RESULT,
		encode_command_result(hunter_build_result));
	hunter_connection.send_frame(hunter_build_result_frame);
}

void run_guest(uint16_t port) {
	auto client = TcpConnection::connect("127.0.0.1", port);
	auto session = client_handshake(
		client,
		{PROTOCOL_MINIMUM_COMPATIBLE_MINOR,
		 "Builder",
		 "fvd-test",
		 test_asset_digest()});
	auto snapshot_frame = client.receive_frame();
	snapshot_frame.has_value() or TESTFAIL;
	session.validate_incoming(*snapshot_frame);
	snapshot_frame->message_type == MessageType::WORLD_SNAPSHOT or TESTFAIL;
	auto snapshot = decode_world_state(snapshot_frame->payload);
	ReplicationTracker replication{session.get_identity().player_id};
	replication.accept_snapshot(snapshot);
	TESTEQUALS(replication.get_local_kills().value(), uint64_t{0});
	TESTEQUALS(snapshot.map_size, uint32_t{128});
	TESTEQUALS(snapshot.players.size(), size_t{8});
	TESTEQUALS(snapshot.entities.size(), size_t{8});
	TESTEQUALS(snapshot.local_player_id, gamestate::player_id_t{2});
	TESTEQUALS(snapshot.local_kill_count, uint64_t{0});
	auto initial = find_owned_entity(snapshot, 2);
	auto other = find_owned_entity(snapshot, 3);
	auto horse = find_owned_entity(snapshot, 1);
	auto target = coord::phys3{
		initial.position.ne,
		initial.position.se + coord::phys_t::from_int(3),
		initial.position.up};

	CommandIntent intent{};
	intent.command_id = 1;
	intent.kind = CommandKind::MOVE;
	intent.entity_ids = {initial.entity_id};
	intent.target = target;
	auto command_frame = session.make_frame(
		MessageType::COMMAND_INTENT,
		encode_command_intent(intent));
	client.send_frame(command_frame);

	auto result_frame = client.receive_frame();
	result_frame.has_value() or TESTFAIL;
	session.validate_incoming(*result_frame);
	result_frame->message_type == MessageType::COMMAND_RESULT or TESTFAIL;
	auto result = decode_command_result(result_frame->payload);
	TESTEQUALS(result.command_id, intent.command_id);
	result.status == CommandStatus::ACCEPTED or TESTFAIL;
	TESTEQUALS(result.world_revision, uint64_t{1});

	auto delta_frame = client.receive_frame();
	delta_frame.has_value() or TESTFAIL;
	session.validate_incoming(*delta_frame);
	delta_frame->message_type == MessageType::WORLD_DELTA or TESTFAIL;
	auto delta_patch = decode_world_delta(delta_frame->payload);
	replication.accept_delta(delta_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto delta = replication.get_world_state().value();
	TESTEQUALS(delta_patch.changed.base_revision, uint64_t{0});
	TESTEQUALS(delta.world_revision, uint64_t{1});
	TESTEQUALS(delta.entities.size() >= size_t{8}, true);
	TESTEQUALS(find_entity(delta, initial.entity_id).position, target);

	CommandIntent spoofed{};
	spoofed.command_id = 2;
	spoofed.kind = CommandKind::MOVE;
	spoofed.entity_ids = {other.entity_id};
	spoofed.target = {
		other.position.ne,
		other.position.se + coord::phys_t::from_int(3),
		other.position.up};
	auto spoofed_frame = session.make_frame(
		MessageType::COMMAND_INTENT,
		encode_command_intent(spoofed));
	client.send_frame(spoofed_frame);

	auto spoofed_result_frame = client.receive_frame();
	spoofed_result_frame.has_value() or TESTFAIL;
	session.validate_incoming(*spoofed_result_frame);
	spoofed_result_frame->message_type == MessageType::COMMAND_RESULT or TESTFAIL;
	auto spoofed_result = decode_command_result(spoofed_result_frame->payload);
	TESTEQUALS(spoofed_result.command_id, spoofed.command_id);
	spoofed_result.status == CommandStatus::REJECTED or TESTFAIL;
	TESTEQUALS(spoofed_result.world_revision, uint64_t{1});
	TESTEQUALS(spoofed_result.reason, "entity is not owned by this session");
	TESTEQUALS(replication.get_revision(), uint64_t{1});

	auto send_forged_hunter_command = [&](uint64_t command_id, CommandKind kind) {
		CommandIntent forged_hunter{};
		forged_hunter.command_id = command_id;
		forged_hunter.kind = kind;
		forged_hunter.entity_ids = {horse.entity_id};
		forged_hunter.target = {
			horse.position.ne,
			horse.position.se - coord::phys_t::from_int(3),
			horse.position.up};
		auto forged_hunter_frame = session.make_frame(
			MessageType::COMMAND_INTENT,
			encode_command_intent(forged_hunter));
		client.send_frame(forged_hunter_frame);

		auto forged_hunter_result_frame = client.receive_frame();
		forged_hunter_result_frame.has_value() or TESTFAIL;
		session.validate_incoming(*forged_hunter_result_frame);
		forged_hunter_result_frame->message_type == MessageType::COMMAND_RESULT or TESTFAIL;
		auto forged_hunter_result = decode_command_result(
			forged_hunter_result_frame->payload);
		TESTEQUALS(forged_hunter_result.command_id, forged_hunter.command_id);
		forged_hunter_result.status == CommandStatus::REJECTED or TESTFAIL;
		TESTEQUALS(forged_hunter_result.world_revision, uint64_t{1});
		TESTEQUALS(forged_hunter_result.reason, "entity is not owned by this session");
		TESTEQUALS(replication.get_revision(), uint64_t{1});
	};
	send_forged_hunter_command(3, CommandKind::ATTACK_MOVE);
	send_forged_hunter_command(4, CommandKind::MOVE);

	CommandIntent build{};
	build.command_id = 5;
	build.kind = CommandKind::BUILD_TOWER;
	build.entity_ids = {initial.entity_id};
	build.target = {
		target.ne,
		target.se + coord::phys_t::from_int(4),
		target.up};
	client.send_frame(session.make_frame(
		MessageType::COMMAND_INTENT,
		encode_command_intent(build)));

	auto build_result_frame = client.receive_frame();
	build_result_frame.has_value() or TESTFAIL;
	session.validate_incoming(*build_result_frame);
	build_result_frame->message_type == MessageType::COMMAND_RESULT or TESTFAIL;
	auto build_result = decode_command_result(build_result_frame->payload);
	build_result.status == CommandStatus::ACCEPTED or TESTFAIL;
	TESTEQUALS(build_result.world_revision, uint64_t{2});

	auto planned_frame = client.receive_frame();
	planned_frame.has_value() or TESTFAIL;
	session.validate_incoming(*planned_frame);
	auto planned_patch = decode_world_delta(planned_frame->payload);
	replication.accept_delta(planned_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto planned = replication.get_world_state().value();
	TESTEQUALS(planned.constructions.size(), size_t{1});
	planned.constructions.front().phase == ConstructionPhase::PLANNED
		or TESTFAIL;
	not planned.reservations.empty() or TESTFAIL;
	TESTEQUALS(
		find_entity(planned, planned.constructions.front().site_entity_id).definition,
		"fvd.data.entities.Foundation");
	const auto planned_sites = make_construction_presentation(planned);
	TESTEQUALS(planned_sites.sites.size(), size_t{1});
	TESTEQUALS(planned_sites.sites.front().owner_id, gamestate::player_id_t{2});
	TESTEQUALS(planned_sites.sites.front().phase == SitePresentationPhase::PLANNED, true);
	TESTEQUALS(planned_sites.reservations.empty(), false);

	auto constructing_frame = client.receive_frame();
	constructing_frame.has_value() or TESTFAIL;
	session.validate_incoming(*constructing_frame);
	auto constructing_patch = decode_world_delta(constructing_frame->payload);
	replication.accept_delta(constructing_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto constructing = replication.get_world_state().value();
	TESTEQUALS(constructing.constructions.size(), size_t{1});
	constructing.constructions.front().phase
		== ConstructionPhase::UNDER_CONSTRUCTION
		or TESTFAIL;
	TESTEQUALS(
		find_entity(constructing,
		            constructing.constructions.front().site_entity_id).definition,
		"fvd.data.entities.MedievalTowerLevel1");
	const auto &guest_build = constructing.constructions.front();
	TESTEQUALS(guest_build.phase_start_time, planned.constructions.front().phase_end_time);
	TESTEQUALS(guest_build.progress_percent(guest_build.phase_start_time), 0);
	TESTEQUALS(guest_build.progress_percent(guest_build.phase_end_time), 100);
	const auto building_sites = make_construction_presentation(constructing);
	TESTEQUALS(building_sites.sites.size(), size_t{1});
	TESTEQUALS(building_sites.sites.front().phase == SitePresentationPhase::BUILDING, true);
	TESTEQUALS(building_sites.sites.front().owner_id, gamestate::player_id_t{2});

	auto completed_frame = client.receive_frame();
	completed_frame.has_value() or TESTFAIL;
	session.validate_incoming(*completed_frame);
	auto completed_patch = decode_world_delta(completed_frame->payload);
	replication.accept_delta(completed_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto completed = replication.get_world_state().value();
	completed.constructions.empty() or TESTFAIL;
	completed.reservations.empty() or TESTFAIL;
	find_defined_entity(completed, "fvd.data.entities.MedievalTowerLevel1");
	const auto finished_sites = make_construction_presentation(completed);
	TESTEQUALS(finished_sites.sites.size(), size_t{1});
	TESTEQUALS(finished_sites.sites.front().phase == SitePresentationPhase::COMPLETE, true);
	TESTEQUALS(finished_sites.reservations.empty(), true);

	auto spawned_frame = client.receive_frame();
	spawned_frame.has_value() or TESTFAIL;
	session.validate_incoming(*spawned_frame);
	auto spawned_patch = decode_world_delta(spawned_frame->payload);
	replication.accept_delta(spawned_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto spawned = replication.get_world_state().value();
	TESTEQUALS(spawned_patch.changed.base_revision, uint64_t{4});
	TESTEQUALS(spawned.world_revision, uint64_t{5});
	TESTEQUALS(spawned.local_kill_count, uint64_t{0});

	auto fired_frame = client.receive_frame();
	fired_frame.has_value() or TESTFAIL;
	session.validate_incoming(*fired_frame);
	auto fired_patch = decode_world_delta(fired_frame->payload);
	replication.accept_delta(fired_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto fired = replication.get_world_state().value();
	TESTEQUALS(fired_patch.changed.base_revision, uint64_t{5});
	TESTEQUALS(fired.world_revision, uint64_t{6});
	TESTEQUALS(fired.arrows.size(), size_t{1});
	const auto victim_id = fired.arrows.front().target_id;
	TESTEQUALS(fired.arrows.front().owner_id, gamestate::player_id_t{2});
	const auto &unhurt = find_entity(spawned, victim_id);
	unhurt.health.has_value() or TESTFAIL;
	unhurt.maximum_health.has_value() or TESTFAIL;
	TESTEQUALS(*unhurt.health, *unhurt.maximum_health);

	auto impacted_frame = client.receive_frame();
	impacted_frame.has_value() or TESTFAIL;
	session.validate_incoming(*impacted_frame);
	auto impacted_patch = decode_world_delta(impacted_frame->payload);
	TESTEQUALS(impacted_patch.changed.arrows.empty(), true);
	replication.accept_delta(impacted_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto impacted = replication.get_world_state().value();
	TESTEQUALS(impacted_patch.changed.base_revision, uint64_t{6});
	TESTEQUALS(impacted.world_revision, uint64_t{7});
	TESTEQUALS(impacted.arrows.size(), size_t{1});
	TESTEQUALS(arrow_visible_at(impacted.arrows.front(),
	                           impacted.simulation_time),
	           false);
	const auto &wounded = find_entity(impacted, victim_id);
	wounded.health.has_value() or TESTFAIL;
	wounded.maximum_health.has_value() or TESTFAIL;
	TESTEQUALS(*wounded.health < *unhurt.health, true);
	TESTEQUALS(*wounded.maximum_health, *unhurt.maximum_health);

	auto killed_frame = client.receive_frame();
	killed_frame.has_value() or TESTFAIL;
	session.validate_incoming(*killed_frame);
	auto killed_patch = decode_world_delta(killed_frame->payload);
	replication.accept_delta(killed_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto killed = replication.get_world_state().value();
	TESTEQUALS(replication.get_local_kills().value(), uint64_t{1});
	const auto &local_tower = find_defined_entity(killed, "fvd.data.entities.MedievalTowerLevel1");
	TESTEQUALS(local_tower.tower_kills.value(), uint64_t{1});
	TESTEQUALS(replication.get_tower_kills(local_tower.entity_id).value(),
	           uint64_t{1});
	TESTEQUALS(killed_patch.changed.base_revision, uint64_t{7});
	TESTEQUALS(killed.world_revision, uint64_t{8});
	TESTEQUALS(std::none_of(
		killed.entities.begin(), killed.entities.end(),
		[victim_id](const auto &entity) { return entity.entity_id == victim_id; }),
	           true);
	TESTEQUALS(killed.local_kill_count, uint64_t{1});
}

void run_hunter_guest(uint16_t port) {
	auto client = TcpConnection::connect("127.0.0.1", port);
	auto session = client_handshake(
		client,
		{PROTOCOL_MINIMUM_COMPATIBLE_MINOR,
		 "Hunter",
		 "fvd-test",
		 test_asset_digest()});
	TESTEQUALS(session.get_identity().player_id, gamestate::player_id_t{1});
	auto snapshot_frame = client.receive_frame();
	snapshot_frame.has_value() or TESTFAIL;
	session.validate_incoming(*snapshot_frame);
	snapshot_frame->message_type == MessageType::WORLD_SNAPSHOT or TESTFAIL;
	auto snapshot = decode_world_state(snapshot_frame->payload);
	ReplicationTracker replication{session.get_identity().player_id};
	replication.accept_snapshot(snapshot);
	TESTEQUALS(replication.get_local_kills().value(), uint64_t{0});
	TESTEQUALS(snapshot.world_revision, uint64_t{9});
	TESTEQUALS(snapshot.local_player_id, gamestate::player_id_t{1});
	TESTEQUALS(snapshot.local_kill_count, uint64_t{0});
	TESTEQUALS(snapshot.entities.size() >= size_t{8}, true);
	TESTEQUALS(find_player(snapshot, 2).connected, false);
	TESTEQUALS(find_defined_entity(
		snapshot, "fvd.data.entities.MedievalTowerLevel1").owner_id,
	           gamestate::player_id_t{2});
	auto horse = find_owned_entity(snapshot, 1);
	auto disconnected_frame = client.receive_frame();
	disconnected_frame.has_value() or TESTFAIL;
	session.validate_incoming(*disconnected_frame);
	disconnected_frame->message_type
		== MessageType::PARTICIPANT_DISCONNECTED
		or TESTFAIL;
	auto disconnected = decode_participant_disconnected(
		disconnected_frame->payload);
	TESTEQUALS(disconnected.player_id, gamestate::player_id_t{2});
	disconnected.reason == DisconnectReason::CONNECTION_LOST or TESTFAIL;

	CommandIntent attack{};
	attack.command_id = 1;
	attack.kind = CommandKind::ATTACK_MOVE;
	attack.entity_ids = {horse.entity_id};
	attack.target = {
		horse.position.ne,
		horse.position.se - coord::phys_t::from_int(3),
		horse.position.up};
	auto attack_frame = session.make_frame(
		MessageType::COMMAND_INTENT,
		encode_command_intent(attack));
	client.send_frame(attack_frame);

	auto attack_result_frame = client.receive_frame();
	attack_result_frame.has_value() or TESTFAIL;
	session.validate_incoming(*attack_result_frame);
	attack_result_frame->message_type == MessageType::COMMAND_RESULT or TESTFAIL;
	auto attack_result = decode_command_result(attack_result_frame->payload);
	TESTEQUALS(attack_result.command_id, attack.command_id);
	attack_result.status == CommandStatus::ACCEPTED or TESTFAIL;
	TESTEQUALS(attack_result.world_revision, uint64_t{10});

	auto delta_frame = client.receive_frame();
	delta_frame.has_value() or TESTFAIL;
	session.validate_incoming(*delta_frame);
	delta_frame->message_type == MessageType::WORLD_DELTA or TESTFAIL;
	auto delta_patch = decode_world_delta(delta_frame->payload);
	replication.accept_delta(delta_patch) == DeltaDisposition::APPLY or TESTFAIL;
	auto delta = replication.get_world_state().value();
	TESTEQUALS(delta_patch.changed.base_revision, uint64_t{9});
	TESTEQUALS(delta.world_revision, uint64_t{10});
	TESTEQUALS(delta.entities.size() >= size_t{8}, true);
	TESTEQUALS(find_entity(delta, horse.entity_id).position != horse.position, true);
	auto replicated_order = std::find_if(
		delta.attack_orders.begin(), delta.attack_orders.end(),
		[&](const auto &order) { return order.horse_id == horse.entity_id; });
	replicated_order != delta.attack_orders.end() or TESTFAIL;
	TESTEQUALS(replicated_order->destination, attack.target);

	CommandIntent build{};
	build.command_id = 2;
	build.kind = CommandKind::BUILD_TOWER;
	build.entity_ids = {horse.entity_id};
	build.target = {10, 10, 0};
	auto build_frame = session.make_frame(
		MessageType::COMMAND_INTENT,
		encode_command_intent(build));
	client.send_frame(build_frame);

	auto build_result_frame = client.receive_frame();
	build_result_frame.has_value() or TESTFAIL;
	session.validate_incoming(*build_result_frame);
	build_result_frame->message_type == MessageType::COMMAND_RESULT or TESTFAIL;
	auto build_result = decode_command_result(build_result_frame->payload);
	TESTEQUALS(build_result.command_id, build.command_id);
	build_result.status == CommandStatus::REJECTED or TESTFAIL;
	TESTEQUALS(build_result.world_revision, uint64_t{10});
	TESTEQUALS(build_result.reason, "Hunter cannot construct towers");
	TESTEQUALS(replication.get_revision(), uint64_t{10});
}

std::string required_environment(const char *name) {
	auto value = std::getenv(name);
	if (value == nullptr or value[0] == '\0') {
		throw Error{MSG(err) << "Two-process network test requires " << name};
	}
	return value;
}

uint16_t process_test_port() {
	auto value = required_environment("OPENAGE_FVD_NETWORK_TEST_PORT");
	unsigned int port{};
	auto parsed = std::from_chars(value.data(), value.data() + value.size(), port);
	if (parsed.ec != std::errc{}
	    or parsed.ptr != value.data() + value.size()
	    or port == 0
	    or port > (std::numeric_limits<uint16_t>::max)()) {
		throw Error{MSG(err) << "Invalid two-process network test port " << value};
	}
	return static_cast<uint16_t>(port);
}

void announce_process_test_port(uint16_t port) {
	auto path = required_environment("OPENAGE_FVD_NETWORK_TEST_READY_FILE");
	std::ofstream ready{path, std::ios::trunc};
	if (not ready) {
		throw Error{MSG(err) << "Could not open two-process network test ready file " << path};
	}
	ready << port << '\n';
	ready.close();
	if (not ready) {
		throw Error{MSG(err) << "Could not write two-process network test ready file " << path};
	}
}

} // namespace

void fvd_loopback() {
	auto listener = TcpListener::listen("127.0.0.1", 0);
	std::exception_ptr server_error{};
	std::jthread server{[&]() {
		try {
			run_host(listener, true);
		}
		catch (...) {
			server_error = std::current_exception();
			listener.close();
		}
	}};

	try {
		run_guest(listener.get_port());
		run_hunter_guest(listener.get_port());
	}
	catch (...) {
		auto client_error = std::current_exception();
		server.join();
		if (server_error) {
			std::rethrow_exception(server_error);
		}
		std::rethrow_exception(client_error);
	}
	server.join();
	if (server_error) {
		std::rethrow_exception(server_error);
	}
}

void fvd_process_worker() {
	auto role = required_environment("OPENAGE_FVD_NETWORK_TEST_ROLE");
	if (role == "host") {
		auto listener = TcpListener::listen("127.0.0.1", 0);
		announce_process_test_port(listener.get_port());
		run_host(listener, false);
		return;
	}
	if (role == "guest") {
		run_guest(process_test_port());
		return;
	}
	throw Error{MSG(err) << "Unknown two-process network test role " << role};
}

} // namespace openage::network::tests
