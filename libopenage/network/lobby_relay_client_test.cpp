// Copyright 2026 the openage authors. See copying.md for legal info.

#include "lobby_relay_client.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <thread>

#include "network/relay.h"
#include "network/relay_multi_host_lobby.h"
#include "network/relay_service.h"
#include "network/replication.h"
#include "testing/testing.h"

namespace openage::network::tests {

namespace {

bool wait_for(RelayLobbyClient &client, const std::string &phase) {
	// The replacement Hunter room needs extra relay handshakes and proof
	// acknowledgements before the first snapshot; allow Windows CI time.
	for (size_t i = 0; i < 1000; ++i) {
		const auto state = client.status();
		if (state.phase == phase) return true;
		if (state.phase == "error") return false;
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	return false;
}

} // namespace

void lobby_rematch_room_visibility() {
	RelayRoomDirectory directory{64};
	auto listener = TcpListener::listen("127.0.0.1", 0);
	std::exception_ptr relay_error{};
	std::jthread relay{[&]() {
		try {
			serve_relay_directory(listener, directory, 32);
		}
		catch (...) {
			relay_error = std::current_exception();
		}
	}};
	struct CloseListener {
		TcpListener &listener;
		~CloseListener() { this->listener.close(); }
	} close_listener{listener};

	// Exercise both former match processes as the next room creator. Repeat
	// the browser snapshot because the live failure is reported as intermittent.
	for (size_t round = 0; round < 6; ++round) {
		RelayLobbyClient first{};
		RelayLobbyClient second{};
		RelayLobbyConfig match{};
		match.address = "127.0.0.1";
		match.port = listener.get_port();
		match.name = "Initial Host " + std::to_string(round);
		match.room_name = "Completed Match " + std::to_string(round);
		match.hosting = true;
		first.connect(match);
		TESTEQUALS(wait_for(first, "waiting"), true);

		match.hosting = false;
		match.name = "Initial Guest " + std::to_string(round);
		second.refresh(match);
		TESTEQUALS(wait_for(second, "menu"), true);
		match.room_id = second.status().suggested_room_id;
		TESTEQUALS(match.room_id != uint64_t{0}, true);
		second.connect(match);
		TESTEQUALS(wait_for(first, "lobby"), true);
		TESTEQUALS(wait_for(second, "lobby"), true);
		first.request_start();
		TESTEQUALS(wait_for(first, "assigned"), true);
		TESTEQUALS(wait_for(second, "assigned"), true);

		auto &authority = first.is_authority() ? first : second;
		auto &remote = first.is_authority() ? second : first;
		WorldState initial{};
		initial.map_size = 128;
		initial.world_revision = 1;
		initial.local_player_id = remote.status().local_player_id;
		initial.players = {
			{1, PlayerRole::HUNTER, true},
			{2, PlayerRole::BUILDER, true},
		};
		authority.provide_host_snapshot(initial);
		TESTEQUALS(wait_for(authority, "bootstrap"), true);
		TESTEQUALS(wait_for(remote, "bootstrap"), true);

		auto finished = initial;
		finished.world_revision = 2;
		finished.simulation_time = time::time_t::from_int(2700);
		finished.match_outcome = {
			MatchWinner::BUILDERS,
			MatchEndReason::TIME_LIMIT,
			finished.simulation_time,
		};
		authority.provide_host_update(finished);
		TESTEQUALS(wait_for(authority, "streaming"), true);
		TESTEQUALS(wait_for(remote, "streaming"), true);
		TESTEQUALS(remote.get_replication()->get_world_state()->match_outcome,
		           finished.match_outcome);
		first.close();
		second.close();

		auto &rehoster = round % 2 == 0 ? first : second;
		RelayLobbyConfig rematch{};
		rematch.address = "127.0.0.1";
		rematch.port = listener.get_port();
		rematch.name = "Rematch Host " + std::to_string(round);
		rematch.room_name = "Visible Three Player Rematch " + std::to_string(round);
		rematch.hosting = true;
		rematch.player_capacity = 3;
		rehoster.connect(rematch);
		RelayLobbyClient early_browser{};
		auto immediate_refresh = rematch;
		immediate_refresh.hosting = false;
		early_browser.refresh(immediate_refresh);
		TESTEQUALS(wait_for(early_browser, "menu"), true);
		TESTEQUALS(
			early_browser.status().rooms.find(rematch.room_name) != std::string::npos,
			true);
		early_browser.close();
		TESTEQUALS(wait_for(rehoster, "waiting"), true);

		auto &returning_player = round % 2 == 0 ? second : first;
		rematch.hosting = false;
		rematch.name = "Returning Player " + std::to_string(round);
		returning_player.refresh(rematch);
		TESTEQUALS(wait_for(returning_player, "menu"), true);
		const auto returning_browser = returning_player.status();
		TESTEQUALS(returning_browser.rooms.find(rematch.room_name) != std::string::npos, true);
		rematch.room_id = returning_browser.suggested_room_id;
		TESTEQUALS(rematch.room_id != uint64_t{0}, true);
		returning_player.connect(rematch);
		TESTEQUALS(wait_for(rehoster, "lobby"), true);
		TESTEQUALS(wait_for(returning_player, "lobby"), true);

		RelayLobbyClient third_player{};
		rematch.name = "Third Player " + std::to_string(round);
		for (size_t refresh = 0; refresh < 5; ++refresh) {
			third_player.refresh(rematch);
			TESTEQUALS(wait_for(third_player, "menu"), true);
			const auto browser = third_player.status();
			TESTEQUALS(browser.rooms.find(rematch.room_name) != std::string::npos, true);
			TESTEQUALS(browser.suggested_room_id, rematch.room_id);
		}
		third_player.close();
		returning_player.close();
		rehoster.close();

		bool room_removed = false;
		for (size_t attempt = 0; attempt < 50; ++attempt) {
			if (list_relay_rooms(
			        rematch.address, rematch.port, RelayToken{}).rooms.empty()) {
				room_removed = true;
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
		}
		TESTEQUALS(room_removed, true);
	}

	listener.close();
	relay.join();
	if (relay_error) {
		std::rethrow_exception(relay_error);
	}
}

void lobby_relay_client() {
	RelayLobbyClient invalid{};
	RelayLobbyConfig bad{};
	bad.name = "Test";
	bad.room_name = "Test Room";
	bad.hosting = true;
	bad.token_hex = "foo";
	TESTTHROWS(invalid.connect(bad));
	TESTEQUALS(invalid.status().phase, std::string{"menu"});

	// The match-ending rule belongs to the assigned gameplay role,
	// not to a guest's connection position in the lobby.
	RelayLobbyStatus roles{};
	roles.phase = "bootstrap";
	roles.connected = true;
	roles.local_role = "HUNTER";
	roles.local_player_id = 1;
	roles.remote_player_id = 2;
	TESTEQUALS(host_can_continue_after_guest_loss(roles), true);
	roles.phase = "streaming";
	TESTEQUALS(host_can_continue_after_guest_loss(roles), true);
	roles.phase = "playing";
	TESTEQUALS(host_can_continue_after_guest_loss(roles), true);
	roles.phase = "assigned";
	TESTEQUALS(host_can_continue_after_guest_loss(roles), false);
	roles.phase = "playing";
	roles.local_role = "BUILDER";
	roles.local_player_id = 2;
	roles.remote_player_id = 1;
	TESTEQUALS(host_can_continue_after_guest_loss(roles), false);
	roles.local_role = "HUNTER";
	roles.local_player_id = 1;
	roles.remote_player_id = 2;
	roles.connected = false;
	TESTEQUALS(host_can_continue_after_guest_loss(roles), false);

	RelayRoomDirectory directory{4};
	auto listener = TcpListener::listen("127.0.0.1", 0);
	std::exception_ptr relay_error{};
	std::jthread relay{[&]() {
		try {
			serve_relay_directory(listener, directory, 8);
		}
		catch (...) {
			relay_error = std::current_exception();
		}
	}};

	RelayLobbyClient host{};
	RelayLobbyClient guest{};
	RelayLobbyConfig conf{};
	conf.address = "127.0.0.1";
	conf.port = listener.get_port();
	conf.name = "HostPlayer";
	conf.room_name = "Automated Test";
	conf.hosting = true;
	host.connect(conf);
	TESTEQUALS(wait_for(host, "waiting"), true);

	conf.name = "GuestPlayer";
	guest.refresh(conf);
	TESTEQUALS(wait_for(guest, "menu"), true);
	// Public-room fields must not be displaced by the appended role fields.
	// The old positional publish() construction silently coupled their order.
	const auto room_listing = guest.status();
	TESTEQUALS(room_listing.rooms.find("Automated Test") != std::string::npos, true);
	TESTEQUALS(room_listing.relay_network_version,
	           std::to_string(PROTOCOL_MAJOR) + "." + std::to_string(PROTOCOL_MINOR));
	TESTEQUALS(room_listing.suggested_room_id != 0, true);
	TESTEQUALS(room_listing.roles.empty(), true);
	TESTEQUALS(room_listing.local_role.empty(), true);
	TESTEQUALS(room_listing.role_seconds_remaining, uint8_t{0});
	conf.room_id = room_listing.suggested_room_id;
	TESTEQUALS(conf.room_id != 0, true);
	conf.name = "GuestPlayer";
	conf.hosting = false;
	guest.connect(conf);

	TESTEQUALS(wait_for(host, "lobby"), true);
	TESTEQUALS(wait_for(guest, "lobby"), true);
	TESTEQUALS(host.status().can_start, true);
	TESTEQUALS(guest.status().can_start, false);
	TESTEQUALS(host.status().rooms.empty(), true);
	TESTEQUALS(guest.status().roles.empty(), true);
	TESTEQUALS(host.status().roles.empty(), true);
	TESTEQUALS(host.status().participants.find("GuestPlayer") != std::string::npos, true);
	TESTEQUALS(guest.status().participants.find("HostPlayer") != std::string::npos, true);
	// A guest cannot start the match by setting its own flag.
	guest.request_start();
	TESTEQUALS(guest.status().phase, std::string{"lobby"});
	host.request_start();
	TESTEQUALS(wait_for(host, "assigned"), true);
	TESTEQUALS(wait_for(guest, "assigned"), true);
	TESTEQUALS(host.status().roles, guest.status().roles);
	// Even the legacy two-peer path must tell the presenter how many
	// starting Builders to keep in the authoritative simulation.
	TESTEQUALS(host.status().match_player_count, size_t{2});
	TESTEQUALS(guest.status().match_player_count, size_t{2});
	TESTEQUALS(host.status().rooms.empty(), true);
	TESTEQUALS(host.status().suggested_room_id, uint64_t{0});
	TESTEQUALS(guest.status().rooms.empty(), true);
	TESTEQUALS(guest.status().suggested_room_id, uint64_t{0});
	TESTEQUALS(host.status().roles.find("HostPlayer") != std::string::npos, true);
	TESTEQUALS(host.status().roles.find("GuestPlayer") != std::string::npos, true);
	TESTEQUALS(host.status().roles.find("HUNTER") != std::string::npos, true);
	TESTEQUALS(host.status().roles.find("BUILDER") != std::string::npos, true);
	TESTEQUALS(host.status().local_role != guest.status().local_role, true);
	TESTEQUALS(host.status().local_role == "HUNTER" or host.status().local_role == "BUILDER", true);
	TESTEQUALS(guest.status().local_role == "HUNTER" or guest.status().local_role == "BUILDER", true);
	// The timer is monotonic; delayed CI scheduling may already have passed
	// the five-second interval. Exact boundary checks live in lobby_test.
	TESTEQUALS(host.status().role_seconds_remaining <= 5, true);
	TESTEQUALS(guest.status().role_seconds_remaining <= 5, true);
	TESTEQUALS(host.status().local_player_id != guest.status().local_player_id, true);
	TESTEQUALS(host.status().remote_player_id, guest.status().local_player_id);
	TESTEQUALS(guest.status().remote_player_id, host.status().local_player_id);
	TESTEQUALS(host.status().local_player_id + guest.status().local_player_id,
	           gamestate::player_id_t{3});
	TESTEQUALS(guest.get_replication() == nullptr, true);

	// Authority belongs to the Hunter after a transparent time-zero rehost.
	// It is deliberately unrelated to which person created the room.
	RelayLobbyClient &authority = host.is_authority() ? host : guest;
	RelayLobbyClient &remote = host.is_authority() ? guest : host;
	TESTEQUALS(authority.status().local_player_id, gamestate::player_id_t{1});
	TESTEQUALS(remote.status().local_player_id, gamestate::player_id_t{2});
	WorldState initial{};
	initial.map_size = 128;
	initial.world_revision = 1;
	initial.local_player_id = remote.status().local_player_id;
	initial.local_kill_count = 7;
	initial.players = {
		{1, PlayerRole::HUNTER, true},
		{2, PlayerRole::BUILDER, true},
	};
	auto wrong_recipient = initial;
	wrong_recipient.local_player_id = authority.status().local_player_id;
	TESTTHROWS(authority.provide_host_snapshot(wrong_recipient));
	auto wrong_base = initial;
	wrong_base.base_revision = 3;
	TESTTHROWS(authority.provide_host_snapshot(wrong_base));

	authority.provide_host_snapshot(initial);
	TESTTHROWS(authority.provide_host_snapshot(initial));
	TESTEQUALS(wait_for(remote, "bootstrap"), true);
	TESTEQUALS(wait_for(authority, "bootstrap"), true);
	auto replication = remote.get_replication();
	TESTEQUALS(replication != nullptr, true);
	TESTEQUALS(replication->get_local_player_id(), initial.local_player_id);
	TESTEQUALS(replication->get_revision(), uint64_t{1});
	TESTEQUALS(replication->get_local_kills().value(), uint64_t{7});
	TESTEQUALS(replication->get_world_state()->players.size(), size_t{2});
	TESTEQUALS(authority.status().snapshot_revision, uint64_t{1});
	TESTEQUALS(remote.status().snapshot_revision, uint64_t{1});
	TESTEQUALS(authority.status().match_player_count, size_t{2});
	TESTEQUALS(remote.status().match_player_count, size_t{2});
	TESTEQUALS(authority.status().roles, remote.status().roles);
	TESTEQUALS(authority.status().local_role != remote.status().local_role, true);

	// The same authenticated relay session must continue carrying authority
	// changes after MATCH_READY; no second connection or new role draw.
	auto next = initial;
	next.world_revision = 2;
	next.local_kill_count = 8;
	auto wrong_update = next;
	wrong_update.local_player_id = authority.status().local_player_id;
	TESTTHROWS(authority.provide_host_update(wrong_update));
	authority.provide_host_update(next);
	TESTTHROWS(authority.provide_host_update(initial));
	TESTEQUALS(wait_for(authority, "streaming"), true);
	TESTEQUALS(wait_for(remote, "streaming"), true);
	TESTEQUALS(remote.status().snapshot_revision, uint64_t{2});
	TESTEQUALS(replication->get_revision(), uint64_t{2});
	TESTEQUALS(replication->get_local_kills().value(), uint64_t{8});
	TESTEQUALS(remote.status().roles, authority.status().roles);

	// Revision numbers can skip captured intermediate samples: the world
	// delta base is the last transmitted revision, not revision minus one.
	auto later = next;
	later.world_revision = 4;
	later.local_kill_count = 10;
	authority.provide_host_update(later);
	bool got_later = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		if (remote.status().snapshot_revision == 4) {
			got_later = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(got_later, true);
	TESTEQUALS(replication->get_revision(), uint64_t{4});
	TESTEQUALS(replication->get_local_kills().value(), uint64_t{10});

	// The same session must recover a full recipient-specific world from
	// the authority's most recently transmitted delta baseline.
	TESTEQUALS(remote.request_recovery_snapshot(), true);
	bool recovered = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		if (remote.status().snapshot_recoveries == 1) {
			recovered = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(recovered, true);
	TESTEQUALS(replication->get_revision(), uint64_t{4});
	TESTEQUALS(replication->get_local_kills().value(), uint64_t{10});

	// The role reveal must finish on both peers before human orders are sent.
	bool host_ready = false;
	bool guest_ready = false;
	for (size_t attempt = 0; attempt < 700; ++attempt) {
		host_ready = authority.status().phase == "playing";
		guest_ready = remote.status().phase == "playing";
		if (host_ready and guest_ready) break;
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(host_ready, true);
	TESTEQUALS(guest_ready, true);
	// Even without remote commands, the accepted session remains alive via
	// validated PING/PONG frames rather than relying on TCP peer_connected().
	bool heartbeats_received = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		if (remote.status().heartbeat_acknowledgements >= 2) {
			heartbeats_received = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(heartbeats_received, true);
	TESTEQUALS(authority.status().connected, true);
	TESTEQUALS(remote.status().connected, true);
	CommandIntent movement{};
	movement.kind = CommandKind::MOVE;
	movement.entity_ids = {42};
	movement.target = {17, 18, 0};
	TESTEQUALS(authority.send_guest_command(movement), false);
	TESTEQUALS(remote.send_guest_command(movement), true);
	std::vector<AuthenticatedCommand> submitted{};
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		submitted = authority.take_host_commands();
		if (not submitted.empty()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(submitted.size(), size_t{1});
	TESTEQUALS(submitted[0].identity.player_id, initial.local_player_id);
	TESTEQUALS(submitted[0].intent.kind == CommandKind::MOVE, true);
	TESTEQUALS(submitted[0].intent.entity_ids == movement.entity_ids, true);
	TESTEQUALS(submitted[0].intent.command_id, uint64_t{1});
	CommandResult answer{};
	answer.command_id = submitted[0].intent.command_id;
	answer.status = CommandStatus::REJECTED;
	answer.world_revision = 4;
	answer.reason = "unauthorized unit";
	const auto active_generation = authority.get_session_generation();
	const auto source = submitted[0].identity;
	// A delayed callback cannot reply as a different player or a prior
	// session generation, even if its command ID is reused.
	TESTEQUALS(authority.send_command_result(
		{source.session_id + 1, source.player_id}, active_generation, answer), false);
	TESTEQUALS(authority.send_command_result(
		source, active_generation + 1, answer), false);
	TESTEQUALS(authority.send_command_result(source, active_generation, answer), true);
	std::vector<CommandResult> decisions{};
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		decisions = remote.take_command_results();
		if (not decisions.empty()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(decisions.size(), size_t{1});
	TESTEQUALS(decisions[0].command_id, answer.command_id);
	TESTEQUALS(decisions[0].status == CommandStatus::REJECTED, true);
	TESTEQUALS(decisions[0].reason, answer.reason);
	TESTEQUALS(remote.status().pending_order_count, size_t{0});
	TESTEQUALS(remote.status().order_feedback.find("Move rejected") != std::string::npos, true);

	// Special orders retain their payloads and command identities in the
	// authenticated relay, even with several in flight at once. The authority
	// can decide independently; the remote's feedback follows each result.
	CommandIntent queued_tower{};
	queued_tower.kind = CommandKind::BUILD_TOWER;
	queued_tower.entity_ids = {42};
	queued_tower.target = {25, 26, 0};
	queued_tower.queue = true;
	CommandIntent focus{};
	focus.kind = CommandKind::FOCUS_ATTACK;
	focus.entity_ids = {55};
	focus.target_entity = gamestate::entity_id_t{77};
	focus.target = {30, 31, 0};
	CommandIntent prioritize{};
	prioritize.kind = CommandKind::PRIORITIZE_FOUNDATION;
	prioritize.entity_ids = {42};
	prioritize.target_entity = gamestate::entity_id_t{88};
	prioritize.target = {33, 34, 0};
	TESTEQUALS(remote.send_guest_command(queued_tower), true);
	TESTEQUALS(remote.send_guest_command(focus), true);
	TESTEQUALS(remote.send_guest_command(prioritize), true);
	TESTEQUALS(remote.status().pending_order_count, size_t{3});
	std::vector<AuthenticatedCommand> special{};
	for (size_t attempt = 0; attempt < 300 and special.size() < 3; ++attempt) {
		auto batch = authority.take_host_commands();
		special.insert(special.end(), batch.begin(), batch.end());
		if (special.size() < 3) {
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
		}
	}
	TESTEQUALS(special.size(), size_t{3});
	TESTEQUALS(special[0].intent.command_id, uint64_t{2});
	TESTEQUALS(special[1].intent.command_id, uint64_t{3});
	TESTEQUALS(special[2].intent.command_id, uint64_t{4});
	TESTEQUALS(special[0].intent.kind == CommandKind::BUILD_TOWER, true);
	TESTEQUALS(special[0].intent.queue, true);
	TESTEQUALS(special[1].intent.kind == CommandKind::FOCUS_ATTACK, true);
	TESTEQUALS(special[1].intent.target_entity == focus.target_entity, true);
	TESTEQUALS(special[2].intent.kind == CommandKind::PRIORITIZE_FOUNDATION, true);
	TESTEQUALS(special[2].intent.target_entity == prioritize.target_entity, true);
	for (size_t i = 0; i < special.size(); ++i) {
		CommandResult decision{};
		decision.command_id = special[i].intent.command_id;
		decision.status = i == 2 ? CommandStatus::REJECTED : CommandStatus::ACCEPTED;
		decision.reason = i == 2 ? "foundation unavailable" : "";
		TESTEQUALS(authority.send_command_result(source, active_generation, decision), true);
	}
	std::vector<CommandResult> special_results{};
	for (size_t attempt = 0; attempt < 300 and special_results.size() < 3; ++attempt) {
		auto batch = remote.take_command_results();
		special_results.insert(special_results.end(), batch.begin(), batch.end());
		if (special_results.size() < 3) {
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
		}
	}
	TESTEQUALS(special_results.size(), size_t{3});
	TESTEQUALS(remote.status().pending_order_count, size_t{0});
	TESTEQUALS(remote.status().order_feedback.find("Prioritize foundation rejected")
	           != std::string::npos, true);

	// Every Builder is now an ordinary guest, even if that person
	// originally created the room. The Hunter's authority survives.
	remote.close();
	bool builder_left = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		const auto current = authority.status();
		if (current.remote_player_id == 0) {
			builder_left = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(builder_left, true);
	TESTEQUALS(authority.status().phase, std::string{"playing"});
	TESTEQUALS(authority.send_command_result(source, active_generation, answer), false);
	TESTEQUALS(authority.take_host_commands().empty(), true);
	TESTEQUALS(remote.send_guest_command(movement), false);
	authority.close();
	TESTEQUALS(authority.status().phase, std::string{"menu"});
	TESTEQUALS(authority.send_command_result(source, active_generation, answer), false);
	TESTEQUALS(authority.get_session_generation() != active_generation, true);

	// Test the opposite failure direction in a fresh relay room. Host loss
	// must invalidate the guest's authoritative state, including its score.
	// Reuse the exact RelayLobbyClient to exercise generation changes across
	// F10-style close and reconnect, not merely a different object.
	RelayLobbyClient &host2 = host;
	RelayLobbyClient guest2{};
	conf.hosting = true;
	conf.room_id = 0;
	conf.name = "HostAgain";
	host2.connect(conf);
	TESTEQUALS(wait_for(host2, "waiting"), true);
	guest2.refresh(conf);
	TESTEQUALS(wait_for(guest2, "menu"), true);
	conf.room_id = guest2.status().suggested_room_id;
	TESTEQUALS(conf.room_id != 0, true);
	conf.name = "GuestAgain";
	conf.hosting = false;
	guest2.connect(conf);
	TESTEQUALS(wait_for(host2, "lobby"), true);
	TESTEQUALS(wait_for(guest2, "lobby"), true);
	host2.request_start();
	TESTEQUALS(wait_for(host2, "assigned"), true);
	TESTEQUALS(wait_for(guest2, "assigned"), true);
	RelayLobbyClient &authority2 = host2.is_authority() ? host2 : guest2;
	RelayLobbyClient &remote2 = host2.is_authority() ? guest2 : host2;
	TESTEQUALS(authority2.send_command_result(source, active_generation, answer), false);
	auto restart = initial;
	restart.local_player_id = remote2.status().local_player_id;
	authority2.provide_host_snapshot(restart);
	TESTEQUALS(wait_for(remote2, "bootstrap"), true);
	TESTEQUALS(wait_for(authority2, "bootstrap"), true);
	auto last_replication = remote2.get_replication();
	TESTEQUALS(last_replication != nullptr, true);
	TESTEQUALS(last_replication->is_host_connected(), true);
	authority2.close();
	TESTEQUALS(wait_for(remote2, "error"), true);
	TESTEQUALS(remote2.status().connected, false);
	TESTEQUALS(last_replication->is_host_connected(), false);
	TESTEQUALS(last_replication->get_local_kills().has_value(), false);
	TESTEQUALS(remote2.send_guest_command(movement), false);
	remote2.close();

	// End-to-end relay client integration with one authoritative host and
	// TWO live guests: shared role assignment, private snapshots, independent
	// command/decision channels, and continued play after Builder loss.
	RelayLobbyClient multiplayer_host{};
	RelayLobbyClient multi_guest_a{};
	RelayLobbyClient multi_guest_b{};
	RelayLobbyConfig multi{};
	multi.address = "127.0.0.1";
	multi.port = listener.get_port();
	multi.name = "Many Host";
	multi.room_name = "Three Human Game";
	multi.hosting = true;
	multi.player_capacity = 3;
	multiplayer_host.connect(multi);
	TESTEQUALS(wait_for(multiplayer_host, "waiting"), true);
	multi.hosting = false;
	multi.name = "Guest A";
	multi_guest_a.refresh(multi);
	TESTEQUALS(wait_for(multi_guest_a, "menu"), true);
	multi.room_id = multi_guest_a.status().suggested_room_id;
	TESTEQUALS(multi.room_id != uint64_t{0}, true);
	multi_guest_a.connect(multi);
	TESTEQUALS(wait_for(multi_guest_a, "lobby"), true);
	TESTEQUALS(wait_for(multiplayer_host, "lobby"), true);
	multi.name = "Guest B";
	multi_guest_b.connect(multi);
	TESTEQUALS(wait_for(multi_guest_b, "lobby"), true);
	bool both_in_lobby = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		if (multiplayer_host.status().participants.find("Guest B") != std::string::npos
		    and multi_guest_a.status().participants.find("Guest B") != std::string::npos) {
			both_in_lobby = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(both_in_lobby, true);
	multiplayer_host.request_start();
	std::array<RelayLobbyClient *, 3> players{
		&multiplayer_host, &multi_guest_a, &multi_guest_b};
	for (auto *player : players) {
		TESTEQUALS(wait_for(*player, "assigned"), true);
	}
	// The original room creator is not necessarily the Hunter anymore.
	RelayLobbyClient *hunter_authority = nullptr;
	std::vector<RelayLobbyClient *> builders{};
	for (auto *player : players) {
		if (player->is_authority()) {
			TESTEQUALS(hunter_authority == nullptr, true);
			hunter_authority = player;
		}
		else builders.push_back(player);
	}
	TESTEQUALS(hunter_authority != nullptr, true);
	TESTEQUALS(builders.size(), size_t{2});
	TESTEQUALS(hunter_authority->status().local_role, std::string{"HUNTER"});
	auto multi_status = hunter_authority->status();
	TESTEQUALS(multi_status.multi_host, true);
	TESTEQUALS(multi_status.match_player_count, size_t{3});
	TESTEQUALS(multi_status.remote_player_ids.size(), size_t{2});
	for (auto *player : players) {
		TESTEQUALS(player->status().roles, multi_status.roles);
	}
	TESTEQUALS(builders[0]->status().local_player_id
	           != builders[1]->status().local_player_id, true);
	for (auto id : multi_status.remote_player_ids) {
		WorldState world{};
		world.map_size = 128;
		world.world_revision = 1;
		world.local_player_id = id;
		world.local_kill_count = 10 + id;
		world.players = {
			{1, PlayerRole::HUNTER, true},
			{2, PlayerRole::BUILDER, true},
			{3, PlayerRole::BUILDER, true}};
		hunter_authority->provide_host_snapshot(std::move(world));
	}
	for (auto *player : players) {
		TESTEQUALS(wait_for(*player, "bootstrap"), true);
		// Guests and the Hunter must agree on the first authoritative
		// world revision before any live delta is transmitted.
		TESTEQUALS(player->status().snapshot_revision, uint64_t{1});
	}
	TESTEQUALS(hunter_authority->status().snapshot_revision, uint64_t{1});
	for (auto *builder : builders) {
		TESTEQUALS(builder->get_replication() != nullptr, true);
		TESTEQUALS(builder->get_replication()->get_local_kills().value(),
		           uint64_t{10 + builder->status().local_player_id});
	}
	auto update_for = [](gamestate::player_id_t player, uint64_t revision,
	                     uint64_t kills) {
		WorldState world{};
		world.map_size = 128;
		world.world_revision = revision;
		world.local_player_id = player;
		world.local_kill_count = kills;
		world.players = {
			{1, PlayerRole::HUNTER, true},
			{2, PlayerRole::BUILDER, true},
			{3, PlayerRole::BUILDER, true}};
		return world;
	};
	hunter_authority->provide_host_update(
		update_for(builders[0]->status().local_player_id, 2, 22));
	hunter_authority->provide_host_update(
		update_for(builders[1]->status().local_player_id, 3, 33));
	for (auto *builder : builders) {
		TESTEQUALS(wait_for(*builder, "streaming"), true);
	}
	TESTEQUALS(builders[0]->get_replication()->get_local_kills().value(),
	           uint64_t{22});
	TESTEQUALS(builders[1]->get_replication()->get_local_kills().value(),
	           uint64_t{33});
	bool all_playing = false;
	for (size_t attempt = 0; attempt < 700; ++attempt) {
		all_playing = std::all_of(players.begin(), players.end(),
			[](RelayLobbyClient *client) {
				return client->status().phase == "playing";
			});
		if (all_playing) break;
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(all_playing, true);
	CommandIntent move{};
	move.kind = CommandKind::MOVE;
	move.entity_ids = {501};
	move.target = {22, 23, 0};
	TESTEQUALS(builders[0]->send_guest_command(move), true);
	move.entity_ids = {502};
	TESTEQUALS(builders[1]->send_guest_command(move), true);
	std::vector<AuthenticatedCommand> multi_commands{};
	for (size_t attempt = 0; attempt < 300 and multi_commands.size() < 2; ++attempt) {
		auto batch = hunter_authority->take_host_commands();
		multi_commands.insert(multi_commands.end(), batch.begin(), batch.end());
		if (multi_commands.size() < 2)
			std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(multi_commands.size(), size_t{2});
	TESTEQUALS(multi_commands.at(0).identity.player_id
	           != multi_commands.at(1).identity.player_id, true);
	for (const auto &cmd : multi_commands) {
		CommandResult result{};
		result.command_id = cmd.intent.command_id;
		result.status = CommandStatus::ACCEPTED;
		result.world_revision = 3;
		TESTEQUALS(hunter_authority->send_command_result(
			cmd.identity, hunter_authority->get_session_generation(), result), true);
	}
	bool both_confirmed = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		if (builders[0]->status().pending_order_count == 0
		    and builders[1]->status().pending_order_count == 0) {
			both_confirmed = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(both_confirmed, true);
	// If the original room creator was randomly assigned Builder,
	// disconnect THAT machine. Hunter authority must remain online.
	auto *departing = hunter_authority == &multiplayer_host
		? builders[0] : &multiplayer_host;
	auto *survivor = departing == builders[0] ? builders[1] : builders[0];
	const auto departed_id = departing->status().local_player_id;
	const auto survivor_id = survivor->status().local_player_id;
	departing->close();
	bool departed_recorded = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		const auto current = hunter_authority->status();
		if (std::find(current.disconnected_players.begin(),
		              current.disconnected_players.end(), departed_id)
		    != current.disconnected_players.end()) {
			departed_recorded = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(departed_recorded, true);
	TESTEQUALS(hunter_authority->status().phase, std::string{"playing"});
	TESTEQUALS(hunter_authority->status().remote_player_ids.size(), size_t{1});
	bool survivor_notified = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		if (survivor->status().departure_notice.find("disconnected") != std::string::npos) {
			survivor_notified = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(survivor_notified, true);
	auto after_departure = update_for(survivor_id, 4, 34);
	after_departure.players.at(departed_id - 1).connected = false;
	hunter_authority->provide_host_update(std::move(after_departure));
	bool updated_disconnected = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		auto current = survivor->get_replication()->get_world_state();
		if (current and current->world_revision == 4) {
			updated_disconnected = true;
			TESTEQUALS(current->players.at(departed_id - 1).connected, false);
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(updated_disconnected, true);
	for (const auto &cmd : multi_commands) {
		if (cmd.identity.player_id != departed_id) continue;
		CommandResult stale{};
		stale.command_id = cmd.intent.command_id;
		stale.status = CommandStatus::ACCEPTED;
		TESTEQUALS(hunter_authority->send_command_result(
			cmd.identity, hunter_authority->get_session_generation(), stale), false);
	}
	CommandIntent continuing{};
	continuing.kind = CommandKind::MOVE;
	continuing.entity_ids = {509};
	continuing.target = {51, 52, 0};
	TESTEQUALS(survivor->send_guest_command(continuing), true);
	std::vector<AuthenticatedCommand> continuation{};
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		continuation = hunter_authority->take_host_commands();
		if (not continuation.empty()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(continuation.size(), size_t{1});
	TESTEQUALS(continuation.front().identity.player_id, survivor_id);
	CommandResult accepted{};
	accepted.command_id = continuation.front().intent.command_id;
	accepted.status = CommandStatus::ACCEPTED;
	TESTEQUALS(hunter_authority->send_command_result(
		continuation.front().identity, hunter_authority->get_session_generation(),
		accepted), true);
	bool acknowledged = false;
	for (size_t attempt = 0; attempt < 300; ++attempt) {
		if (survivor->status().pending_order_count == 0) {
			acknowledged = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(acknowledged, true);
	hunter_authority->close();
	TESTEQUALS(wait_for(*survivor, "error"), true);
	for (auto *player : players) player->close();

	// Regression: when the old room creator draws Builder in a TWO-person
	// match, the replacement Hunter owns a prepared relay room with one
	// available Builder channel. It must NOT launch the legacy 2-player
	// hosting worker and advertise a second room, waiting for manual Start.
	// Repeat the deterministic Builder-created Hunter rehost because
	// Linux exposed intermittent bootstrap revision-zero failures (7/10).
	// Assert the first authoritative revision in every independent room.
	for (size_t attempt = 0; attempt < 10; ++attempt) {
		HostLobby old_lobby{1, "Original Builder Host"};
		old_lobby.join(2, "New Hunter");
		const auto old_assignment = old_lobby.start(
			[](uint64_t) { return uint64_t{1}; }); // Guest draws Hunter.
		TESTEQUALS(player_for_participant(old_assignment, 1), gamestate::player_id_t{2});
		TESTEQUALS(player_for_participant(old_assignment, 2), gamestate::player_id_t{1});
		auto private_room = std::make_shared<RelayMultiHostLobby>(
			"127.0.0.1", listener.get_port(), RelayToken{},
			"2-player authority handoff", "New Hunter", 2);
		private_room->prepare_hunter_rehost(old_assignment);
		const auto new_room_id = private_room->room_id();

		RelayLobbyClient promoted_hunter{};
		RelayLobbyConfig promoted_config{};
		promoted_config.address = "127.0.0.1";
		promoted_config.port = listener.get_port();
		promoted_config.name = "New Hunter";
		promoted_config.room_name = "2-player authority handoff";
		promoted_config.hosting = true;
		promoted_config.player_capacity = 2;
		promoted_config.rehost_start = old_assignment;
		promoted_config.prepared_rehost_lobby = private_room;
		promoted_hunter.connect(promoted_config);
		TESTEQUALS(wait_for(promoted_hunter, "waiting"), true);
		TESTEQUALS(promoted_hunter.is_authority(), true);

		RelayLobbyClient former_creator{};
		RelayLobbyConfig former_config{};
		former_config.address = "127.0.0.1";
		former_config.port = listener.get_port();
		former_config.name = "Original Builder Host";
		former_config.hosting = false;
		former_config.room_id = new_room_id;
		former_config.rehost_offer = private_room->rehost_offer_for(2);
		former_creator.connect(former_config);
		TESTEQUALS(wait_for(promoted_hunter, "assigned"), true);
		TESTEQUALS(wait_for(former_creator, "assigned"), true);
		TESTEQUALS(promoted_hunter.status().multi_host, true);
		TESTEQUALS(promoted_hunter.status().local_player_id, gamestate::player_id_t{1});
		TESTEQUALS(former_creator.status().local_player_id, gamestate::player_id_t{2});
		TESTEQUALS(former_creator.status().local_role, std::string{"BUILDER"});
		TESTEQUALS(directory.room_count(), size_t{0}); // No phantom second room.

		WorldState authority_bootstrap{};
		authority_bootstrap.map_size = 128;
		authority_bootstrap.world_revision = 1;
		authority_bootstrap.local_player_id = 2;
		authority_bootstrap.players = {
			{1, PlayerRole::HUNTER, true}, {2, PlayerRole::BUILDER, true}};
		promoted_hunter.provide_host_snapshot(std::move(authority_bootstrap));
		TESTEQUALS(wait_for(promoted_hunter, "bootstrap"), true);
		TESTEQUALS(wait_for(former_creator, "bootstrap"), true);
		TESTEQUALS(promoted_hunter.status().snapshot_revision, uint64_t{1});
		TESTEQUALS(former_creator.status().snapshot_revision, uint64_t{1});
		TESTEQUALS(former_creator.get_replication()->get_world_state()->players.size(),
		           size_t{2});
		former_creator.close();
		promoted_hunter.close();
	}
	listener.close();
	relay.join();
	if (relay_error) std::rethrow_exception(relay_error);
}

} // namespace openage::network::tests
