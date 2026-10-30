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
#include <iterator>
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
		// No cross-client barriers: commands and recipient-private captures
		// are emitted on independent wall-clock schedules throughout the run.
		const auto param = [](const char *name, size_t fallback, size_t cap) {
			const auto raw = std::getenv(name);
			if (not raw or not *raw) return fallback;
			const auto n = std::stoul(raw);
			if (n == 0 or n > cap) throw Error{MSG(err) << "Invalid network benchmark parameter " << name};
			return static_cast<size_t>(n);
		};
		const size_t rounds = param("FVD_BENCH_ROUNDS", 20, 200);
		const size_t update_ms = param("FVD_BENCH_UPDATE_MS", 500, 2000);
		const size_t command_ms = param("FVD_BENCH_COMMAND_MS", 500, 2000);
		const size_t arrows = std::getenv("FVD_BENCH_ARROWS")
			? static_cast<size_t>(std::stoul(std::getenv("FVD_BENCH_ARROWS"))) : 0;
		if (arrows > MAX_STATE_ARROWS) {
			throw Error{MSG(err) << "FVD benchmark arrows exceed MAX_STATE_ARROWS"};
		}
		using instant = clock_t::time_point;
		using samples_t = std::vector<double>;
		const auto elapsed_ms = [](instant now, instant then) {
			return std::chrono::duration<double, std::milli>(now - then).count();
		};
		const auto percentile = [](samples_t data, size_t percent) {
			if (data.empty()) return 0.0;
			std::sort(data.begin(), data.end());
			return data[(data.size() * percent + 99) / 100 - 1];
		};
		const auto maximum = [](const samples_t &data) {
			return data.empty() ? 0.0 : *std::max_element(data.begin(), data.end());
		};
		struct Peer {
			samples_t commands{}, worlds{}, state_age{};
			std::map<uint64_t, instant> pending_commands{}, offered_worlds{};
			instant last_capture{};
			uint64_t next_command_id{}, applied{}, offered{};
			uint64_t commands_sent{}, commands_received{}, updates_sent{}, updates_received{}, skipped{};
			bool alive{true};
		};
		std::map<gamestate::player_id_t, Peer> peers{};
		for (const auto &[player, client] : builders) {
			auto &p = peers[player];
			p.next_command_id = *seen_ids.at(player).rbegin() + 1;
			p.applied = client->get_replication()->get_revision();
			client->take_command_results();
		}
		const auto started = clock_t::now();
		const auto ending = started + std::chrono::milliseconds{static_cast<int64_t>(rounds * update_ms)};
		const auto drain_ending = ending + std::chrono::milliseconds{1500};
		const auto update_period = std::chrono::milliseconds{static_cast<int64_t>(update_ms)};
		const auto command_period = std::chrono::milliseconds{static_cast<int64_t>(command_ms)};
		auto next_update = started, next_command = started, next_age_sample = started;
		uint64_t next_revision = 100, next_arrow_id = 1, update_round = 0, command_round = 0;
		uint64_t accepted_by_host = 0;
		std::cout << "FVD_BENCH_SCHEDULE mode=independent update_ms=" << update_ms
		          << " command_ms=" << command_ms
		          << " duration_ms=" << rounds * update_ms
		          << " drain_ms=1500" << std::endl;
		while (clock_t::now() < drain_ending) {
			const auto now = clock_t::now();
			if (now < ending and now >= next_command) {
				for (const auto &[player, client] : builders) {
					auto &p = peers.at(player);
					CommandIntent move{};
					move.kind = CommandKind::MOVE;
					move.entity_ids = {uint64_t{10000} + player};
					move.target = {static_cast<int64_t>(50 + command_round % 32),
					               static_cast<int64_t>(40 + player), 0};
					const auto id = p.next_command_id++;
					TESTEQUALS(client->send_guest_command(std::move(move)), true);
					TESTEQUALS(p.pending_commands.emplace(id, now).second, true);
					++p.commands_sent;
				}
				++command_round;
				next_command += command_period;
			}
			if (now < ending and now >= next_update) {
				for (const auto &[player, client] : builders) {
					(void) client;
					auto &p = peers.at(player);
					auto world = private_world(count, player, next_revision++, 1000 + update_round);
					world.simulation_time = time::time_t::from_int(100 + static_cast<int64_t>(update_round));
					for (size_t index = 0; index < arrows; ++index) {
						world.arrows.push_back({
							next_arrow_id++, 1, 1, {40, 40, 1}, {42, 42, 0},
							time::time_t::from_int(90), time::time_t::from_double(90.2)});
					}
					p.offered = world.world_revision;
					p.offered_worlds.emplace(world.world_revision, now);
					++p.updates_sent;
					authority->provide_host_update(std::move(world));
				}
				++update_round;
				next_update += update_period;
			}
			// A slow client never delays the next scheduled host offer.
			for (auto &command : authority->take_host_commands()) {
				const auto player = command.identity.player_id;
				TESTEQUALS(peers.contains(player), true);
				TESTEQUALS(command.intent.kind, CommandKind::MOVE);
				TESTEQUALS(command.intent.entity_ids.size(), size_t{1});
				TESTEQUALS(command.intent.entity_ids.front(), uint64_t{10000} + player);
				CommandResult reply{};
				reply.command_id = command.intent.command_id;
				reply.status = CommandStatus::ACCEPTED;
				reply.world_revision = next_revision;
				TESTEQUALS(authority->send_command_result(
					command.identity, authority->get_session_generation(), std::move(reply)), true);
				++accepted_by_host;
			}
			for (const auto &[player, client] : builders) {
				auto &p = peers.at(player);
				const auto observed = clock_t::now();
				for (const auto &reply : client->take_command_results()) {
					TESTEQUALS(reply.status, CommandStatus::ACCEPTED);
					auto sent = p.pending_commands.find(reply.command_id);
					TESTEQUALS(sent != p.pending_commands.end(), true);
					p.commands.push_back(elapsed_ms(observed, sent->second));
					p.pending_commands.erase(sent);
					++p.commands_received;
				}
				auto tracker = client->get_replication();
				if (tracker and tracker->has_state()) {
					const auto revision = tracker->get_revision();
					if (revision > p.applied) {
						auto applied = p.offered_worlds.find(revision);
						if (applied != p.offered_worlds.end()) {
							p.worlds.push_back(elapsed_ms(observed, applied->second));
							p.last_capture = applied->second;
							++p.updates_received;
							p.skipped += static_cast<uint64_t>(
								std::distance(p.offered_worlds.begin(), applied));
							p.offered_worlds.erase(p.offered_worlds.begin(), std::next(applied));
						}
						p.applied = revision;
					}
				}
				if (client->status().phase == "error") p.alive = false;
				if (observed >= next_age_sample) {
					p.state_age.push_back(elapsed_ms(observed,
						p.last_capture == instant{} ? started : p.last_capture));
				}
			}
			if (now >= next_age_sample) next_age_sample += std::chrono::milliseconds{50};
			std::this_thread::sleep_for(std::chrono::milliseconds{5});
		}
		samples_t commands{}, worlds{}, state_ages{};
		uint64_t total_sent = 0, total_returned = 0, total_offered = 0, total_received = 0, total_skipped = 0;
		std::cout << std::fixed << std::setprecision(2);
		for (const auto &[player, p] : peers) {
			commands.insert(commands.end(), p.commands.begin(), p.commands.end());
			worlds.insert(worlds.end(), p.worlds.begin(), p.worlds.end());
			state_ages.insert(state_ages.end(), p.state_age.begin(), p.state_age.end());
			total_sent += p.commands_sent;
			total_returned += p.commands_received;
			total_offered += p.updates_sent;
			total_received += p.updates_received;
			total_skipped += p.skipped;
			std::cout << "FVD_BENCH_CLIENT player=" << player
			          << " command_offered=" << p.commands_sent
			          << " command_samples=" << p.commands.size()
			          << " command_pending=" << p.pending_commands.size()
			          << " command_p50_ms=" << percentile(p.commands, 50)
			          << " command_p95_ms=" << percentile(p.commands, 95)
			          << " command_max_ms=" << maximum(p.commands)
			          << " world_offered=" << p.updates_sent
			          << " world_samples=" << p.worlds.size()
			          << " world_skipped=" << p.skipped
			          << " world_p50_ms=" << percentile(p.worlds, 50)
			          << " world_p95_ms=" << percentile(p.worlds, 95)
			          << " world_max_ms=" << maximum(p.worlds)
			          << " state_age_p95_ms=" << percentile(p.state_age, 95)
			          << " state_age_max_ms=" << maximum(p.state_age)
			          << " latest_offered_revision=" << p.offered
			          << " latest_applied_revision=" << p.applied
			          << " alive=" << (p.alive ? 1 : 0) << std::endl;
		}
		std::cout << "FVD_BENCH_RESULT players=" << count
		          << " remote=" << builders.size()
		          << " rounds=" << update_round
		          << " arrows_per_recipient_update=" << arrows
		          << " mode=independent"
		          << " command_offered=" << total_sent
		          << " command_samples=" << total_returned
		          << " command_authority_accepted=" << accepted_by_host
		          << " command_p50_ms=" << percentile(commands, 50)
		          << " command_p95_ms=" << percentile(commands, 95)
		          << " command_max_ms=" << maximum(commands)
		          << " world_offered=" << total_offered
		          << " world_samples=" << total_received
		          << " world_skipped=" << total_skipped
		          << " world_p50_ms=" << percentile(worlds, 50)
		          << " world_p95_ms=" << percentile(worlds, 95)
		          << " world_max_ms=" << maximum(worlds)
		          << " state_age_p95_ms=" << percentile(state_ages, 95)
		          << " state_age_max_ms=" << maximum(state_ages)
		          << std::endl;
		// Do not run the original post-benchmark capacity smoke tests:
		// the slowest peer intentionally remains behind after this experiment.
		for (auto &client : clients) client->close();
		prepared.reset();
		listener.close();
		relay.join();
		if (relay_error) std::rethrow_exception(relay_error);
		return;
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
