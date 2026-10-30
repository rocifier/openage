// Copyright 2026 the openage authors. See copying.md for legal info.

#include "lobby_relay_client.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <atomic>
#include <mutex>
#include <vector>
#include <limits>
#include <utility>

#include "error/error.h"
#include "log/log.h"
#include "log/message.h"
#include "network/handshake.h"
#include "network/lobby.h"
#include "network/replication.h"
#include "network/relay.h"
#include "network/relay_multi_host_lobby.h"
#include "network/authority_succession.h"
#include "network/world_delta.h"

namespace openage::network {

namespace {

constexpr size_t MAX_INFLIGHT_COMMANDS = 256;
// Heartbeats are sent over the accepted, sequenced ConnectionSession.
// Short stalls are tolerated; a silent relay/peer eventually ends the match.
constexpr auto LIVE_HEARTBEAT_INTERVAL = std::chrono::seconds{1};
constexpr auto LIVE_SESSION_TIMEOUT = std::chrono::seconds{15};
constexpr auto LIVE_POLL_INTERVAL = std::chrono::milliseconds{5};

void log_role_draw(const StartMatch &assignment) {
	const auto creator = player_for_participant(
		assignment, assignment.host_participant_id);
	log::log(INFO << "[FVD/authority] role_draw creator_player=" << creator
	              << " hunter_player=1 participants=" << assignment.assignments.size()
	              << " handoff=" << (creator == 1 ? "not_needed" : "required"));
}

const char *order_name(CommandKind kind) {
	switch (kind) {
	case CommandKind::MOVE: return "Move";
	case CommandKind::ATTACK_MOVE: return "Attack-move";
	case CommandKind::BUILD_TOWER: return "Build tower";
	case CommandKind::FOCUS_ATTACK: return "Focus attack";
	case CommandKind::PRIORITIZE_FOUNDATION: return "Prioritize foundation";
	case CommandKind::SET_STANCE: return "Stance";
	case CommandKind::PATROL: return "Patrol";
	case CommandKind::DEMOLISH_TOWER: return "Demolish tower";
	case CommandKind::SET_RALLY_POINT: return "Rally point";
	}
	return "Order";
}

uint8_t hex_digit(char ch) {
	if (ch >= '0' and ch <= '9') return static_cast<uint8_t>(ch - '0');
	if (ch >= 'a' and ch <= 'f') return static_cast<uint8_t>(ch - 'a' + 10);
	if (ch >= 'A' and ch <= 'F') return static_cast<uint8_t>(ch - 'A' + 10);
	throw Error{MSG(err) << "The relay token must contain hexadecimal characters"};
}

RelayToken parse_token(const std::string &hex) {
	if (hex.empty()) {
		return {};
	}
	if (hex.size() != RELAY_TOKEN_SIZE * 2) {
		throw Error{MSG(err) << "A player token needs exactly 64 hexadecimal characters"};
	}
	RelayToken token{};
	for (size_t i = 0; i < RELAY_TOKEN_SIZE; ++i) {
		token[i] = static_cast<uint8_t>(
			hex_digit(hex[i * 2]) * 16 + hex_digit(hex[i * 2 + 1]));
	}
	if (std::all_of(token.begin(), token.end(), [](uint8_t value) {
		    return value == 0;
	    })) {
		throw Error{MSG(err) << "A player token must not be all zero"};
	}
	return token;
}

// Temporary developer-only digest. Replace with the configured gameplay
// asset-manifest SHA-256 before opening Internet matchmaking.
AssetDigest provisional_gameplay_digest() {
	AssetDigest digest{};
	for (size_t i = 0; i < digest.size(); ++i) {
		digest[i] = static_cast<uint8_t>(i + 61);
	}
	return digest;
}

std::string local_role_name(const StartMatch &start, gamestate::player_id_t local_player) {
	for (const auto &assignment : start.assignments) {
		if (assignment.player_id == local_player) {
			return assignment.role == PlayerRole::HUNTER ? "HUNTER" : "BUILDER";
		}
	}
	throw Error{MSG(err) << "No FVD role assigned to local player"};
}

void validate_endpoint(const RelayLobbyConfig &cfg) {
	if (cfg.address.empty() or cfg.port == 0) {
		throw Error{MSG(err) << "Relay address and port are required"};
	}
	(void) parse_token(cfg.token_hex);
}

void validate_config(const RelayLobbyConfig &cfg) {
	validate_endpoint(cfg);
	if (cfg.name.empty() or cfg.name.size() > MAX_PARTICIPANT_NAME_SIZE) {
		throw Error{MSG(err) << "A valid player name is required"};
	}
	if (cfg.hosting) {
		if (cfg.player_capacity < 2 or cfg.player_capacity > MAX_STATE_PLAYERS) {
			throw Error{MSG(err) << "Host player capacity must be between 2 and 8"};
		}
		if (cfg.room_name.empty()
		    or cfg.room_name.size() > MAX_RELAY_ROOM_NAME_SIZE) {
			throw Error{MSG(err) << "A valid room name is required"};
		}
	}
	else if (cfg.room_id == 0) {
		throw Error{MSG(err) << "Choose a relay room before joining"};
	}
}

} // namespace

bool host_can_continue_after_guest_loss(const RelayLobbyStatus &status) {
	// Only continue a bootstrapped match. Losing the Hunter always ends it;
	// the local authoritative Hunter can survive a remote Builder leaving.
	return status.connected and status.local_role == "HUNTER"
	       and status.local_player_id == 1 and status.remote_player_id >= 2
	       and (status.phase == "bootstrap" or status.phase == "streaming"
	            or status.phase == "playing");
}

RelayLobbyClient::~RelayLobbyClient() {
	this->close();
}

void RelayLobbyClient::connect(RelayLobbyConfig settings) {
	this->close();
	// Validate before creating any network worker. A role-preserving
	// Hunter rehost MUST use its already-advertised private room even when
	// there are only two people. Otherwise the legacy 2-player host path
	// creates a second, unrelated room and both clients wait forever.
	validate_config(settings);
	if (settings.rehost_start.has_value() != bool(settings.prepared_rehost_lobby)
	    or (settings.prepared_rehost_lobby and not settings.hosting)) {
		throw Error{MSG(err) << "FVD Hunter rehost requires a prepared host room and original role mapping"};
	}
	this->stopping = false;
	this->start_requested = false;
	{
		std::lock_guard lock{this->mutex};
		this->hosting = settings.hosting;
		this->pending_host_snapshot.reset();
		this->pending_host_update.reset();
		this->pending_multi_snapshots.clear();
		this->offered_multi_snapshots.clear();
		this->pending_multi_updates.clear();
		this->pending_multi_results.clear();
		this->active_multi_identities.clear();
		this->pending_guest_commands.clear();
		this->incoming_host_commands.clear();
		this->pending_command_results.clear();
		this->guest_command_results.clear();
		this->guest_outstanding_commands.clear();
		this->active_remote_identity.reset();
		this->next_guest_command_id = 1;
		this->guest_snapshot_requested = false;
		this->last_host_offered_revision = 0;
		this->delivered_projectiles.clear();
		this->guest_replication.reset();
		this->multi_host_lobby.reset();
		this->prepared_rehost_lobby.reset();
		this->handoff_client.reset();
		this->roles_assigned_at.reset();
	}
	this->publish(RelayLobbyStatus{
		.phase = "connecting",
		.message = settings.hosting ? "Creating relay room..." : "Joining relay room...",
	});
	this->worker = std::thread{[this, settings = std::move(settings)]() {
		if (settings.hosting
		    and (settings.player_capacity > 2 or settings.prepared_rehost_lobby)) {
			// Rehosts always run the authenticated multi-slot admission
			// manager, including a Hunter plus exactly one Builder.
			this->run_multi_host(settings);
		}
		else {
			this->run(settings);
		}
	}};
}

void RelayLobbyClient::refresh(RelayLobbyConfig settings) {
	this->close();
	validate_endpoint(settings);
	this->stopping = false;
	this->publish(RelayLobbyStatus{
		.phase = "refreshing",
		.message = "Refreshing available games...",
	});
	this->worker = std::thread{[this, settings = std::move(settings)]() {
		this->run_refresh(settings);
	}};
}

std::shared_ptr<RelayLobbyClient> RelayLobbyClient::replacement() const {
	std::lock_guard lock{this->mutex};
	return this->handoff_client;
}

bool RelayLobbyClient::is_authority() const {
	if (auto next = this->replacement()) return next->is_authority();
	std::lock_guard lock{this->mutex};
	return this->hosting;
}

void RelayLobbyClient::request_start() {
	if (auto next = this->replacement()) { next->request_start(); return; }
	std::lock_guard lock{this->mutex};
	if (this->current.can_start and this->current.connected) {
		this->start_requested = true;
	}
}

void RelayLobbyClient::provide_host_snapshot(WorldState snapshot) {
	if (auto next = this->replacement()) { next->provide_host_snapshot(std::move(snapshot)); return; }
	std::lock_guard lock{this->mutex};
	// Simulation captures can finish one GUI frame after a remote Builder
	// has departed. Ignore that specific obsolete recipient without ending
	// the authoritative match.
	if (this->current.multi_host
	    and std::find(this->current.disconnected_players.begin(),
	                  this->current.disconnected_players.end(), snapshot.local_player_id)
	        != this->current.disconnected_players.end()) return;
	if (not this->hosting or this->current.phase != "assigned"
	    or (this->current.remote_player_id == 0
	        and this->active_multi_identities.empty())
	    or (this->active_multi_identities.empty()
	        ? snapshot.local_player_id != this->current.remote_player_id
	        : not this->active_multi_identities.contains(snapshot.local_player_id))
	    or snapshot.base_revision != 0 or snapshot.world_revision == 0
	    or this->pending_host_snapshot) {
		throw Error{MSG(err) << "Invalid or duplicate FVD host bootstrap snapshot"};
	}
	this->last_host_offered_revision = snapshot.world_revision;
	if (not this->active_multi_identities.empty()) {
		auto recipient = snapshot.local_player_id;
		if (not this->offered_multi_snapshots.insert(recipient).second) {
			throw Error{MSG(err) << "Duplicate multi-guest bootstrap snapshot"};
		}
		this->pending_multi_snapshots.emplace(recipient, std::move(snapshot));
	}
	else this->pending_host_snapshot = std::move(snapshot);
}

uint64_t RelayLobbyClient::delivered_projectile_id(
	gamestate::player_id_t recipient) const {
	if (auto next = this->replacement()) return next->delivered_projectile_id(recipient);
	std::lock_guard lock{this->mutex};
	auto found = this->delivered_projectiles.find(recipient);
	return found == this->delivered_projectiles.end() ? 0 : found->second;
}

void RelayLobbyClient::provide_host_update(WorldState world) {
	if (auto next = this->replacement()) { next->provide_host_update(std::move(world)); return; }
	std::lock_guard lock{this->mutex};
	if (this->current.multi_host
	    and std::find(this->current.disconnected_players.begin(),
	                  this->current.disconnected_players.end(), world.local_player_id)
	        != this->current.disconnected_players.end()) return;
	if (not this->hosting
	    or (this->current.phase != "bootstrap" and this->current.phase != "streaming")
	    or (this->current.remote_player_id == 0
	        and this->active_multi_identities.empty())
	    or (this->active_multi_identities.empty()
	        ? world.local_player_id != this->current.remote_player_id
	        : not this->active_multi_identities.contains(world.local_player_id))
	    or world.base_revision != 0
	    or world.world_revision <= this->last_host_offered_revision) {
		throw Error{MSG(err) << "Invalid or stale FVD host world update"};
	}
	// A single pending sample is enough: the worker diffs against the last
	// transmitted world, never against a dropped intermediate capture.
	this->last_host_offered_revision = world.world_revision;
	if (not this->active_multi_identities.empty()) {
		this->pending_multi_updates.insert_or_assign(world.local_player_id, std::move(world));
	}
	else this->pending_host_update = std::move(world);
}

bool RelayLobbyClient::reveal_finished_locked() const {
	if (not this->roles_assigned_at) return false;
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - *this->roles_assigned_at);
	return role_reveal_seconds_remaining(elapsed) == 0;
}

bool RelayLobbyClient::send_guest_command(CommandIntent intent) {
	if (auto next = this->replacement()) return next->send_guest_command(std::move(intent));
	std::lock_guard lock{this->mutex};
	if (this->hosting or not this->current.connected
	    or (this->current.phase != "bootstrap" and this->current.phase != "streaming")
	    or not this->reveal_finished_locked()
	    or this->guest_outstanding_commands.size() >= MAX_INFLIGHT_COMMANDS) {
		return false;
	}
	intent.command_id = this->next_guest_command_id++;
	this->guest_outstanding_commands.emplace(intent.command_id, intent.kind);
	this->current.pending_order_count = this->guest_outstanding_commands.size();
	this->current.order_feedback = std::string{order_name(intent.kind)} + " sent...";
	this->pending_guest_commands.push_back(std::move(intent));
	return true;
}

std::vector<AuthenticatedCommand> RelayLobbyClient::take_host_commands() {
	if (auto next = this->replacement()) return next->take_host_commands();
	std::lock_guard lock{this->mutex};
	std::vector<AuthenticatedCommand> drained;
	drained.reserve(this->incoming_host_commands.size());
	while (not this->incoming_host_commands.empty()) {
		drained.push_back(std::move(this->incoming_host_commands.front()));
		this->incoming_host_commands.pop_front();
	}
	return drained;
}

uint64_t RelayLobbyClient::get_session_generation() const {
	if (auto next = this->replacement()) return next->get_session_generation();
	std::lock_guard lock{this->mutex};
	return this->session_generation;
}

bool RelayLobbyClient::accepts_host_command(
	const SessionIdentity &identity, uint64_t generation) const {
	if (auto next = this->replacement()) return next->accepts_host_command(identity, generation);
	std::lock_guard lock{this->mutex};
	if (this->stopping or not this->hosting or not this->current.connected
	    or generation != this->session_generation) return false;
	if (not this->active_multi_identities.empty()) {
		auto found = this->active_multi_identities.find(identity.player_id);
		return found != this->active_multi_identities.end()
		    and found->second.session_id == identity.session_id;
	}
	return this->active_remote_identity
	    and this->active_remote_identity->session_id == identity.session_id
	    and this->active_remote_identity->player_id == identity.player_id;
}

bool RelayLobbyClient::send_command_result(
	const SessionIdentity &identity, uint64_t generation, CommandResult result) {
	if (auto next = this->replacement()) return next->send_command_result(identity, generation, std::move(result));
	std::lock_guard lock{this->mutex};
	if (this->stopping or not this->hosting or not this->current.connected
	    or generation != this->session_generation
	    or (not this->active_multi_identities.empty()
	        ? (not this->active_multi_identities.contains(identity.player_id)
	           or this->active_multi_identities.at(identity.player_id).session_id != identity.session_id)
	        : (not this->active_remote_identity
	           or this->active_remote_identity->session_id != identity.session_id
	           or this->active_remote_identity->player_id != identity.player_id))) {
		return false;
	}
	if (this->pending_command_results.size() >= MAX_INFLIGHT_COMMANDS) {
		throw Error{MSG(err) << "FVD command-result queue capacity exceeded"};
	}
	if (not this->active_multi_identities.empty()) {
		auto &queue = this->pending_multi_results[identity.player_id];
		if (queue.size() >= MAX_INFLIGHT_COMMANDS) {
			throw Error{MSG(err) << "FVD per-peer command-result queue is full"};
		}
		queue.push_back(std::move(result));
	}
	else this->pending_command_results.push_back(std::move(result));
	return true;
}

std::vector<CommandResult> RelayLobbyClient::take_command_results() {
	if (auto next = this->replacement()) return next->take_command_results();
	std::lock_guard lock{this->mutex};
	std::vector<CommandResult> drained;
	drained.reserve(this->guest_command_results.size());
	while (not this->guest_command_results.empty()) {
		drained.push_back(std::move(this->guest_command_results.front()));
		this->guest_command_results.pop_front();
	}
	return drained;
}

bool RelayLobbyClient::request_recovery_snapshot() {
	if (auto next = this->replacement()) return next->request_recovery_snapshot();
	std::lock_guard lock{this->mutex};
	if (this->hosting or not this->current.connected
	    or (this->current.phase != "bootstrap"
	        and this->current.phase != "streaming")
	    or not this->guest_replication) return false;
	this->guest_snapshot_requested = true;
	return true;
}

std::shared_ptr<ReplicationTracker> RelayLobbyClient::get_replication() const {
	if (auto next = this->replacement()) return next->get_replication();
	std::lock_guard lock{this->mutex};
	return this->guest_replication;
}

RelayLobbyStatus RelayLobbyClient::status() const {
	if (auto next = this->replacement()) return next->status();
	std::lock_guard lock{this->mutex};
	auto result = this->current;
	if (this->roles_assigned_at
	    and (result.phase == "assigned" or result.phase == "bootstrap"
	         or result.phase == "streaming")) {
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - *this->roles_assigned_at);
		result.role_seconds_remaining = role_reveal_seconds_remaining(elapsed);
		if (result.role_seconds_remaining == 0
		    and (result.phase == "bootstrap" or result.phase == "streaming")) {
			result.phase = "playing";
		}
	}
	return result;
}

void RelayLobbyClient::publish(RelayLobbyStatus next) {
	std::lock_guard lock{this->mutex};
	if (next.phase == "assigned" and not this->roles_assigned_at) {
		// Start exactly once per lobby at the receipt/publication of START_MATCH.
		this->roles_assigned_at = std::chrono::steady_clock::now();
	}
	this->current = std::move(next);
}

void RelayLobbyClient::publish_stream_revision(uint64_t revision, bool local_host) {
	std::lock_guard lock{this->mutex};
	this->current.phase = "streaming";
	this->current.message = local_host
		? "Host-authoritative world updates active."
		: "Receiving authoritative match state from host.";
	// Recipient-private multi-host streams can complete captures in a
	// different order. The Hunter's global status must not move backwards
	// when a slower Builder stream reports an earlier world revision.
	this->current.snapshot_revision =
		std::max(this->current.snapshot_revision, revision);
}

void RelayLobbyClient::close() {
	this->stopping = true;
	this->start_requested = false;
	std::shared_ptr<TcpConnection> socket;
	std::shared_ptr<RelayMultiHostLobby> multi_host;
	std::shared_ptr<RelayLobbyClient> successor;
	std::string previous_phase{};
	gamestate::player_id_t previous_player{};
	{
		std::lock_guard lock{this->mutex};
		previous_phase = this->current.phase;
		previous_player = this->current.local_player_id;
		successor = this->handoff_client;
		++this->session_generation;
		this->active_remote_identity.reset();
		this->active_multi_identities.clear();
		socket = this->connection;
		multi_host = this->multi_host_lobby;
	}
	if (previous_phase != "menu") {
		log::log(INFO << "[FVD/match] local_session_closed phase="
		              << previous_phase << " player=" << previous_player
		              << " replacement=" << (successor ? "yes" : "no"));
	}
	if (socket) socket->close();
	if (multi_host) multi_host->close();
	if (successor) successor->close();
	if (this->worker.joinable()) this->worker.join();
	{
		std::lock_guard lock{this->mutex};
		this->connection.reset();
		this->pending_host_snapshot.reset();
		this->pending_host_update.reset();
		this->pending_multi_snapshots.clear();
		this->offered_multi_snapshots.clear();
		this->pending_multi_updates.clear();
		this->pending_multi_results.clear();
		this->pending_guest_commands.clear();
		this->incoming_host_commands.clear();
		this->pending_command_results.clear();
		this->guest_command_results.clear();
		this->guest_outstanding_commands.clear();
		this->next_guest_command_id = 1;
		this->guest_snapshot_requested = false;
		this->last_host_offered_revision = 0;
		this->guest_replication.reset();
		this->handoff_client.reset();
		this->prepared_rehost_lobby.reset();
		this->roles_assigned_at.reset();
		this->hosting = false;
		this->current = {};
	}
}

void RelayLobbyClient::run(RelayLobbyConfig settings) {
	std::optional<AuthoritySuccession> succession{};
	try {
		auto socket = std::make_shared<TcpConnection>(
			TcpConnection::connect(settings.address, settings.port));
		{
			std::lock_guard lock{this->mutex};
			this->connection = socket;
		}
		if (this->stopping) return;
		const auto token = parse_token(settings.token_hex);
		gamestate::player_id_t provisional_participant_id{2};
		if (settings.hosting) {
			auto room_id = create_relay_room(
				*socket, token, settings.room_name, settings.name);
			this->publish(RelayLobbyStatus{
				.phase = "waiting",
				.message = "Room #" + std::to_string(room_id) + " is advertised. Waiting for a guest...",
				.participants = settings.name + " (host)",
				.connected = true,
			});
			await_relay_room(*socket, room_id);
		}
		else {
			// The relay assigns an immutable provisional participant slot
			// (2..8) for this room channel. Keep it through StartMatch.
			provisional_participant_id =
				join_relay_room_slot(*socket, token, settings.room_id);
		}
		if (this->stopping) return;

		if (settings.hosting) {
			std::string guest_name{};
			auto accepted = host_handshake(
				*socket, 2, provisional_gameplay_digest(), &guest_name);
			if (not accepted) {
				throw Error{MSG(err) << "Relay guest rejected by handshake"};
			}
			HostLobby lobby{1, settings.name};
			lobby.join(2, guest_name);
			const auto roster = lobby.snapshot();
			socket->send_frame(accepted->make_frame(
				MessageType::PARTICIPANT_SNAPSHOT,
				encode_participant_snapshot(roster)));
			this->publish(RelayLobbyStatus{
				.phase = "lobby",
				.message = "Guest connected. Host may start the match.",
				.participants = settings.name + " (host)\n" + guest_name + " (guest)",
				.can_start = true,
				.connected = true,
			});
			while (not this->stopping and not this->start_requested) {
				std::this_thread::sleep_for(std::chrono::milliseconds{20});
			}
			if (this->stopping) return;
			auto assignment = lobby.start();
			validate_start_match_against_roster(roster, assignment);
			succession.emplace(assignment, assignment.host_participant_id);
			log_role_draw(assignment);
			socket->send_frame(accepted->make_frame(
				MessageType::START_MATCH, encode_start_match(assignment)));
			// The handshake's temporary slot 2 is not the role lottery result.
			// Preserve the accepted session ID and sequence numbers.
			accepted->bind_match_player(assignment, 2);
			const auto guest_player = accepted->get_identity().player_id;
			const auto host_player = player_for_participant(assignment, 1);
			const auto roles = describe_match_roles(roster, assignment);
			const auto host_role = local_role_name(assignment, host_player);
			{
				std::lock_guard lock{this->mutex};
				this->active_remote_identity = accepted->get_identity();
			}
			if (host_player != 1) {
				log::log(INFO << "[FVD/authority] handoff_requested original_creator_player="
				              << host_player << " hunter_player=1");
				this->publish(RelayLobbyStatus{
					.phase = "handoff",
					.message = "Builder lobby creator: transferring match authority to Hunter...",
					.participants = settings.name + " (host)\n" + guest_name + " (guest)",
					.connected = true,
					.roles = roles,
					.local_role = host_role,
				});
				std::map<gamestate::player_id_t,
					std::pair<ConnectionSession *, TcpConnection *>> peers{};
				peers.emplace(guest_player, std::make_pair(&*accepted, socket.get()));
				this->forward_rehost_offers(assignment, peers, settings);
				return;
			}
			log::log(INFO << "[FVD/authority] authority_active epoch=1 player=1"
			              << " participants=2 reason=creator_is_hunter");
			this->publish(RelayLobbyStatus{
				.phase = "assigned",
				.message = "Match start requested; synchronizing the initial world.",
				.participants = settings.name + " (host)\n" + guest_name + " (guest)",
				.connected = true,
				.local_player_id = host_player,
				.remote_player_id = guest_player,
				.match_player_count = 2,
				.roles = roles,
				.local_role = host_role,
			});
			while (not this->stopping) {
				std::optional<WorldState> snapshot;
				{
					std::lock_guard lock{this->mutex};
					if (this->pending_host_snapshot) {
						snapshot = std::move(this->pending_host_snapshot);
						this->pending_host_snapshot.reset();
					}
				}
				if (snapshot) {
					socket->send_frame(accepted->make_frame(
						MessageType::WORLD_SNAPSHOT, encode_world_state(*snapshot)));
					auto ready_frame = socket->receive_frame();
					if (not ready_frame) {
						throw Error{MSG(err) << "Guest disconnected before initial world acknowledgement"};
					}
					accepted->validate_incoming(*ready_frame);
					if (ready_frame->message_type != MessageType::MATCH_READY) {
						throw Error{MSG(err) << "Expected initial world acknowledgement"};
					}
					const auto ready = decode_match_ready(ready_frame->payload);
					if (ready.player_id != guest_player
					    or ready.snapshot_revision != snapshot->world_revision) {
						throw Error{MSG(err) << "Guest acknowledged the wrong player or world revision"};
					}
					this->publish(RelayLobbyStatus{
						.phase = "bootstrap",
						.message = "Initial world acknowledged. Waiting for live simulation captures.",
						.participants = settings.name + " (host)\n" + guest_name + " (guest)",
						.connected = true,
						.local_player_id = host_player,
						.remote_player_id = guest_player,
						.match_player_count = 2,
						.snapshot_revision = snapshot->world_revision,
						.roles = roles,
						.local_role = host_role,
					});
					// Start the delta baseline with the exact snapshot the
					// guest acknowledged, including recipient-private scores.
					WorldDeltaBaseline baseline;
					baseline.accept_snapshot(std::move(*snapshot));
					auto last_guest_message = std::chrono::steady_clock::now();
					while (not this->stopping) {
						std::optional<WorldState> next;
						{
							std::lock_guard lock{this->mutex};
							next = std::exchange(this->pending_host_update, std::nullopt);
						}
						if (next) {
							const auto revision = next->world_revision;
							auto delta = baseline.advance(std::move(*next));
							socket->send_frame(accepted->make_frame(
								MessageType::WORLD_DELTA, encode_world_delta(delta)));
							if (not delta.changed.arrows.empty()) {
								const auto last_id = delta.changed.arrows.back().arrow_id;
								std::lock_guard lock{this->mutex};
								this->delivered_projectiles[next->local_player_id] =
									std::max(this->delivered_projectiles[next->local_player_id], last_id);
							}
							this->publish_stream_revision(revision, true);
						}
						// Host owns session sequence numbers. Poll guest intents
						// on this same worker; never decode them on a render thread.
						for (size_t i = 0; i < 32; ++i) {
							auto frame = socket->try_receive_frame();
							if (not frame) break;
							accepted->validate_incoming(*frame);
							last_guest_message = std::chrono::steady_clock::now();
							if (frame->message_type == MessageType::PING) {
								if (not frame->payload.empty()) {
									throw Error{MSG(err) << "Invalid FVD gameplay heartbeat"};
								}
								socket->send_frame(accepted->make_frame(MessageType::PONG, {}));
								continue;
							}
							if (frame->message_type == MessageType::SNAPSHOT_REQUEST) {
								const auto request = decode_snapshot_request(frame->payload);
								const auto &latest = baseline.current_snapshot();
								if (request.last_applied_revision > latest.world_revision) {
									throw Error{MSG(err) << "Guest requests future FVD snapshot revision"};
								}
								socket->send_frame(accepted->make_frame(
									MessageType::WORLD_SNAPSHOT, encode_world_state(latest)));
								continue;
							}
							if (frame->message_type != MessageType::COMMAND_INTENT) {
								throw Error{MSG(err) << "Unexpected guest match message"};
							}
							AuthenticatedCommand incoming{
								accepted->get_identity(), decode_command_intent(frame->payload)};
							std::lock_guard lock{this->mutex};
							if (this->incoming_host_commands.size() >= MAX_INFLIGHT_COMMANDS) {
								throw Error{MSG(err) << "Guest command queue exceeded capacity"};
							}
							this->incoming_host_commands.push_back(std::move(incoming));
						}
						{
							std::deque<CommandResult> outgoing;
							{
								std::lock_guard lock{this->mutex};
								outgoing.swap(this->pending_command_results);
							}
							for (auto &result : outgoing) {
								socket->send_frame(accepted->make_frame(
									MessageType::COMMAND_RESULT, encode_command_result(result)));
							}
						}
						if (not socket->is_open() or not socket->peer_connected()) {
							throw Error{MSG(err) << "Relay guest disconnected during gameplay"};
						}
						if (std::chrono::steady_clock::now() - last_guest_message > LIVE_SESSION_TIMEOUT) {
							throw Error{MSG(err) << "Relay guest stopped responding (15s); match ended. Press F10 to reset."};
						}
						std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
					}
					break;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds{20});
			}
		}
		else {
			auto session = client_handshake(*socket, {
				settings.rehost_offer ? uint16_t{4}
				                      : PROTOCOL_MINIMUM_COMPATIBLE_MINOR,
				settings.name, "fvd-lobby", provisional_gameplay_digest()});
			if (settings.rehost_offer) {
				if (session.get_protocol_version().minor < 4) {
					throw Error{MSG(err) << "Hunter handoff requires protocol 7.4"};
				}
				socket->send_frame(session.make_frame(
					MessageType::AUTHORITY_REHOST_PROOF,
					encode_rehost_proof(make_rehost_proof(*settings.rehost_offer))));
			}
			auto frame = socket->receive_frame();
			if (not frame) {
				throw Error{MSG(err) << "Relay host closed before lobby snapshot"};
			}
			session.validate_incoming(*frame);
			if (frame->message_type != MessageType::PARTICIPANT_SNAPSHOT) {
				throw Error{MSG(err) << "Expected a relay lobby roster"};
			}
			const auto roster = decode_participant_snapshot(frame->payload);
			std::string people{};
			for (const auto &person : roster.participants) {
				if (not people.empty()) people += "\n";
				people += person.name + (person.is_host ? " (host)" : " (guest)");
			}
			this->publish(RelayLobbyStatus{
				.phase = "lobby",
				.message = "Connected. Waiting for host to start...",
				.participants = people,
				.connected = true,
			});
			// Earlier guests receive revised participant snapshots whenever
			// another guest joins. Only the last validated roster may be
			// used to authenticate the immutable StartMatch assignments.
			auto latest_roster = roster;
			while (not this->stopping) {
				frame = socket->receive_frame();
				if (not frame) {
					throw Error{MSG(err) << "Relay host closed before match start"};
				}
				session.validate_incoming(*frame);
				if (frame->message_type == MessageType::START_MATCH) break;
				if (frame->message_type != MessageType::PARTICIPANT_SNAPSHOT) {
					throw Error{MSG(err) << "Expected lobby roster update or StartMatch"};
				}
				auto revised = decode_participant_snapshot(frame->payload);
				if (revised.host_participant_id != latest_roster.host_participant_id
				    or revised.revision <= latest_roster.revision) {
					throw Error{MSG(err) << "Stale or mismatched FVD lobby roster"};
				}
				latest_roster = std::move(revised);
				people.clear();
				for (const auto &person : latest_roster.participants) {
					if (not people.empty()) people += "\n";
					people += person.name + (person.is_host ? " (host)" : " (guest)");
				}
				this->publish(RelayLobbyStatus{
					.phase = "lobby",
					.message = "Connected. Waiting for host to start...",
					.participants = people,
					.connected = true,
				});
			}
			if (this->stopping) return;
			const auto assigned = decode_start_match(frame->payload);
			validate_start_match_against_roster(latest_roster, assigned);
			succession.emplace(assigned, assigned.host_participant_id);
			session.bind_match_player(assigned, provisional_participant_id);
			const auto guest_player = session.get_identity().player_id;
			if (settings.rehost_offer
			    and (guest_player != settings.rehost_offer->original_player_id
			         or player_for_participant(assigned, assigned.host_participant_id) != 1)) {
				throw Error{MSG(err) << "Hunter replacement changed the original role assignment"};
			}
			const auto host_player = player_for_participant(assigned, 1);
			const auto roles = describe_match_roles(latest_roster, assigned);
			const auto guest_role = local_role_name(assigned, guest_player);
			if (host_player != 1) {
				this->publish(RelayLobbyStatus{
					.phase = "handoff",
					.message = "Hunter selected. Establishing independent authoritative room...",
					.participants = people,
					.connected = true,
					.roles = roles,
					.local_role = guest_role,
				});
				if (guest_player == 1) {
					this->begin_hunter_rehost(settings, assigned, session, *socket);
				}
				else {
					std::optional<Frame> invitation{};
					const auto deadline = std::chrono::steady_clock::now()
					                      + std::chrono::seconds{30};
					while (not this->stopping and std::chrono::steady_clock::now() < deadline) {
						invitation = socket->try_receive_frame();
						if (invitation) break;
						if (not socket->is_open() or not socket->peer_connected()) break;
						std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
					}
					if (this->stopping) return;
					if (not invitation) {
						throw Error{MSG(err) << "Host closed or timed out before Hunter invitation"};
					}
					session.validate_incoming(*invitation);
					if (invitation->message_type != MessageType::AUTHORITY_REHOST_OFFER) {
						throw Error{MSG(err) << "Expected Hunter authority redirect offer"};
					}
					const auto offer = decode_rehost_offer(invitation->payload);
					if (offer.original_player_id != guest_player
					    or offer.original_lobby_revision != assigned.lobby_revision
					    or offer.authority_epoch != 2) {
						throw Error{MSG(err) << "Hunter handoff offer did not match original identity"};
					}
					this->run_rehosted_guest(settings, offer);
				}
				return;
			}
			this->publish(RelayLobbyStatus{
				.phase = "assigned",
				.message = "Match start requested; waiting for authoritative initial world.",
				.participants = people,
				.connected = true,
				.local_player_id = guest_player,
				.remote_player_id = host_player,
				.match_player_count = 2,
				.roles = roles,
				.local_role = guest_role,
			});
			frame = socket->receive_frame();
			if (not frame) {
				throw Error{MSG(err) << "Host disconnected before initial world"};
			}
			session.validate_incoming(*frame);
			if (frame->message_type != MessageType::WORLD_SNAPSHOT) {
				throw Error{MSG(err) << "Expected an authoritative world snapshot"};
			}
			auto tracker = std::make_shared<ReplicationTracker>(guest_player);
			tracker->accept_snapshot(decode_world_state(frame->payload));
			const auto revision = tracker->get_revision();
			socket->send_frame(session.make_frame(
				MessageType::MATCH_READY,
				encode_match_ready({guest_player, revision})));
			{
				std::lock_guard lock{this->mutex};
				this->guest_replication = tracker;
			}
			this->publish(RelayLobbyStatus{
				.phase = "bootstrap",
				.message = "Authoritative world synchronized; waiting for role reveal.",
				.participants = people,
				.connected = true,
				.local_player_id = guest_player,
				.remote_player_id = host_player,
				.match_player_count = 2,
				.snapshot_revision = revision,
				.roles = roles,
				.local_role = guest_role,
			});
			auto last_host_message = std::chrono::steady_clock::now();
			auto last_ping_sent = last_host_message - LIVE_HEARTBEAT_INTERVAL;
			while (not this->stopping) {
				// Keep an otherwise idle guest session observable by the host.
				// A transient delay does not change the authenticated session.
				const auto now = std::chrono::steady_clock::now();
				if (now - last_ping_sent >= LIVE_HEARTBEAT_INTERVAL) {
					socket->send_frame(session.make_frame(MessageType::PING, {}));
					last_ping_sent = now;
				}
				// Queue client intents only through the authenticated session.
				// This worker alone allocates outgoing sequence numbers.
				bool snapshot_request = false;
				{
					std::lock_guard lock{this->mutex};
					snapshot_request = std::exchange(this->guest_snapshot_requested, false);
				}
				if (snapshot_request) {
					socket->send_frame(session.make_frame(
						MessageType::SNAPSHOT_REQUEST,
						encode_snapshot_request(tracker->make_snapshot_request())));
				}
				{
					std::deque<CommandIntent> outbound;
					{
						std::lock_guard lock{this->mutex};
						outbound.swap(this->pending_guest_commands);
					}
					for (auto &intent : outbound) {
						socket->send_frame(session.make_frame(
							MessageType::COMMAND_INTENT, encode_command_intent(intent)));
					}
				}
				auto update_frame = socket->try_receive_frame();
				if (not update_frame) {
					if (not socket->is_open() or not socket->peer_connected()) {
						if (this->stopping) break;
						throw Error{MSG(err) << "Authoritative host disconnected during gameplay"};
					}
					if (std::chrono::steady_clock::now() - last_host_message > LIVE_SESSION_TIMEOUT) {
						throw Error{MSG(err) << "Authoritative host stopped responding (15s); match ended. Press F10 to reset."};
					}
					std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
					continue;
				}
				session.validate_incoming(*update_frame);
				last_host_message = std::chrono::steady_clock::now();
				if (update_frame->message_type == MessageType::PONG) {
					if (not update_frame->payload.empty()) {
						throw Error{MSG(err) << "Invalid FVD gameplay heartbeat acknowledgement"};
					}
					{
						std::lock_guard lock{this->mutex};
						++this->current.heartbeat_acknowledgements;
					}
					continue;
				}
				if (update_frame->message_type == MessageType::COMMAND_RESULT) {
					auto result = decode_command_result(update_frame->payload);
					std::lock_guard lock{this->mutex};
					auto outstanding = this->guest_outstanding_commands.find(result.command_id);
					if (outstanding == this->guest_outstanding_commands.end()) {
						throw Error{MSG(err) << "Unrecognized or duplicate FVD command result"};
					}
					const auto kind = outstanding->second;
					this->guest_outstanding_commands.erase(outstanding);
					this->current.pending_order_count = this->guest_outstanding_commands.size();
					this->current.order_feedback = std::string{order_name(kind)}
						+ (result.status == CommandStatus::ACCEPTED ? " confirmed" : " rejected");
					if (result.status == CommandStatus::REJECTED and not result.reason.empty()) {
						this->current.order_feedback += ": " + result.reason;
					}
					if (this->guest_command_results.size() >= MAX_INFLIGHT_COMMANDS) {
						this->guest_command_results.pop_front();
					}
					this->guest_command_results.push_back(std::move(result));
					continue;
				}
				if (update_frame->message_type == MessageType::PARTICIPANT_DISCONNECTED) {
					const auto departed = decode_participant_disconnected(update_frame->payload);
					if (departed.player_id == guest_player) {
						throw Error{MSG(err) << "FVD host sent a self-disconnect notice to the guest"};
					}
					if (departed.player_id == 1) {
						// Hunter loss ends the game. The connection cannot
						// fall back to a locally simulated divergent world.
						throw Error{MSG(err) << "Hunter disconnected; match ended"};
					}
					{
						std::lock_guard lock{this->mutex};
						this->current.departure_notice = "Builder player "
							+ std::to_string(departed.player_id)
							+ " disconnected; match continues.";
					}
					continue; // The next authoritative delta carries the persistent flag.
				}
				if (update_frame->message_type == MessageType::WORLD_DELTA) {
					auto delta = decode_world_delta(update_frame->payload);
					const auto disposition = tracker->accept_delta(delta);
					if (disposition == DeltaDisposition::REQUEST_SNAPSHOT) {
						socket->send_frame(session.make_frame(
							MessageType::SNAPSHOT_REQUEST,
							encode_snapshot_request(tracker->make_snapshot_request())));
					}
					if (disposition != DeltaDisposition::APPLY) {
						// Ignore further stale deltas until the full snapshot
						// reseeds the guest's validated world.
						continue;
					}
				}
				else if (update_frame->message_type == MessageType::WORLD_SNAPSHOT) {
					tracker->accept_snapshot(decode_world_state(update_frame->payload));
					std::lock_guard lock{this->mutex};
					++this->current.snapshot_recoveries;
				}
				else {
					throw Error{MSG(err) << "Unexpected FVD message "
					                     << static_cast<uint16_t>(update_frame->message_type)
					                     << " during world replication"};
				}
				this->publish_stream_revision(tracker->get_revision(), false);
			}
		}
	}
	catch (const std::exception &error) {
		std::shared_ptr<TcpConnection> socket;
		{
			std::lock_guard lock{this->mutex};
			socket = this->connection;
		}
		if (socket) {
			socket->close();
		}
		if (not this->stopping) {
			log::log(WARN << "[FVD/match] session_error context=two_peer"
			              << " reason=" << error.what());
			std::shared_ptr<ReplicationTracker> replication;
			bool builder_departure = false;
			{
				std::lock_guard lock{this->mutex};
				builder_departure = settings.hosting
				                    and host_can_continue_after_guest_loss(this->current);
				if (builder_departure) {
					// A Builder leaving must not stop the authoritative Hunter.
					// Reject any queued, not-yet-submitted Builder commands and
					// stop producing snapshots for this closed connection.
					this->incoming_host_commands.clear();
					this->pending_command_results.clear();
					this->active_remote_identity.reset();
					this->pending_host_update.reset();
					this->current.connected = false;
					this->current.can_start = false;
					this->current.remote_player_id = 0;
					this->current.message =
						"Builder disconnected; the Hunter match continues. Press F10 to leave.";
					// Preserve bootstrap/streaming so status() still ends the
					// role reveal at its original time without a second session.
				}
				replication = this->guest_replication;
			}
			if (builder_departure) {
				log::log(INFO << "[FVD/match] participant_disconnected player=2"
				              << " role=Builder action=continue participants_remaining=1");
			}
			if (not builder_departure) {
				if (replication) {
					replication->host_disconnected();
				}
				std::string reason = std::string{"Match connection closed: "} + error.what();
				// Explicitly distinguish a Builder *authority* loss from a
				// Builder guest leaving. A Hunter guest is the rightful
				// successor, but cannot run an imaginary reconstruction from
				// the presentation-only WorldState.
				const auto prior = this->status();
				if (not settings.hosting and succession
				    and succession->host_is_builder()
				    and prior.local_role == "HUNTER"
				    and (prior.phase == "bootstrap" or prior.phase == "streaming"
				         or prior.phase == "playing")) {
					reason += ". You are the Hunter and designated successor, "
					          "but there is no restorable simulation checkpoint "
					          "or surviving peer transport. Match ended safely";
				}
				this->publish(RelayLobbyStatus{
					.phase = "error",
					.message = std::move(reason)
					         + ". Press F10 to reset before hosting or joining a new match.",
				});
			}
		}
	}
}

void RelayLobbyClient::run_multi_host(RelayLobbyConfig settings) {
	try {
		auto lobby = settings.prepared_rehost_lobby
			? settings.prepared_rehost_lobby
			: std::make_shared<RelayMultiHostLobby>(
				settings.address, settings.port, parse_token(settings.token_hex),
				settings.room_name, settings.name, settings.player_capacity);
		{
			std::lock_guard lock{this->mutex};
			this->multi_host_lobby = lobby;
		}
		auto participants_text = [&]() {
			std::string text{};
			for (const auto &person : lobby->roster().participants) {
				if (not text.empty()) text += "\n";
				text += person.name + (person.is_host ? " (host)" : " (guest)");
			}
			return text;
		};
		this->publish(RelayLobbyStatus{
			.phase = "waiting",
			.message = "Room #" + std::to_string(lobby->room_id())
			           + " accepts up to " + std::to_string(settings.player_capacity)
			           + " players. Waiting for Builders or Hunter...",
			.participants = participants_text(),
			.connected = true,
		});
		size_t previous_count = 1;
		while (not this->stopping) {
			lobby->poll_admissions(provisional_gameplay_digest());
			const auto count = lobby->participant_count();
			if (count != previous_count
			    or (count > 1 and this->status().phase == "waiting")) {
				previous_count = count;
				this->publish(RelayLobbyStatus{
					.phase = "lobby",
					.message = "Guests joined. Start when everyone is ready ("
					           + std::to_string(lobby->participant_count()) + "/"
					           + std::to_string(settings.player_capacity) + ").",
					.participants = participants_text(),
					.can_start = count >= 2,
					.connected = true,
				});
			}
			if ((this->start_requested and lobby->participant_count() >= 2
			     and not settings.rehost_start)
			    or (settings.rehost_start
			        and lobby->participant_count() == settings.rehost_start->assignments.size())) break;
			std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
		}
		if (this->stopping) return;
		// HostLobby::start() increments its roster revision. The assignments
		// must be checked against the LAST PRE-START roster (revision N),
		// not the post-start roster (N+1), exactly as in the two-peer path.
		// Otherwise describe_match_roles() throws and closes all guest sockets
		// before they receive a WORLD_SNAPSHOT.
		const auto final_roster = lobby->roster();
		const auto assignment = settings.rehost_start
			? lobby->start_rehost() : lobby->start();
		validate_start_match_against_roster(final_roster, assignment);
		if (not settings.rehost_start) log_role_draw(assignment);
		AuthoritySuccession authority{assignment, assignment.host_participant_id};
		const auto host_player = player_for_participant(assignment, 1);
		const auto role = local_role_name(assignment, host_player);
		const auto role_text = describe_match_roles(final_roster, assignment);
		std::vector<gamestate::player_id_t> remotes{};
		std::map<gamestate::player_id_t, RelayMultiHostLobby::Peer const *> peers{};
		for (const auto &peer : lobby->channels()) {
			if (not peer.admitted) continue;
			const auto identity = peer.session->get_identity();
			remotes.push_back(identity.player_id);
			peers.emplace(identity.player_id, &peer);
		}
		if (host_player != 1) {
			log::log(INFO << "[FVD/authority] handoff_requested original_creator_player="
			              << host_player << " hunter_player=1 participants="
			              << assignment.assignments.size());
			this->publish(RelayLobbyStatus{
				.phase = "handoff",
				.message = "Original Builder host: delivering Hunter authority invitations...",
				.participants = participants_text(),
				.connected = true,
				.roles = role_text,
				.local_role = role,
			});
			std::map<gamestate::player_id_t,
				std::pair<ConnectionSession *, TcpConnection *>> original_peers{};
			for (const auto &[id, peer] : peers) {
				original_peers.emplace(id, std::make_pair(
					peer->session.get(), peer->socket.get()));
			}
			this->forward_rehost_offers(assignment, original_peers, settings);
			return;
		}
		log::log(INFO << "[FVD/authority] authority_active epoch="
		              << (settings.rehost_start ? 2 : 1)
		              << " player=1 participants=" << assignment.assignments.size()
		              << " room=" << lobby->room_id());
		{
			std::lock_guard lock{this->mutex};
			for (const auto &[id, peer] : peers) {
				this->active_multi_identities.emplace(id, peer->session->get_identity());
			}
		}
		this->publish(RelayLobbyStatus{
			.phase = "assigned",
			.message = "Starting " + std::to_string(assignment.assignments.size())
			           + "-player match; synchronizing each guest...",
			.participants = participants_text(),
			.connected = true,
			.local_player_id = host_player,
			.remote_player_id = remotes.front(),
			.remote_player_ids = remotes,
			.match_player_count = assignment.assignments.size(),
			.multi_host = true,
			.roles = role_text,
			.local_role = role,
		});

		std::map<gamestate::player_id_t, WorldDeltaBaseline> baselines{};
		std::map<gamestate::player_id_t, std::chrono::steady_clock::time_point> last_seen{};
		std::map<gamestate::player_id_t, std::chrono::steady_clock::time_point> bootstrap_sent{};
		std::map<gamestate::player_id_t, bool> ready{};
		std::map<gamestate::player_id_t, bool> dropped_during_bootstrap{};
		for (const auto &[id, peer] : peers) {
			ready[id] = false;
			dropped_during_bootstrap[id] = false;
		}
		auto drop_before_ready = [&](gamestate::player_id_t id) {
			if (id == 1) throw Error{MSG(err) << "Hunter disconnected before match bootstrap"};
			if (dropped_during_bootstrap[id]) return;
			dropped_during_bootstrap[id] = true;
			peers.at(id)->socket->close();
			std::lock_guard lock{this->mutex};
			this->active_multi_identities.erase(id);
			this->pending_multi_snapshots.erase(id);
			this->pending_multi_updates.erase(id);
			this->pending_multi_results.erase(id);
			auto &queued = this->incoming_host_commands;
			queued.erase(std::remove_if(queued.begin(), queued.end(),
				[id](const AuthenticatedCommand &command) {
					return command.identity.player_id == id;
				}), queued.end());
			auto &ids = this->current.remote_player_ids;
			ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
			this->current.disconnected_players.push_back(id);
			this->current.remote_player_id = ids.empty() ? 0 : ids.front();
			this->current.departure_notice =
				"Builder player " + std::to_string(id) + " left during synchronization.";
		};
		// Bootstrap is recipient-private and must be acknowledged separately.
		while (not this->stopping) {
			bool all_ready = true;
			for (const auto &[id, peer] : peers) {
				if (dropped_during_bootstrap[id]) continue;
				if (ready.at(id)) {
					// Faster guests can finish the handshake well before
					// slower ones. Keep their authenticated sessions alive
					// and queue any post-reveal input until the host clock
					// starts, rather than ignoring their heartbeats.
					try {
						for (size_t i = 0; i < 16; ++i) {
							auto frame = peer->socket->try_receive_frame();
							if (not frame) break;
							peer->session->validate_incoming(*frame);
							last_seen[id] = std::chrono::steady_clock::now();
							if (frame->message_type == MessageType::PING
							    and frame->payload.empty()) {
								peer->socket->send_frame(peer->session->make_frame(
									MessageType::PONG, {}));
							}
							else if (frame->message_type == MessageType::COMMAND_INTENT) {
								auto intent = decode_command_intent(frame->payload);
								std::lock_guard lock{this->mutex};
								if (this->incoming_host_commands.size() >= MAX_INFLIGHT_COMMANDS) {
									throw Error{MSG(err) << "Pre-start guest command queue full"};
								}
								this->incoming_host_commands.push_back({
									peer->session->get_identity(), std::move(intent)});
							}
							else if (frame->message_type == MessageType::SNAPSHOT_REQUEST) {
								auto request = decode_snapshot_request(frame->payload);
								const auto &initial = baselines.at(id).current_snapshot();
								if (request.last_applied_revision > initial.world_revision) {
									throw Error{MSG(err) << "Future bootstrap revision requested"};
								}
								peer->socket->send_frame(peer->session->make_frame(
									MessageType::WORLD_SNAPSHOT, encode_world_state(initial)));
							}
							else throw Error{MSG(err) << "Unexpected post-bootstrap guest frame"};
						}
						if (not peer->socket->is_open() or not peer->socket->peer_connected()
						    or std::chrono::steady_clock::now() - last_seen.at(id)
						       > LIVE_SESSION_TIMEOUT) {
							drop_before_ready(id);
						}
					}
					catch (const std::exception &) {
						drop_before_ready(id);
					}
					continue;
				}
				all_ready = false;
				if (not peer->socket->is_open() or not peer->socket->peer_connected()) {
					drop_before_ready(id);
					continue;
				}
				std::optional<WorldState> snapshot{};
				{
					std::lock_guard lock{this->mutex};
					auto item = this->pending_multi_snapshots.find(id);
					if (item != this->pending_multi_snapshots.end()) {
						snapshot = std::move(item->second);
						this->pending_multi_snapshots.erase(item);
					}
				}
				if (snapshot) {
					if (snapshot->local_player_id != id or snapshot->world_revision != 1) {
						throw Error{MSG(err) << "Wrong recipient/revision for FVD multi-peer bootstrap"};
					}
					peer->socket->send_frame(peer->session->make_frame(
						MessageType::WORLD_SNAPSHOT, encode_world_state(*snapshot)));
					baselines[id].accept_snapshot(std::move(*snapshot));
					bootstrap_sent[id] = std::chrono::steady_clock::now();
					last_seen[id] = bootstrap_sent[id];
				}
				if (not bootstrap_sent.contains(id)) continue;
				auto frame = peer->socket->try_receive_frame();
				if (frame) {
					peer->session->validate_incoming(*frame);
					last_seen[id] = std::chrono::steady_clock::now();
					if (frame->message_type != MessageType::MATCH_READY) {
						throw Error{MSG(err) << "FVD guest sent a command before world acknowledgement"};
					}
					auto receipt = decode_match_ready(frame->payload);
					if (receipt.player_id != id or
					    receipt.snapshot_revision != baselines[id].current_snapshot().world_revision) {
						throw Error{MSG(err) << "FVD guest acknowledged another player's world"};
					}
					ready[id] = true;
				}
				else if (not peer->socket->is_open() or not peer->socket->peer_connected()
				         or std::chrono::steady_clock::now() - bootstrap_sent[id]
				            > LIVE_SESSION_TIMEOUT) {
					drop_before_ready(id);
				}
			}
			if (all_ready) break;
			std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
		}
		if (this->stopping) return;
		const auto acknowledged_builders =
			std::count_if(ready.begin(), ready.end(), [](const auto &item) {
				return item.second;
			});
		if (acknowledged_builders == 0) {
			throw Error{MSG(err) << "Hunter bootstrap failed: no Builder acknowledged revision 1"};
		}
		{
			// Publish the acknowledged bootstrap revision at the same time
			// as the phase transition. The time-zero Hunter rehost follows
			// this path even with just one Builder; leaving the default
			// revision at zero makes the presenter report an uninitialized
			// authoritative world until a later delta happens to arrive.
			std::lock_guard lock{this->mutex};
			this->current.phase = "bootstrap";
			this->current.message = "All players synchronized. Waiting for role reveal.";
			this->current.snapshot_revision = 1;
		}
		log::log(INFO << "[FVD/authority] bootstrap_complete room=" << lobby->room_id()
		              << " hunter_player=1 revision=1"
		              << " acknowledged_builders=" << acknowledged_builders);

		std::map<gamestate::player_id_t, bool> live{};
		for (const auto &[id, peer] : peers) {
			live[id] = not dropped_during_bootstrap[id];
		}
		// A Builder who failed during the initial handshake is still a
		// fixed, disconnected match participant. Tell all surviving peers
		// before any streamed world updates change their player flags.
		for (const auto &[departed, was_dropped] : dropped_during_bootstrap) {
			if (not was_dropped) continue;
			for (const auto &[other, peer] : peers) {
				if (not live[other]) continue;
				peer->socket->send_frame(peer->session->make_frame(
					MessageType::PARTICIPANT_DISCONNECTED,
					encode_participant_disconnected({
						departed, DisconnectReason::CONNECTION_LOST})));
			}
		}
		// The coordinator never sends on an active Builder socket. Each
		// recipient's streaming worker owns its ConnectionSession and TCP
		// reads/writes, so a backpressured send_frame() can block only that
		// recipient. Bootstrap above deliberately remains synchronous.
		std::map<gamestate::player_id_t,
		         std::deque<gamestate::player_id_t>> pending_departure_notices{};
		std::mutex failures_mutex{};
		std::deque<std::pair<gamestate::player_id_t, std::string>> failures{};
		std::atomic<bool> stop_peers{false};
		std::vector<std::thread> peer_workers{};
		auto disconnect_peer = [&](gamestate::player_id_t player) {
			if (not live[player]) return AuthorityLossAction::CONTINUE;
			const auto consequence = authority.disconnected(player);
			log::log(INFO << "[FVD/match] participant_disconnected player=" << player
			              << " role=" << (player == 1 ? "Hunter" : "Builder")
			              << " action=" << (consequence == AuthorityLossAction::CONTINUE
			                                     ? "continue" : "end_match")
			              << " room=" << lobby->room_id());
			live[player] = false;
			// The failed worker is exiting; nobody else accesses this socket.
			peers.at(player)->socket->close();
			{
				std::lock_guard lock{this->mutex};
				this->active_multi_identities.erase(player);
				this->pending_multi_results.erase(player);
				this->pending_multi_updates.erase(player);
				this->pending_multi_snapshots.erase(player);
				pending_departure_notices.erase(player);
				auto &ids = this->current.remote_player_ids;
				ids.erase(std::remove(ids.begin(), ids.end(), player), ids.end());
				auto &commands = this->incoming_host_commands;
				commands.erase(std::remove_if(commands.begin(), commands.end(),
					[player](const AuthenticatedCommand &cmd) {
						return cmd.identity.player_id == player;
					}), commands.end());
				this->current.disconnected_players.push_back(player);
				if (this->current.remote_player_id == player) {
					this->current.remote_player_id = ids.empty() ? 0 : ids.front();
				}
				this->current.departure_notice = player == 1
					? "Hunter disconnected; match ended."
					: "Builder player " + std::to_string(player)
					  + " disconnected; match continues.";
				// No direct TCP sends from this coordinator: a blocked
				// recipient cannot delay notices to healthy recipients.
				for (const auto &[other, peer] : peers) {
					(void) peer;
					if (live[other]) pending_departure_notices[other].push_back(player);
				}
			}
			return consequence;
		};
		auto stop_peer_workers = [&]() {
			stop_peers = true;
			// shutdown() interrupts a concurrent send_frame() before join.
			for (const auto &[id, peer] : peers) {
				(void) id;
				peer->socket->close();
			}
			for (auto &worker : peer_workers) {
				if (worker.joinable()) worker.join();
			}
		};
		try {
			for (const auto &[id, peer] : peers) {
				if (not live.at(id)) continue;
				// Baselines and heartbeat clocks belong exclusively to the
				// recipient worker after its bootstrap handoff.
				peer_workers.emplace_back(
					[&, id, peer, baseline = std::move(baselines.at(id)),
					 seen = last_seen.at(id)]() mutable {
						auto &socket = *peer->socket;
						auto &session = *peer->session;
						try {
							while (not this->stopping and not stop_peers) {
								std::optional<WorldState> update{};
								std::deque<CommandResult> decisions{};
								std::deque<gamestate::player_id_t> notices{};
								{
									std::lock_guard lock{this->mutex};
									auto found = this->pending_multi_updates.find(id);
									if (found != this->pending_multi_updates.end()) {
										update = std::move(found->second);
										this->pending_multi_updates.erase(found);
									}
									auto results = this->pending_multi_results.find(id);
									if (results != this->pending_multi_results.end()) {
										decisions.swap(results->second);
										this->pending_multi_results.erase(results);
									}
									auto departures = pending_departure_notices.find(id);
									if (departures != pending_departure_notices.end()) {
										notices.swap(departures->second);
										pending_departure_notices.erase(departures);
									}
								}
								// Prioritize small, reliable protocol events.
								// Only this worker assigns this session's
								// outgoing sequence numbers, preserving order.
								for (const auto departed : notices) {
									socket.send_frame(session.make_frame(
										MessageType::PARTICIPANT_DISCONNECTED,
										encode_participant_disconnected({
											departed, DisconnectReason::CONNECTION_LOST})));
								}
								for (auto &decision : decisions) {
									socket.send_frame(session.make_frame(
										MessageType::COMMAND_RESULT,
										encode_command_result(decision)));
								}
								if (update) {
									auto delta = baseline.advance(std::move(*update));
									socket.send_frame(session.make_frame(
										MessageType::WORLD_DELTA,
										encode_world_delta(delta)));
									// The projectile cursor advances only
									// after that recipient's frame is sent.
									if (not delta.changed.arrows.empty()) {
										const auto last_id = delta.changed.arrows.back().arrow_id;
										std::lock_guard lock{this->mutex};
										this->delivered_projectiles[id] =
											std::max(this->delivered_projectiles[id], last_id);
									}
									this->publish_stream_revision(
										baseline.current_snapshot().world_revision, true);
								}
								for (size_t i = 0; i < 32; ++i) {
									auto frame = socket.try_receive_frame();
									if (not frame) break;
									session.validate_incoming(*frame);
									seen = std::chrono::steady_clock::now();
									if (frame->message_type == MessageType::PING) {
										if (not frame->payload.empty()) {
											throw Error{MSG(err) << "Invalid multi-guest heartbeat"};
										}
										socket.send_frame(session.make_frame(MessageType::PONG, {}));
										continue;
									}
									if (frame->message_type == MessageType::SNAPSHOT_REQUEST) {
										auto request = decode_snapshot_request(frame->payload);
										const auto &latest = baseline.current_snapshot();
										if (request.last_applied_revision > latest.world_revision) {
											throw Error{MSG(err) << "Future multi-guest snapshot revision requested"};
										}
										socket.send_frame(session.make_frame(
											MessageType::WORLD_SNAPSHOT,
											encode_world_state(latest)));
										continue;
									}
									if (frame->message_type != MessageType::COMMAND_INTENT) {
										throw Error{MSG(err) << "Unexpected multi-guest match frame"};
									}
									auto intent = decode_command_intent(frame->payload);
									std::lock_guard lock{this->mutex};
									if (this->incoming_host_commands.size() >= MAX_INFLIGHT_COMMANDS) {
										throw Error{MSG(err) << "Multi-guest command mailbox full"};
									}
									this->incoming_host_commands.push_back({
										session.get_identity(), std::move(intent)});
								}
								if (not socket.is_open() or not socket.peer_connected()
								    or std::chrono::steady_clock::now() - seen > LIVE_SESSION_TIMEOUT) {
									throw Error{MSG(err) << "Guest transport ended"};
								}
								std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
							}
						}
						catch (const std::exception &error) {
							if (not this->stopping and not stop_peers) {
								std::lock_guard lock{failures_mutex};
								failures.emplace_back(id, error.what());
							}
						}
					});
			}
			// The coordinator handles departures and authoritative status
			// changes, but never enters a recipient's blocking send path.
			while (not this->stopping) {
				std::deque<std::pair<gamestate::player_id_t, std::string>> departed{};
				{
					std::lock_guard lock{failures_mutex};
					departed.swap(failures);
				}
				for (const auto &[id, reason] : departed) {
					if (not live.at(id)) continue;
					log::log(WARN << "[FVD/match] recipient_transport_ended player="
					              << id << " reason=" << reason);
					auto consequence = disconnect_peer(id);
					if (consequence == AuthorityLossAction::END_MATCH) {
						throw Error{MSG(err) << "Hunter disconnected; match ended"};
					}
					if (consequence == AuthorityLossAction::REQUIRE_RESTORED_CHECKPOINT) {
						throw Error{MSG(err) << "Authoritative Builder lost; "
						                     << "takeover requires a complete simulation checkpoint"};
					}
				}
				std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
			}
		}
		catch (...) {
			stop_peer_workers();
			throw;
		}
		stop_peer_workers();
	}
	catch (const std::exception &error) {
		if (not this->stopping) {
			log::log(WARN << "[FVD/match] session_error context=multi_host"
			              << " reason=" << error.what());
		}
		std::shared_ptr<RelayMultiHostLobby> active{};
		{
			std::lock_guard lock{this->mutex};
			active = this->multi_host_lobby;
			this->active_multi_identities.clear();
			this->incoming_host_commands.clear();
			this->pending_multi_results.clear();
			this->pending_multi_updates.clear();
		}
		if (active) active->close(); // Unblock every guest immediately on Hunter loss.
		if (not this->stopping) {
			this->publish(RelayLobbyStatus{
				.phase = "error",
				.message = std::string{"Multi-player match ended: "} + error.what()
				           + ". Press F10 to reset.",
			});
		}
	}
}

void RelayLobbyClient::run_rehosted_guest(
	RelayLobbyConfig settings, const RehostOffer &offer) {
	if (this->stopping) return;
	const bool was_lobby_creator = settings.hosting;
	if (offer.authority_epoch != 2 or offer.room_id == 0) {
		throw Error{MSG(err) << "Invalid Hunter authority redirect"};
	}
	auto child = std::make_shared<RelayLobbyClient>();
	settings.hosting = false;
	settings.player_capacity = 2; // Guests never create an authority room.
	settings.room_id = offer.room_id;
	settings.rehost_start.reset();
	settings.prepared_rehost_lobby.reset();
	settings.rehost_offer = offer;
	child->connect(std::move(settings));
	{
		std::lock_guard lock{this->mutex};
		if (this->stopping or this->handoff_client) {
			child->close();
			return;
		}
		this->handoff_client = child;
	}
	// The old authenticated sessions stay alive until the replacement has
	// completed StartMatch. Losing the former Builder host after this point
	// cannot affect the Hunter's independent relay room.
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{45};
	while (not this->stopping) {
		if (std::chrono::steady_clock::now() >= deadline) {
			throw Error{MSG(err) << "Hunter replacement did not start within 45 seconds"};
		}
		const auto status = child->status();
		if (status.phase == "assigned" or status.phase == "bootstrap"
		    or status.phase == "streaming" or status.phase == "playing") {
			log::log(INFO << "[FVD/authority] builder_rejoined epoch=2 room="
			              << offer.room_id << " player=" << status.local_player_id
			              << " former_lobby_creator=" << (was_lobby_creator ? "yes" : "no"));
			return;
		}
		if (status.phase == "error") {
			throw Error{MSG(err) << "Hunter rehost failed: " << status.message};
		}
		std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
	}
}

void RelayLobbyClient::begin_hunter_rehost(
	RelayLobbyConfig settings, const StartMatch &assignment,
	ConnectionSession &old_session, TcpConnection &old_socket) {
	if (this->stopping) return;
	const auto original_player = old_session.get_identity().player_id;
	if (original_player != 1
	    or player_for_participant(assignment, assignment.host_participant_id) == 1) {
		throw Error{MSG(err) << "Only the remote Hunter can initiate authority transfer"};
	}
	auto room = std::make_shared<RelayMultiHostLobby>(
		settings.address, settings.port, parse_token(settings.token_hex),
		settings.room_name + " (Hunter)", settings.name,
		assignment.assignments.size());
	room->prepare_hunter_rehost(assignment);
	log::log(INFO << "[FVD/authority] hunter_rehost_created epoch=2 room="
	              << room->room_id() << " expected_builders="
	              << assignment.assignments.size() - 1);
	auto child = std::make_shared<RelayLobbyClient>();
	settings.hosting = true;
	settings.room_id = 0;
	settings.player_capacity = assignment.assignments.size();
	settings.rehost_offer.reset();
	settings.rehost_start = assignment;
	settings.prepared_rehost_lobby = room;
	child->connect(std::move(settings));
	{
		std::lock_guard lock{this->mutex};
		if (this->stopping or this->handoff_client) {
			child->close();
			return;
		}
		this->handoff_client = child;
	}
	// The old Builder host forwards each private invitation on the original
	// authenticated participant session. Each Builder subsequently proves
	// their original slot to the Hunter's NEW accepted session.
	for (const auto &slot : assignment.assignments) {
		if (slot.player_id == 1) continue;
		old_socket.send_frame(old_session.make_frame(
			MessageType::AUTHORITY_REHOST_OFFER,
			encode_rehost_offer(room->rehost_offer_for(slot.player_id))));
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{45};
	while (not this->stopping) {
		if (std::chrono::steady_clock::now() >= deadline) {
			throw Error{MSG(err) << "Other Builders did not complete Hunter handoff within 45 seconds"};
		}
		auto status = child->status();
		if (status.phase == "assigned" or status.phase == "bootstrap"
		    or status.phase == "streaming" or status.phase == "playing") return;
		if (status.phase == "error") {
			throw Error{MSG(err) << "Hunter authority room failed: " << status.message};
		}
		std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
	}
}

void RelayLobbyClient::forward_rehost_offers(
	const StartMatch &assignment,
	const std::map<gamestate::player_id_t,
	               std::pair<ConnectionSession *, TcpConnection *>> &old_peers,
	const RelayLobbyConfig &settings) {
	if (this->stopping) return;
	const auto old_host_player =
		player_for_participant(assignment, assignment.host_participant_id);
	if (old_host_player == 1 or not old_peers.contains(1)) {
		throw Error{MSG(err) << "Hunter rehost needs the original Builder host and Hunter channel"};
	}
	const auto &[hunter_session, hunter_socket] = old_peers.at(1);
	std::map<gamestate::player_id_t, RehostOffer> invitations{};
	const auto expires = std::chrono::steady_clock::now() + std::chrono::seconds{30};
	while (not this->stopping and invitations.size() + 1 < assignment.assignments.size()) {
		auto frame = hunter_socket->try_receive_frame();
		if (frame) {
			hunter_session->validate_incoming(*frame);
			if (frame->message_type != MessageType::AUTHORITY_REHOST_OFFER) {
				throw Error{MSG(err) << "Expected the Hunter's private authority handoff offer"};
			}
			auto offer = decode_rehost_offer(frame->payload);
			if (offer.original_lobby_revision != assignment.lobby_revision
			    or offer.original_player_id == 1 or offer.authority_epoch != 2
			    or (offer.original_player_id != old_host_player
			        and not old_peers.contains(offer.original_player_id))
			    or invitations.contains(offer.original_player_id)
			    or (not invitations.empty()
			        and invitations.begin()->second.room_id != offer.room_id)) {
				throw Error{MSG(err) << "Invalid or duplicate Hunter authority invitation"};
			}
			invitations.emplace(offer.original_player_id, offer);
			log::log(INFO << "[FVD/authority] handoff_invitation_forwarded epoch=2"
			              << " room=" << offer.room_id
			              << " builder_player=" << offer.original_player_id);
			if (offer.original_player_id != old_host_player) {
				auto &[session, socket] = old_peers.at(offer.original_player_id);
				socket->send_frame(session->make_frame(
					MessageType::AUTHORITY_REHOST_OFFER, encode_rehost_offer(offer)));
			}
		}
		else if (not hunter_socket->peer_connected()
		         or std::chrono::steady_clock::now() >= expires) {
			throw Error{MSG(err) << "Hunter authority redirection timed out"};
		}
		else std::this_thread::sleep_for(LIVE_POLL_INTERVAL);
	}
	if (this->stopping) return;
	this->run_rehosted_guest(settings, invitations.at(old_host_player));
}

void RelayLobbyClient::run_refresh(RelayLobbyConfig settings) {
	try {
		auto socket = std::make_shared<TcpConnection>(
			TcpConnection::connect(settings.address, settings.port));
		{
			std::lock_guard lock{this->mutex};
			this->connection = socket;
		}
		if (this->stopping) return;
		auto rooms = list_relay_rooms(*socket, parse_token(settings.token_hex));
		socket->close();
		std::string listing{};
		for (const auto &room : rooms) {
			if (not listing.empty()) {
				listing += "\n";
			}
			listing += "#" + std::to_string(room.room_id) + "  "
			        + room.room_name + "  (" + room.host_name + ")";
		}
		this->publish(RelayLobbyStatus{
			.phase = "menu",
			.message = rooms.empty()
			           ? "No games are waiting."
			           : "The first room is selected; choose Join Room or enter another ID.",
			.rooms = std::move(listing),
			.suggested_room_id = rooms.empty() ? 0 : rooms.front().room_id,
		});
	}
	catch (const std::exception &error) {
		if (not this->stopping) {
			this->publish(RelayLobbyStatus{
				.phase = "error",
				.message = error.what(),
			});
		}
	}
}

} // namespace openage::network
