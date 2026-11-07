// Copyright 2026 the openage authors. See copying.md for legal info.

// Multi-session TCP integration harness for the authoritative FVD components.
// The public-room relay UI is still two-player; this test deliberately uses
// independent direct TCP sessions to verify the 3-8 participant core.
#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "assets/mod_manager.h"
#include "config.h"
#include "event/event_loop.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/process_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/wait.h"
#include "gamestate/fvd.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/terrain_factory.h"
#include "network/command_bridge.h"
#include "network/connection_mailbox.h"
#include "network/handshake.h"
#include "network/lobby.h"
#include "network/match_connection_state.h"
#include "network/match_disconnect_coordinator.h"
#include "network/replication.h"
#include "network/transport.h"
#include "testing/testing.h"
#include "util/fslike/directory.h"
#include "util/path.h"

namespace openage::network::tests {

namespace {

AssetDigest multiparty_digest() {
	AssetDigest digest{};
	for (size_t i = 0; i < digest.size(); ++i) {
		digest[i] = static_cast<uint8_t>(i + 83);
	}
	return digest;
}

struct RemotePeer {
	std::unique_ptr<TcpConnection> socket{};
	std::unique_ptr<ConnectionSession> session{};
	std::unique_ptr<OutboundMailbox> outbound{};
};

void run_multi_host(TcpListener &listener, size_t players) {
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
	auto commander = std::make_shared<gamestate::event::Commander>(loop);

	// Exercise the actual 3-8-person roster and Hunter/Builder slot rules.
	HostLobby lobby{1, "Hunter Host"};
	for (size_t id = 2; id <= players; ++id) {
		lobby.join(id, "Builder " + std::to_string(id));
	}
	auto assignments = lobby.start([](uint64_t) { return uint64_t{0}; });
	TESTEQUALS(assignments.assignments.size(), players);
	TESTEQUALS(player_for_participant(assignments, 1), gamestate::player_id_t{1});

	MatchConnectionState connected{state->get_fvd()->get_player_count()};
	connected.restrict_to_participants(players);
	MatchDisconnectCoordinator coordinator{connected};
	std::vector<RemotePeer> peers{};
	peers.reserve(players - 1);
	for (size_t index = 0; index < players - 1; ++index) {
		RemotePeer peer{};
		peer.socket = std::make_unique<TcpConnection>(listener.accept());
		// The test makes the host Hunter and the remote guests Builders,
		// exercising independent session sequence numbers on every socket.
		const auto player = static_cast<gamestate::player_id_t>(index + 2);
		auto accepted = host_handshake(*peer.socket, player, multiparty_digest());
		accepted.has_value() or TESTFAIL;
		peer.session = std::make_unique<ConnectionSession>(*accepted);
		peer.outbound = std::make_unique<OutboundMailbox>(4);
		coordinator.register_remote(peer.session->get_identity(), *peer.outbound);
		peers.push_back(std::move(peer));
	}

	for (auto &peer : peers) {
		auto recipient = peer.session->get_identity().player_id;
		auto snapshot = capture_fvd_state(
			state, connected, time::TIME_ZERO, 0, 1, recipient);
		coordinator.record_snapshot(snapshot);
		peer.socket->send_frame(peer.session->make_frame(
			MessageType::WORLD_SNAPSHOT, encode_world_state(snapshot)));
	}

	CommandMailbox incoming{16};
	for (auto &peer : peers) {
		auto frame = peer.socket->receive_frame();
		frame.has_value() or TESTFAIL;
		peer.session->validate_incoming(*frame);
		frame->message_type == MessageType::COMMAND_INTENT or TESTFAIL;
		incoming.try_push({
			peer.session->get_identity(), decode_command_intent(frame->payload)})
			or TESTFAIL;
	}
	auto simultaneous = incoming.drain(players);
	TESTEQUALS(simultaneous.size(), players - 1);
	std::vector<std::optional<bool>> decisions(players - 1);
	for (size_t i = 0; i < simultaneous.size(); ++i) {
		TESTEQUALS(simultaneous[i].identity.player_id,
		           static_cast<gamestate::player_id_t>(i + 2));
		submit_authenticated_command(
			simultaneous[i], connected, loop, commander, state, time::TIME_ZERO,
			[&, i](bool ok) { decisions[i] = ok; });
	}
	loop->reach_time(time::TIME_ZERO, state);
	for (size_t i = 0; i < peers.size(); ++i) {
		decisions[i].has_value() or TESTFAIL;
		TESTEQUALS(*decisions[i], true);
		CommandResult reply{};
		reply.command_id = simultaneous[i].intent.command_id;
		reply.status = CommandStatus::ACCEPTED;
		reply.world_revision = 2;
		peers[i].socket->send_frame(peers[i].session->make_frame(
			MessageType::COMMAND_RESULT, encode_command_result(reply)));
		auto recipient = peers[i].session->get_identity().player_id;
		auto delta = coordinator.make_delta(capture_fvd_state(
			state, connected, time::TIME_ZERO, 0, 2, recipient));
		peers[i].socket->send_frame(peers[i].session->make_frame(
			MessageType::WORLD_DELTA, encode_world_delta(delta)));
	}

	// The highest-numbered Builder leaves. Others must receive a proper
	// authoritative notification and contiguous revision, then keep playing.
	auto &departing = peers.back();
	auto goodbye = departing.socket->receive_frame();
	TESTEQUALS(goodbye.has_value(), false);
	ConnectionEventMailbox closures{4};
	closures.try_push({
		departing.session->get_identity(), DisconnectReason::CONNECTION_LOST})
		or TESTFAIL;
	uint64_t revision = 2;
	TESTEQUALS(coordinator.process_closures(
		closures, state, time::TIME_ZERO, revision, 4), size_t{1});
	TESTEQUALS(revision, uint64_t{3});
	TESTEQUALS(connected.is_match_active(), true);
	TESTEQUALS(connected.is_connected(static_cast<gamestate::player_id_t>(players)), false);
	for (size_t i = 0; i + 1 < peers.size(); ++i) {
		auto messages = peers[i].outbound->drain(4);
		TESTEQUALS(messages.size(), size_t{2});
		for (const auto &message : messages) {
			peers[i].socket->send_frame(make_outbound_frame(*peers[i].session, message));
		}
	}
	AuthenticatedCommand post_disconnect{};
	auto &survivor = peers[peers.size() - 2];
	auto command_frame = survivor.socket->receive_frame();
	command_frame.has_value() or TESTFAIL;
	survivor.session->validate_incoming(*command_frame);
	command_frame->message_type == MessageType::COMMAND_INTENT or TESTFAIL;
	post_disconnect = {
		survivor.session->get_identity(),
		decode_command_intent(command_frame->payload)};
	TESTEQUALS(coordinator.accepts_command(post_disconnect), true);
	std::optional<bool> post_result{};
	submit_authenticated_command(
		post_disconnect, connected, loop, commander, state, time::TIME_ZERO,
		[&](bool ok) { post_result = ok; });
	loop->reach_time(time::TIME_ZERO, state);
	post_result.has_value() or TESTFAIL;
	TESTEQUALS(*post_result, true);
	CommandResult reply{};
	reply.command_id = post_disconnect.intent.command_id;
	reply.status = CommandStatus::ACCEPTED;
	reply.world_revision = revision;
	survivor.socket->send_frame(survivor.session->make_frame(
		MessageType::COMMAND_RESULT, encode_command_result(reply)));
}

void run_multi_client(uint16_t port, size_t players) {
	auto connection = TcpConnection::connect("127.0.0.1", port);
	auto session = client_handshake(connection, {
		PROTOCOL_MINIMUM_COMPATIBLE_MINOR,
		"Builder Guest",
		"fvd-multi-test",
		multiparty_digest()});
	auto player = session.get_identity().player_id;
	TESTEQUALS(player >= 2 and player <= players, true);
	auto first = connection.receive_frame();
	first.has_value() or TESTFAIL;
	session.validate_incoming(*first);
	first->message_type == MessageType::WORLD_SNAPSHOT or TESTFAIL;
	ReplicationTracker tracker{player};
	tracker.accept_snapshot(decode_world_state(first->payload));
	auto world = tracker.get_world_state().value();
	TESTEQUALS(world.players.size(), size_t{8});
	for (size_t slot = 1; slot <= world.players.size(); ++slot) {
		TESTEQUALS(world.players.at(slot - 1).connected, slot <= players);
	}
	auto builder = std::find_if(world.entities.begin(), world.entities.end(),
		[player](const auto &entity) {
			return entity.owner_id == player
			       and entity.definition == "fvd.data.entities.Builder";
		});
	builder != world.entities.end() or TESTFAIL;
	CommandIntent move{};
	move.command_id = 1;
	move.kind = CommandKind::MOVE;
	move.entity_ids = {builder->entity_id};
	move.target = {static_cast<int>(48 + player),
	               static_cast<int>(48 + player), 0};
	connection.send_frame(session.make_frame(
		MessageType::COMMAND_INTENT, encode_command_intent(move)));

	auto result_frame = connection.receive_frame();
	result_frame.has_value() or TESTFAIL;
	session.validate_incoming(*result_frame);
	result_frame->message_type == MessageType::COMMAND_RESULT or TESTFAIL;
	auto result = decode_command_result(result_frame->payload);
	TESTEQUALS(result.command_id, uint64_t{1});
	TESTEQUALS(result.status == CommandStatus::ACCEPTED, true);
	auto delta_frame = connection.receive_frame();
	delta_frame.has_value() or TESTFAIL;
	session.validate_incoming(*delta_frame);
	delta_frame->message_type == MessageType::WORLD_DELTA or TESTFAIL;
	TESTEQUALS(tracker.accept_delta(decode_world_delta(delta_frame->payload))
	           == DeltaDisposition::APPLY, true);
	TESTEQUALS(tracker.get_revision(), uint64_t{2});

	if (player == players) {
		connection.close();
		return;
	}
	auto notice = connection.receive_frame();
	notice.has_value() or TESTFAIL;
	session.validate_incoming(*notice);
	notice->message_type == MessageType::PARTICIPANT_DISCONNECTED or TESTFAIL;
	TESTEQUALS(decode_participant_disconnected(notice->payload).player_id,
	           static_cast<gamestate::player_id_t>(players));
	auto final_delta = connection.receive_frame();
	final_delta.has_value() or TESTFAIL;
	session.validate_incoming(*final_delta);
	final_delta->message_type == MessageType::WORLD_DELTA or TESTFAIL;
	TESTEQUALS(tracker.accept_delta(decode_world_delta(final_delta->payload))
	           == DeltaDisposition::APPLY, true);
	auto remaining = tracker.get_world_state().value();
	TESTEQUALS(tracker.get_revision(), uint64_t{3});
	TESTEQUALS(remaining.players.at(players - 1).connected, false);
	TESTEQUALS(remaining.players.at(player - 1).connected, true);
	TESTEQUALS(remaining.entities.size(), world.entities.size());

	// One surviving Builder continues giving commands after another leaves.
	if (player == players - 1) {
		move.command_id = 2;
		move.target = {63, 63, 0};
		connection.send_frame(session.make_frame(
			MessageType::COMMAND_INTENT, encode_command_intent(move)));
		auto confirmation = connection.receive_frame();
		confirmation.has_value() or TESTFAIL;
		session.validate_incoming(*confirmation);
		confirmation->message_type == MessageType::COMMAND_RESULT or TESTFAIL;
		TESTEQUALS(decode_command_result(confirmation->payload).status
		           == CommandStatus::ACCEPTED, true);
	}
}

void multi_session_case(size_t players) {
	auto listener = TcpListener::listen("127.0.0.1", 0);
	std::exception_ptr server_error{};
	std::jthread server{[&]() {
		try {
			run_multi_host(listener, players);
		}
		catch (...) {
			server_error = std::current_exception();
		}
	}};
	std::vector<std::exception_ptr> client_errors(players - 1);
	std::vector<std::jthread> clients{};
	clients.reserve(players - 1);
	for (size_t i = 0; i < players - 1; ++i) {
		clients.emplace_back([&, i]() {
			try {
				run_multi_client(listener.get_port(), players);
			}
			catch (...) {
				client_errors[i] = std::current_exception();
			}
		});
	}
	for (auto &client : clients) client.join();
	server.join();
	for (auto &error : client_errors) if (error) std::rethrow_exception(error);
	if (server_error) std::rethrow_exception(server_error);
}

} // namespace

void fvd_multi_session() {
	// Test three, four and the maximum eight independent human sessions.
	multi_session_case(3);
	multi_session_case(4);
	multi_session_case(8);
}

} // namespace openage::network::tests
