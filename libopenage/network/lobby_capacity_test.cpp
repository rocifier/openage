// Copyright 2026 the openage authors. See copying.md for legal info.

// Live RelayLobbyClient integration, without the Qt presenter. In contrast
// to the lower-level multi_session TCP test this covers the same workers,
// room admission and role-preserving rehost used by actual game processes.
#include "lobby_relay_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <exception>
#include <map>
#include <memory>
#include <numeric>
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

// Opt-in benchmark mode uses the real relay and RelayLobbyClient workers.
 // It deliberately supplies scripted authoritative worlds instead of a Qt
 // graphical process; networking measurements must be labelled accordingly.
void run_capacity_case(size_t count, bool force_builder_creator, bool benchmark = false) {
	// Four and eight actual lobby clients use independent background workers,
	// not the simplified raw TCP handshake harness.
	TESTEQUALS(count == 4 or count == 5 or count == 8, true);
	RelayRoomDirectory directory{4};
	const auto optional_port = [](const char *name, uint32_t maximum) -> uint32_t {
		const auto raw = std::getenv(name);
		if (not raw or not *raw) return 0;
		const auto number = std::stoul(raw);
		if (number == 0 or number > maximum) {
			throw Error{MSG(err) << "Invalid FVD benchmark port: " << name};
		}
		return static_cast<uint32_t>(number);
	};
	const auto relay_port = benchmark ? optional_port("FVD_BENCH_RELAY_PORT", 65535) : 0;
	const auto proxy_base = benchmark ? optional_port("FVD_BENCH_PROXY_BASE", 65535) : 0;
	if (proxy_base and proxy_base + count - 2 > 65535) {
		throw Error{MSG(err) << "FVD benchmark proxy port range exceeds 65535"};
	}
	auto listener = TcpListener::listen("127.0.0.1", static_cast<uint16_t>(relay_port));
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
			guest.port = (benchmark and proxy_base)
			             ? static_cast<uint16_t>(proxy_base + i - 1) : host.port;
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
			guest.port = (benchmark and proxy_base)
			             ? static_cast<uint16_t>(proxy_base + i - 1) : host.port;
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
	if (benchmark) {
		// Verify the network topology used by the proxy and timing reports.
		// Only the Hunter uses the unshaped listener; all Builders are shaped.
		TESTEQUALS(authority == &creator, true);
		for (const auto &[player, builder] : builders) {
			TESTEQUALS(player >= 2 and player <= 5, true);
			TESTEQUALS(builder != authority, true);
		}
		std::cout << "FVD_BENCH_TOPOLOGY hunter=direct builders=proxied"
		          << " remote_builders=" << builders.size() << std::endl;
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
	if (benchmark) {
		// Baseline and shaped runs use identical commands / private snapshots.
		// The external proxy (if used) delays actual TCP bytes on each guest
		// connection. Synthetic arrow records stress the same wire codec as
		// gameplay, but cannot establish simulated combat or frame rendering.
		const auto config_number = [](const char *name, size_t fallback, size_t maximum) {
			const auto raw = std::getenv(name);
			if (not raw or not *raw) return fallback;
			const auto value = std::stoul(raw);
			if (value == 0 or value > maximum) {
				throw Error{MSG(err) << "Invalid FVD benchmark parameter " << name};
			}
			return static_cast<size_t>(value);
		};
		const size_t rounds = config_number("FVD_BENCH_ROUNDS", 12, 200);
		const size_t arrows = std::getenv("FVD_BENCH_ARROWS")
			? static_cast<size_t>(std::stoul(std::getenv("FVD_BENCH_ARROWS"))) : 0;
		if (arrows > MAX_STATE_ARROWS) {
			throw Error{MSG(err) << "FVD benchmark arrows must be <= MAX_STATE_ARROWS"};
		}
		auto percentile = [](std::vector<double> samples, size_t percent) {
			std::sort(samples.begin(), samples.end());
			return samples[(samples.size() * percent + 99) / 100 - 1];
		};
		std::vector<double> command_ms{};
		std::vector<double> world_ms{};
		std::map<gamestate::player_id_t, std::vector<double>> command_per_client{};
		std::map<gamestate::player_id_t, std::vector<double>> world_per_client{};
		command_ms.reserve(rounds * builders.size());
		world_ms.reserve(rounds * builders.size());
		for (const auto &[player, client] : builders) {
			(void) player;
			client->take_command_results(); // Drain previous smoke-check results.
		}
		uint64_t next_revision = 100;
		uint64_t next_arrow_id = 1;
		for (size_t round = 0; round < rounds; ++round) {
			std::map<gamestate::player_id_t, clock_t::time_point> began{};
			for (const auto &[player, client] : builders) {
				CommandIntent move{};
				move.kind = CommandKind::MOVE;
				move.entity_ids = {uint64_t{10000} + player};
				move.target = {static_cast<int64_t>(50 + round), static_cast<int64_t>(40 + player), 0};
				began[player] = clock_t::now();
				TESTEQUALS(client->send_guest_command(std::move(move)), true);
			}
			std::vector<AuthenticatedCommand> pending{};
			await_condition("benchmark commands arrived at Hunter", [&]() {
				auto more = authority->take_host_commands();
				pending.insert(pending.end(), more.begin(), more.end());
				return pending.size() >= builders.size();
			});
			TESTEQUALS(pending.size(), builders.size());
			for (const auto &command : pending) {
				TESTEQUALS(command.intent.kind, CommandKind::MOVE);
				CommandResult accepted{};
				accepted.command_id = command.intent.command_id;
				accepted.status = CommandStatus::ACCEPTED;
				accepted.world_revision = next_revision;
				TESTEQUALS(authority->send_command_result(
					command.identity, authority->get_session_generation(), std::move(accepted)), true);
			}
			std::set<gamestate::player_id_t> completed{};
			await_condition("benchmark guest command ACKs", [&]() {
				for (const auto &[player, client] : builders) {
					for (const auto &result : client->take_command_results()) {
						TESTEQUALS(result.status, CommandStatus::ACCEPTED);
						TESTEQUALS(completed.insert(player).second, true);
						const auto elapsed_ms =
							std::chrono::duration<double, std::milli>(clock_t::now() - began.at(player)).count();
						command_ms.push_back(elapsed_ms);
						command_per_client[player].push_back(elapsed_ms);
					}
				}
				return completed.size() == builders.size();
			});
			began.clear();
			std::map<gamestate::player_id_t, uint64_t> expected{};
			for (const auto &[player, client] : builders) {
				began[player] = clock_t::now();
				auto world = private_world(count, player, next_revision++, 1000 + round + player);
				world.simulation_time = time::time_t::from_int(100 + static_cast<int64_t>(round));
				for (size_t index = 0; index < arrows; ++index) {
					world.arrows.push_back({
						next_arrow_id++, 1, 1,
						{40, 40, 1}, {42, 42, 0},
						time::time_t::from_int(90), time::time_t::from_double(90.2)});
				}
				expected[player] = world.world_revision;
				authority->provide_host_update(std::move(world));
			}
			completed.clear();
			await_condition("benchmark world delivered to guests", [&]() {
				for (const auto &[player, client] : builders) {
					if (completed.contains(player)) continue;
					auto tracker = client->get_replication();
					if (tracker and tracker->get_revision() >= expected.at(player)) {
						TESTEQUALS(tracker->get_local_kills().value(), 1000 + round + player);
						completed.insert(player);
						const auto elapsed_ms =
							std::chrono::duration<double, std::milli>(clock_t::now() - began.at(player)).count();
						world_ms.push_back(elapsed_ms);
						world_per_client[player].push_back(elapsed_ms);
					}
				}
				return completed.size() == builders.size();
			});
		}
		std::cout << std::fixed << std::setprecision(2)
			<< "FVD_BENCH_RESULT players=" << count
			<< " remote=" << builders.size()
			<< " rounds=" << rounds << " arrows_per_recipient_update=" << arrows
			<< " command_samples=" << command_ms.size()
			<< " command_p50_ms=" << percentile(command_ms, 50)
			<< " command_p95_ms=" << percentile(command_ms, 95)
			<< " command_max_ms=" << *std::max_element(command_ms.begin(), command_ms.end())
			<< " world_samples=" << world_ms.size()
			<< " world_p50_ms=" << percentile(world_ms, 50)
			<< " world_p95_ms=" << percentile(world_ms, 95)
			<< " world_max_ms=" << *std::max_element(world_ms.begin(), world_ms.end())
			<< std::endl;
		for (const auto &[player, client] : builders) {
			(void) client;
			const auto &command = command_per_client.at(player);
			const auto &world = world_per_client.at(player);
			TESTEQUALS(command.size(), rounds);
			TESTEQUALS(world.size(), rounds);
			std::cout << std::fixed << std::setprecision(2)
				<< "FVD_BENCH_CLIENT player=" << player
				<< " command_p50_ms=" << percentile(command, 50)
				<< " command_p95_ms=" << percentile(command, 95)
				<< " command_max_ms=" << *std::max_element(command.begin(), command.end())
				<< " world_p50_ms=" << percentile(world, 50)
				<< " world_p95_ms=" << percentile(world, 95)
				<< " world_max_ms=" << *std::max_element(world.begin(), world.end())
				<< std::endl;
		}
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

void lobby_latency() {
	// The public lobby's random role draw can make a proxy-connected guest
	// become Hunter authority. That would put ALL downstream world traffic
	// through one shaped "Builder" link and invalidate the benchmark.
	// Use the existing deterministic Hunter-rehost scenario instead: the
	// benchmark creator is Hunter on the direct relay socket, and all four
	// Builder clients connect through their dedicated shaping proxies.
	run_capacity_case(5, true, true);
}

void lobby_capacity() {
	run_capacity_case(4, false);
	run_capacity_case(4, true);
	run_capacity_case(8, false);
	run_capacity_case(8, true);
}

} // namespace openage::network::tests
