// Copyright 2026 the openage authors. See copying.md for legal info.

#include "match_disconnect_coordinator.h"

#include <memory>
#include <optional>
#include <vector>
#include <variant>

#include "gamestate/fvd.h"
#include "gamestate/fvd_test_support.h"

#include "assets/mod_manager.h"
#include "config.h"
#include "event/event_loop.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/process_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/wait.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/terrain_factory.h"
#include "network/command_bridge.h"
#include "network/command_mailbox.h"
#include "network/replication.h"
#include "network/session.h"
#include "testing/testing.h"
#include "util/fslike/directory.h"
#include "util/path.h"

namespace openage::network::tests {

void match_disconnect_coordinator() {
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

	std::optional<gamestate::entity_id_t> builder_two{};
	std::optional<gamestate::entity_id_t> builder_three{};
	for (const auto &[id, entity] : state->get_game_entities()) {
		auto ownership = std::dynamic_pointer_cast<gamestate::component::Ownership>(
			entity->get_component(gamestate::component::component_t::OWNERSHIP));
		auto player = ownership->get_owners().get(time::TIME_ZERO);
		if (player == 2) {
			builder_two = id;
		}
		else if (player == 3) {
			builder_three = id;
		}
	}
	builder_two.has_value() or TESTFAIL;
	builder_three.has_value() or TESTFAIL;

	MatchConnectionState match{state->get_fvd()->get_player_count()};
	MatchDisconnectCoordinator coordinator{match};
	const SessionIdentity two{42, 2};
	const SessionIdentity three{43, 3};
	OutboundMailbox outbound_two{4};
	OutboundMailbox outbound_three{4};
	coordinator.register_remote(two, outbound_two);
	coordinator.register_remote(three, outbound_three);
	TESTTHROWS(coordinator.register_remote(two, outbound_two));
	TESTTHROWS(coordinator.register_remote({44, 2}, outbound_two));

	ReplicationTracker guest_three{three.player_id};
	auto initial = capture_fvd_state(state, match, time::TIME_ZERO, 0, 4, 3);
	guest_three.accept_snapshot(initial);
	coordinator.record_snapshot(initial);
	TESTEQUALS(initial.players.at(1).connected, true);
	auto before = initial.entities;

	// A command may already be queued in a connection thread when the
	// authoritative match learns that the socket has gone away.
	CommandMailbox incoming{4};
	CommandIntent stale{};
	stale.command_id = 11;
	stale.kind = CommandKind::MOVE;
	stale.entity_ids = {*builder_two};
	stale.target = {75, 75, 0};
	incoming.try_push({two, stale}) or TESTFAIL;

	ConnectionEventMailbox closures{4};
	closures.try_push({two, DisconnectReason::CONNECTION_LOST}) or TESTFAIL;
	// Repeated socket close errors are benign; they must never advance the
	// revision twice or produce duplicate participant notifications.
	closures.try_push({two, DisconnectReason::CONNECTION_LOST}) or TESTFAIL;
	uint64_t revision = 4;
	TESTEQUALS(coordinator.process_closures(
		closures, state, time::TIME_ZERO, revision, 4), size_t{1});
	TESTEQUALS(revision, uint64_t{5});
	TESTEQUALS(closures.size(), size_t{0});
	TESTEQUALS(match.is_connected(2), false);
	TESTEQUALS(match.is_connected(3), true);
	TESTTHROWS(match.bind_remote({999, 2}));
	TESTEQUALS(outbound_two.size(), size_t{0});

	auto messages = outbound_three.drain(4);
	TESTEQUALS(messages.size(), size_t{2});
	TESTEQUALS(std::holds_alternative<ParticipantDisconnected>(messages.at(0)), true);
	TESTEQUALS(std::holds_alternative<WorldDeltaMessage>(messages.at(1)), true);
	ConnectionSession session_three{three};
	auto notice_frame = make_outbound_frame(session_three, messages.at(0));
	auto delta_frame = make_outbound_frame(session_three, messages.at(1));
	TESTEQUALS(notice_frame.sequence, uint64_t{1});
	TESTEQUALS(delta_frame.sequence, uint64_t{2});
	auto notice = decode_participant_disconnected(notice_frame.payload);
	TESTEQUALS(notice.player_id, gamestate::player_id_t{2});
	notice.reason == DisconnectReason::CONNECTION_LOST or TESTFAIL;
	auto delta = decode_world_delta(delta_frame.payload);
	TESTEQUALS(delta.changed.base_revision, uint64_t{4});
	TESTEQUALS(delta.changed.world_revision, uint64_t{5});
	TESTEQUALS(delta.changed.local_player_id, gamestate::player_id_t{3});
	TESTEQUALS(delta.changed.players.size(), size_t{1});
	TESTEQUALS(delta.changed.entities.empty(), true);
	TESTEQUALS(delta.removed_entities.empty(), true);
	guest_three.accept_delta(delta) == DeltaDisposition::APPLY or TESTFAIL;
	auto rebuilt = guest_three.get_world_state().value();
	TESTEQUALS(rebuilt.players.at(1).connected, false);
	TESTEQUALS(rebuilt.players.at(2).connected, true);
	TESTEQUALS(rebuilt.entities.size(), before.size());
	for (size_t i = 0; i < before.size(); ++i) {
		TESTEQUALS(rebuilt.entities.at(i).entity_id, before.at(i).entity_id);
		TESTEQUALS(rebuilt.entities.at(i).owner_id, before.at(i).owner_id);
		TESTEQUALS(rebuilt.entities.at(i).position, before.at(i).position);
	}
	// Recovery snapshots carry the same permanent disconnection status.
	auto recovery = capture_fvd_state(state, match, time::TIME_ZERO, 0, revision, 3);
	TESTEQUALS(recovery.players.at(1).connected, false);

	auto pending = incoming.drain(4);
	TESTEQUALS(pending.size(), size_t{1});
	TESTEQUALS(coordinator.accepts_command(pending.at(0)), false);
	bool stale_accepted = true;
	submit_authenticated_command(
		pending.at(0), match, loop, commander, state, time::TIME_ZERO,
		[&](bool result) { stale_accepted = result; });
	TESTEQUALS(stale_accepted, false);
	loop->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(state->get_game_entities().size(), before.size());
	TESTEQUALS(coordinator.accepts_command({{43, 2}, stale}), false);

	// The surviving remote can continue submitting valid owned commands,
	// without taking over the disconnected player's existing entities.
	AuthenticatedCommand surviving{};
	surviving.identity = three;
	surviving.intent.command_id = 12;
	surviving.intent.kind = CommandKind::MOVE;
	surviving.intent.entity_ids = {*builder_three};
	surviving.intent.target = {70, 70, 0};
	TESTEQUALS(coordinator.accepts_command(surviving), true);
	std::optional<bool> accepted;
	submit_authenticated_command(
		surviving, match, loop, commander, state, time::TIME_ZERO,
		[&](bool result) { accepted = result; });
	loop->reach_time(time::TIME_ZERO, state);
	TESTEQUALS(accepted.has_value(), true);
	TESTEQUALS(*accepted, true);

	// Unknown sessions are not silently mistaken for a second close.
	closures.try_push({{999, 2}, DisconnectReason::CONNECTION_LOST}) or TESTFAIL;
	TESTTHROWS(coordinator.process_closures(
		closures, state, time::TIME_ZERO, revision, 1));
	TESTEQUALS(revision, uint64_t{5});

	// An observer unable to receive BOTH messages forces a fatal host
	// error *before* participant state or revision is modified.
	MatchConnectionState small_match{3};
	MatchDisconnectCoordinator small{small_match};
	OutboundMailbox limited{1};
	OutboundMailbox departing{2};
	small.register_remote(two, departing);
	small.register_remote(three, limited);
	ConnectionEventMailbox out_of_space{1};
	out_of_space.try_push({two, DisconnectReason::CONNECTION_LOST}) or TESTFAIL;
	uint64_t small_revision = 8;
	TESTTHROWS(small.process_closures(
		out_of_space, state, time::TIME_ZERO, small_revision, 1));
	TESTEQUALS(small_revision, uint64_t{8});
	TESTEQUALS(small_match.is_connected(2), true);

	// A remote Hunter departure is terminal even when Builder sessions are
	// connected. Surviving Builders receive one final ordered notification
	// and world delta, but may no longer submit gameplay commands.
	MatchConnectionState hunter_match{state->get_fvd()->get_player_count()};
	MatchDisconnectCoordinator hunter_coordinator{hunter_match};
	const SessionIdentity hunter_identity{401, 1};
	const SessionIdentity builder_identity{403, 3};
	OutboundMailbox hunter_outbound{4};
	OutboundMailbox builder_outbound{4};
	hunter_coordinator.register_remote(hunter_identity, hunter_outbound);
	hunter_coordinator.register_remote(builder_identity, builder_outbound);
	auto hunter_initial = capture_fvd_state(
		state, hunter_match, time::TIME_ZERO, 0, 12, builder_identity.player_id);
	hunter_coordinator.record_snapshot(hunter_initial);
	TESTEQUALS(hunter_match.is_match_active(), true);
	ConnectionEventMailbox hunter_closures{2};
	hunter_closures.try_push({hunter_identity, DisconnectReason::CONNECTION_LOST}) or TESTFAIL;
	uint64_t hunter_revision = 12;
	TESTEQUALS(hunter_coordinator.process_closures(
		hunter_closures, state, time::TIME_ZERO, hunter_revision, 2), size_t{1});
	TESTEQUALS(hunter_revision, uint64_t{13});
	TESTEQUALS(hunter_match.is_match_active(), false);
	TESTEQUALS(hunter_match.is_connected(3), true);
	TESTEQUALS(hunter_match.accepts_commands(builder_identity), false);
	AuthenticatedCommand post_hunter_loss{};
	post_hunter_loss.identity = builder_identity;
	post_hunter_loss.intent.command_id = 99;
	post_hunter_loss.intent.kind = CommandKind::MOVE;
	post_hunter_loss.intent.entity_ids = {*builder_three};
	post_hunter_loss.intent.target = {60, 60, 0};
	TESTEQUALS(hunter_coordinator.accepts_command(post_hunter_loss), false);
	auto goodbye = builder_outbound.drain(4);
	TESTEQUALS(goodbye.size(), size_t{2});
	TESTEQUALS(std::holds_alternative<ParticipantDisconnected>(goodbye.at(0)), true);
	TESTEQUALS(std::get<ParticipantDisconnected>(goodbye.at(0)).player_id,
	           gamestate::player_id_t{1});
	TESTEQUALS(std::holds_alternative<WorldDeltaMessage>(goodbye.at(1)), true);
	auto final_world = apply_world_delta(
		hunter_initial, std::get<WorldDeltaMessage>(goodbye.at(1)).state);
	TESTEQUALS(final_world.players.at(0).connected, false);
	TESTEQUALS(final_world.players.at(2).connected, true);

	// Full eight-person fan-out: after the remote Hunter leaves, all seven
	// surviving Builder sockets get a final ordered notice and delta.
	MatchConnectionState full_match{state->get_fvd()->get_player_count()};
	MatchDisconnectCoordinator full_coordinator{full_match};
	const SessionIdentity eight_hunter{9001, 1};
	OutboundMailbox eight_hunter_outbound{4};
	full_coordinator.register_remote(eight_hunter, eight_hunter_outbound);
	std::vector<std::unique_ptr<OutboundMailbox>> recipients{};
	for (gamestate::player_id_t player = 2; player <= 8; ++player) {
		auto outbound = std::make_unique<OutboundMailbox>(4);
		const SessionIdentity builder_identity{9000 + player, player};
		full_coordinator.register_remote(builder_identity, *outbound);
		full_coordinator.record_snapshot(capture_fvd_state(
			state, full_match, time::TIME_ZERO, 0, 20, player));
		recipients.push_back(std::move(outbound));
	}
	ConnectionEventMailbox full_closures{3};
	full_closures.try_push({eight_hunter, DisconnectReason::CONNECTION_LOST}) or TESTFAIL;
	uint64_t full_revision = 20;
	TESTEQUALS(full_coordinator.process_closures(
		full_closures, state, time::TIME_ZERO, full_revision, 3), size_t{1});
	TESTEQUALS(full_revision, uint64_t{21});
	TESTEQUALS(full_match.is_match_active(), false);
	for (size_t i = 0; i < recipients.size(); ++i) {
		auto builder_player = static_cast<gamestate::player_id_t>(i + 2);
		auto messages = recipients[i]->drain(4);
		TESTEQUALS(messages.size(), size_t{2});
		TESTEQUALS(std::holds_alternative<ParticipantDisconnected>(messages.at(0)), true);
		TESTEQUALS(std::get<ParticipantDisconnected>(messages.at(0)).player_id,
		           gamestate::player_id_t{1});
		TESTEQUALS(std::holds_alternative<WorldDeltaMessage>(messages.at(1)), true);
		const auto &delta = std::get<WorldDeltaMessage>(messages.at(1)).state;
		TESTEQUALS(delta.changed.world_revision, uint64_t{21});
		TESTEQUALS(delta.changed.local_player_id, builder_player);
		TESTEQUALS(full_match.is_connected(builder_player), true);
		TESTEQUALS(full_match.accepts_commands({9000 + builder_player, builder_player}), false);
	}
}

} // namespace openage::network::tests
