// Copyright 2026 the openage authors. See copying.md for legal info.

// Live RelayLobbyClient integration, without the Qt presenter. In contrast
// to the lower-level multi_session TCP test this covers the same workers,
// room admission and role-preserving rehost used by actual game processes.
#include "lobby_relay_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "error/error.h"
#include "network/lobby.h"
#include "network/relay.h"
#include "network/relay_multi_host_lobby.h"
#include "network/relay_service.h"
#include "network/replication.h"
#include "testing/testing.h"

namespace openage::network::tests {

namespace {

using clock_t = std::chrono::steady_clock;

template<typename Predicate>
void await_condition(const std::string &where, Predicate predicate,
                     std::chrono::seconds timeout = std::chrono::seconds{30}) {
	const auto deadline = clock_t::now() + timeout;
	while (clock_t::now() < deadline) {
		if (predicate()) return;
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	throw Error{MSG(err) << "FVD capacity regression timed out: " << where};
}

void await_stage(RelayLobbyClient &client, const std::string &where,
                 bool require_playing = false) {
	await_condition(where, [&]() {
		auto status = client.status();
		if (status.phase == "error") {
			throw Error{MSG(err) << "FVD capacity regression " << where
			                     << ": " << status.message};
		}
		if (require_playing) return status.phase == "playing";
		return status.phase == "assigned" or status.phase == "bootstrap"
		       or status.phase == "streaming" or status.phase == "playing";
	}, std::chrono::seconds{40});
}

WorldState private_world(size_t count, gamestate::player_id_t recipient,
                         uint64_t revision, uint64_t kills) {
	WorldState result{};
	result.map_size = 128;
	result.world_revision = revision;
	result.local_player_id = recipient;
	result.local_kill_count = kills;
	for (size_t i = 1; i <= count; ++i) {
		result.players.push_back({
			static_cast<gamestate::player_id_t>(i),
			i == 1 ? PlayerRole::HUNTER : PlayerRole::BUILDER,
			true});
	}
	return result;
}

void run_capacity_case(size_t count, bool force_builder_creator) {
	// Four and eight actual lobby clients use independent background workers,
	// not the simplified raw TCP handshake harness.
	TESTEQUALS(count == 4 or count == 8, true);
	RelayRoomDirectory directory{4};
	auto listener = TcpListener::listen("127.0.0.1", 0);
	std::exception_ptr relay_error{};
	std::jthread relay{[&]() {
		try {
			serve_relay_directory(listener, directory, 64);
		}
		catch (...) {
			relay_error = std::current_exception();
		}
	}};
	// Close the listener BEFORE jthread joins on an assertion failure.
	// Otherwise the relay's accept() could block forever and hide the
	// useful TESTEQUALS failure (the old "[last/total]" symptom).
	struct CloseListener {
		TcpListener &socket;
		~CloseListener() { socket.close(); }
	} close_listener{listener};
	std::vector<std::unique_ptr<RelayLobbyClient>> clients{};
	clients.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		clients.emplace_back(std::make_unique<RelayLobbyClient>());
	}
	auto &creator = *clients.front();
	std::shared_ptr<RelayMultiHostLobby> prepared{};
	gamestate::player_id_t original_creator_player{1};
	RelayLobbyConfig host{};
	host.address = "127.0.0.1";
	host.port = listener.get_port();
	host.name = force_builder_creator ? "Selected Hunter" : "Lobby Creator";
	host.room_name = force_builder_creator ? "Hunter-owned Capacity Test" : "Public Capacity Test";
	host.player_capacity = count;
	host.hosting = true;
	uint64_t room_id{};
	if (force_builder_creator) {
		// The real lottery always gives the original room creator one
		// ticket, every guest two. Force the guest to draw Hunter so the
		// Builder who CREATED that lobby must rejoin as a remote client.
		HostLobby old_lobby{1, "Original Builder Creator"};
		for (size_t i = 2; i <= count; ++i) {
			old_lobby.join(i, "Original Guest " + std::to_string(i));
		}
		const auto original = old_lobby.start(
			[](uint64_t) { return uint64_t{1}; });
		TESTEQUALS(player_for_participant(original, 2),
		           gamestate::player_id_t{1});
		original_creator_player = player_for_participant(original, 1);
		TESTEQUALS(original_creator_player >= 2, true);
		prepared = std::make_shared<RelayMultiHostLobby>(
			host.address, host.port, RelayToken{}, host.room_name, host.name, count);
		prepared->prepare_hunter_rehost(original);
		host.rehost_start = original;
		host.prepared_rehost_lobby = prepared;
		room_id = prepared->room_id();
		creator.connect(host);
		await_condition("prepared Hunter room listening", [&]() {
			const auto phase = creator.status().phase;
			return phase == "waiting" or phase == "lobby";
		});
		for (size_t i = 1; i < count; ++i) {
			RelayLobbyConfig guest{};
			guest.address = host.address;
			guest.port = host.port;
			guest.hosting = false;
			guest.room_id = room_id;
			guest.name = static_cast<gamestate::player_id_t>(i + 1)
			             == original_creator_player
			             ? "Original Builder Creator"
			             : "Builder " + std::to_string(i + 1);
			guest.rehost_offer = prepared->rehost_offer_for(
				static_cast<gamestate::player_id_t>(i + 1));
			clients[i]->connect(guest);
		}
	}
	else {
		creator.connect(host);
		await_condition("original room listening", [&]() {
			return creator.status().phase == "waiting";
		});
		auto advertised = list_relay_rooms(host.address, host.port, RelayToken{});
		auto found = std::find_if(advertised.begin(), advertised.end(),
			[&host](const auto &room) { return room.room_name == host.room_name; });
		TESTEQUALS(found != advertised.end(), true);
		room_id = found->room_id;
		for (size_t i = 1; i < count; ++i) {
			RelayLobbyConfig guest{};
			guest.address = host.address;
			guest.port = host.port;
			guest.room_id = room_id;
			guest.name = "Builder " + std::to_string(i + 1);
			clients[i]->connect(guest);
			await_condition("guest entered original room", [&]() {
				return clients[i]->status().phase == "lobby";
			});
		}
		await_condition("all guests visible in original roster", [&]() {
			return creator.status().participants.find(
				"Builder " + std::to_string(count)) != std::string::npos;
		});
		creator.request_start();
	}
	for (size_t i = 0; i < count; ++i) {
		await_stage(*clients[i], "StartMatch client " + std::to_string(i));
	}
	RelayLobbyClient *authority{};
	std::map<gamestate::player_id_t, RelayLobbyClient *> builders{};
	std::string common_roles{};
	for (auto &client : clients) {
		auto status = client->status();
		TESTEQUALS(status.match_player_count == count or not client->is_authority(),
		           true);
		TESTEQUALS(status.local_player_id >= 1
		           and status.local_player_id <= count, true);
		if (common_roles.empty()) common_roles = status.roles;
		TESTEQUALS(status.roles, common_roles);
		if (client->is_authority()) {
			TESTEQUALS(authority == nullptr, true);
			authority = client.get();
			TESTEQUALS(status.local_player_id, gamestate::player_id_t{1});
			TESTEQUALS(status.local_role, std::string{"HUNTER"});
		}
		else {
			TESTEQUALS(status.local_role, std::string{"BUILDER"});
			TESTEQUALS(builders.emplace(status.local_player_id, client.get()).second, true);
		}
	}
	TESTEQUALS(authority != nullptr, true);
	TESTEQUALS(builders.size(), count - 1);
	TESTEQUALS(authority->status().multi_host, true);
	TESTEQUALS(authority->status().remote_player_ids.size(), count - 1);
	if (force_builder_creator) {
		TESTEQUALS(creator.is_authority(), true);
		TESTEQUALS(builders.contains(original_creator_player), true);
	}
	// Every Builder sees its own snapshot, score and fixed role.
	for (const auto &[player, builder] : builders) {
		authority->provide_host_snapshot(
			private_world(count, player, 1, 100 + player));
	}
	for (const auto &[player, builder] : builders) {
		await_condition("private world for player " + std::to_string(player), [&]() {
			auto tracker = builder->get_replication();
			return tracker and tracker->has_state();
		});
		auto tracker = builder->get_replication();
		TESTEQUALS(tracker->get_revision(), uint64_t{1});
		TESTEQUALS(tracker->get_local_player_id(), player);
		TESTEQUALS(tracker->get_local_kills().value(), uint64_t{100 + player});
	}
	await_condition("Hunter bootstrap acknowledged by all at revision 1", [&]() {
		auto status = authority->status();
		if (status.phase == "error") {
			throw Error{MSG(err) << "Hunter bootstrap failed: " << status.message};
		}
		return (status.phase == "bootstrap" or status.phase == "streaming"
		        or status.phase == "playing")
		       and status.snapshot_revision == 1;
	});
	for (const auto &[player, builder] : builders) {
		TESTEQUALS(builder->status().snapshot_revision, uint64_t{1});
	}
	// Five-second reveal is global to the assigned match; no Builder can
	// issue commands before the worker has finished the reveal.
	for (auto &client : clients) {
		await_stage(*client, "role reveal", true);
	}
	TESTEQUALS(authority->send_guest_command(CommandIntent{}), false);
	// Simultaneous queued construction at ONE contested tile, followed by
	// individual moves. This validates bounded fan-in, player identities,
	// independent command IDs and recipient-specific accept/reject delivery.
	std::atomic<bool> release{false};
	std::vector<int> enqueued(count - 1, 0);
	std::vector<std::jthread> senders{};
	for (const auto &[player, client] : builders) {
		senders.emplace_back([&, player, client]() {
			while (not release.load()) std::this_thread::yield();
			CommandIntent build{};
			build.kind = CommandKind::BUILD_TOWER;
			build.entity_ids = {static_cast<uint64_t>(10000 + player)};
			build.target = {35, 35, 0};
			build.queue = true;
			CommandIntent move{};
			move.kind = CommandKind::MOVE;
			move.entity_ids = {static_cast<uint64_t>(10000 + player)};
			move.target = {static_cast<int>(40 + player), 42, 0};
			const bool first = client->send_guest_command(std::move(build));
			const bool second = client->send_guest_command(std::move(move));
			enqueued.at(player - 2) = (first and second) ? 1 : -1;
		});
	}
	release = true;
	for (auto &thread : senders) thread.join();
	for (auto sent : enqueued) TESTEQUALS(sent, 1);
	const size_t command_count = (count - 1) * 2;
	std::vector<AuthenticatedCommand> commands{};
	await_condition("all concurrent Builder commands", [&]() {
		auto incoming = authority->take_host_commands();
		commands.insert(commands.end(), incoming.begin(), incoming.end());
		return commands.size() >= command_count;
	});
	TESTEQUALS(commands.size(), command_count);
	std::map<gamestate::player_id_t, std::set<uint64_t>> seen_ids{};
	size_t builds{};
	size_t moves{};
	const auto first_builder = builders.begin()->first;
	for (const auto &command : commands) {
		const auto player = command.identity.player_id;
		TESTEQUALS(builders.contains(player), true);
		TESTEQUALS(command.identity.session_id != 0, true);
		TESTEQUALS(seen_ids[player].insert(command.intent.command_id).second, true);
		TESTEQUALS(command.intent.entity_ids.size(), size_t{1});
		TESTEQUALS(command.intent.entity_ids.front(),
		           uint64_t{10000 + player});
		CommandResult result{};
		result.command_id = command.intent.command_id;
		result.world_revision = 1;
		if (command.intent.kind == CommandKind::BUILD_TOWER) {
			++builds;
			TESTEQUALS(command.intent.queue, true);
			const coord::phys3 shared_site{35, 35, 0};
			TESTEQUALS(command.intent.target.ne == shared_site.ne, true);
			TESTEQUALS(command.intent.target.se == shared_site.se, true);
			result.status = player == first_builder
				? CommandStatus::ACCEPTED : CommandStatus::REJECTED;
			if (player != first_builder) result.reason = "Site already reserved";
		}
		else {
			++moves;
			TESTEQUALS(command.intent.kind == CommandKind::MOVE, true);
			result.status = CommandStatus::ACCEPTED;
		}
		TESTEQUALS(authority->send_command_result(
			command.identity, authority->get_session_generation(), std::move(result)), true);
	}
	TESTEQUALS(builds, count - 1);
	TESTEQUALS(moves, count - 1);
	for (const auto &[player, builder] : builders) {
		await_condition("private result delivery " + std::to_string(player), [&]() {
			return builder->status().pending_order_count == 0;
		});
		TESTEQUALS(seen_ids[player].size(), size_t{2});
	}
	// World deltas for all recipients can skip revisions while remaining
	// tied to their own private scores and base snapshots.
	uint64_t revision = 2;
	for (const auto &[player, builder] : builders) {
		authority->provide_host_update(private_world(
			count, player, revision++, 200 + player));
	}
	for (const auto &[player, builder] : builders) {
		await_condition("private delta for player " + std::to_string(player), [&]() {
			auto tracker = builder->get_replication();
			return tracker and tracker->get_revision() >= 2;
		});
		TESTEQUALS(builder->get_replication()->get_local_kills().value(),
		           uint64_t{200 + player});
	}
	// Exercise real concurrent recipient workers: overwrite several private
	// host captures before waiting, and require the final capture to arrive
	// intact for every Builder. A worker must delta against its *transmitted*
	// baseline rather than against a skipped intermediate capture.
	std::map<gamestate::player_id_t, uint64_t> final_revisions{};
	for (uint64_t wave = 0; wave < 8; ++wave) {
		for (const auto &[player, builder] : builders) {
			(void) builder;
			final_revisions[player] = revision;
			authority->provide_host_update(private_world(
				count, player, revision++, 1000 + wave * 100 + player));
		}
	}
	for (const auto &[player, builder] : builders) {
		await_condition("independent recipient final revision " + std::to_string(player),
		                [&]() {
			                auto tracker = builder->get_replication();
			                return tracker
			                    and tracker->get_revision() >= final_revisions.at(player);
		                });
		TESTEQUALS(builder->get_replication()->get_revision(),
		           final_revisions.at(player));
		TESTEQUALS(builder->get_replication()->get_local_kills().value(),
		           uint64_t{1700 + player});
	}
	// Each worker must also service its own authenticated recovery request
	// without corrupting other recipients' baselines or session sequences.
	for (const auto &[player, builder] : builders) {
		(void) player;
		TESTEQUALS(builder->request_recovery_snapshot(), true);
	}
	for (const auto &[player, builder] : builders) {
		await_condition("independent recipient snapshot recovery "
		                + std::to_string(player), [&]() {
			return builder->status().snapshot_recoveries >= 1;
		});
		TESTEQUALS(builder->get_replication()->get_revision(),
		           final_revisions.at(player));
	}
	// Authenticated commands are still accepted on all peer sockets after
	// concurrent delta streaming and recovery snapshots.
	for (const auto &[player, builder] : builders) {
		CommandIntent move{};
		move.kind = CommandKind::MOVE;
		move.entity_ids = {static_cast<uint64_t>(10000 + player)};
		move.target = {72, static_cast<int64_t>(50 + player), 0};
		TESTEQUALS(builder->send_guest_command(std::move(move)), true);
	}
	std::vector<AuthenticatedCommand> concurrent_orders{};
	await_condition("post-recovery parallel commands", [&]() {
		auto batch = authority->take_host_commands();
		concurrent_orders.insert(concurrent_orders.end(),
		                         batch.begin(), batch.end());
		return concurrent_orders.size() >= builders.size();
	});
	TESTEQUALS(concurrent_orders.size(), builders.size());
	for (const auto &command : concurrent_orders) {
		TESTEQUALS(builders.contains(command.identity.player_id), true);
		CommandResult result{};
		result.command_id = command.intent.command_id;
		result.status = CommandStatus::ACCEPTED;
		result.world_revision = revision;
		TESTEQUALS(authority->send_command_result(
			command.identity, authority->get_session_generation(),
			std::move(result)), true);
	}
	for (const auto &[player, builder] : builders) {
		await_condition("post-recovery command ACK " + std::to_string(player),
		                [&]() { return builder->status().pending_order_count == 0; });
	}
	// The original Builder creator is just another guest after rehosting.
	// Its loss must not close the Hunter's room or other Builder transports.
	const auto departed_id = force_builder_creator
		? original_creator_player : builders.rbegin()->first;
	auto departed_command = std::find_if(commands.begin(), commands.end(),
		[departed_id](const auto &command) {
			return command.identity.player_id == departed_id;
		});
	TESTEQUALS(departed_command != commands.end(), true);
	const auto departed_identity = departed_command->identity;
	const auto departed_generation = authority->get_session_generation();
	auto *departing = builders.at(departed_id);
	departing->close();
	await_condition("Hunter continues after Builder departure", [&]() {
		auto status = authority->status();
		return std::find(status.disconnected_players.begin(),
		                 status.disconnected_players.end(), departed_id)
		       != status.disconnected_players.end();
	});
	TESTEQUALS(authority->status().phase, std::string{"playing"});
	TESTEQUALS(authority->status().remote_player_ids.size(), count - 2);
	TESTEQUALS(authority->accepts_host_command(
		departed_identity, departed_generation), false);
	CommandResult obsolete{};
	obsolete.command_id = departed_command->intent.command_id;
	obsolete.status = CommandStatus::ACCEPTED;
	TESTEQUALS(authority->send_command_result(
		departed_identity, departed_generation, std::move(obsolete)), false);
	auto survivor = std::find_if(builders.begin(), builders.end(),
		[departed_id](const auto &item) { return item.first != departed_id; });
	TESTEQUALS(survivor != builders.end(), true);
	await_condition("surviving Builder receives departure notice", [&]() {
		return survivor->second->status().departure_notice.find("disconnected")
		       != std::string::npos;
	});
	CommandIntent continuing{};
	continuing.kind = CommandKind::MOVE;
	continuing.entity_ids = {static_cast<uint64_t>(10000 + survivor->first)};
	continuing.target = {58, 58, 0};
	TESTEQUALS(survivor->second->send_guest_command(continuing), true);
	std::vector<AuthenticatedCommand> after_departure{};
	await_condition("continued orders after original host loss", [&]() {
		auto batch = authority->take_host_commands();
		after_departure.insert(after_departure.end(), batch.begin(), batch.end());
		return not after_departure.empty();
	});
	TESTEQUALS(after_departure.size(), size_t{1});
	TESTEQUALS(after_departure.front().identity.player_id, survivor->first);
	CommandResult accepted{};
	accepted.command_id = after_departure.front().intent.command_id;
	accepted.status = CommandStatus::ACCEPTED;
	TESTEQUALS(authority->send_command_result(
		after_departure.front().identity, authority->get_session_generation(),
		std::move(accepted)), true);
	await_condition("survivor order acknowledged", [&]() {
		return survivor->second->status().pending_order_count == 0;
	});
	// Hunter loss is terminal. No Builder may continue running its own
	// divergent simulation or accept another command after the disconnect.
	authority->close();
	for (const auto &[player, builder] : builders) {
		if (player == departed_id) continue;
		await_condition("survivor terminates when Hunter closes", [&]() {
			return builder->status().phase == "error";
		});
		TESTEQUALS(builder->send_guest_command(continuing), false);
	}
	for (auto &client : clients) client->close();
	prepared.reset();
	listener.close();
	relay.join();
	if (relay_error) std::rethrow_exception(relay_error);
}

} // namespace

void lobby_capacity() {
	run_capacity_case(4, false);
	run_capacity_case(4, true);
	run_capacity_case(8, false);
	run_capacity_case(8, true);
}

} // namespace openage::network::tests
