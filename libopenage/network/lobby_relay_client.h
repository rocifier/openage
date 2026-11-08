// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>

#include "network/messages.h"
#include "network/command_mailbox.h"
#include "network/transport.h"
#include "network/rehost_ticket.h"

namespace openage::network {

class ReplicationTracker;
class RelayMultiHostLobby;

/**
 * One host or guest request to a relay-managed public room directory.
 */
struct RelayLobbyConfig {
	std::string address{"60.234.5.16"};
	uint16_t port{40000};
	uint64_t room_id{0};
	std::string room_name{"FVD Game"};
	std::string token_hex{};
	std::string name{};
	bool hosting{false};
	/** Maximum people in a hosted room, including the host (2..8). */
	size_t player_capacity{2};
	/** An already drawn match is rehosted, never subjected to a second lottery. */
	std::optional<StartMatch> rehost_start{};
	/** Hunter-created room ready for new guests and private-ticket admission. */
	std::shared_ptr<RelayMultiHostLobby> prepared_rehost_lobby{};
	/** Private one-use proof to join the Hunter's replacement room. */
	std::optional<RehostOffer> rehost_offer{};
};

struct RelayLobbyStatus {
	std::string phase{"menu"};
	std::string message{"Host a game or refresh the relay room list."};
	std::string participants{};
	bool can_start{false};
	bool connected{false};
	gamestate::player_id_t local_player_id{0};
	gamestate::player_id_t remote_player_id{0};
	/** Host's active remote player IDs, one recipient-private stream each. */
	std::vector<gamestate::player_id_t> remote_player_ids{};
	/** Fixed match size, including the host and guests, after role draw. */
	size_t match_player_count{0};
	/** Host uses the multi-guest world fanout path even when only one guest joins. */
	bool multi_host{false};
	/** Remote Builders that have been disconnected during this match. */
	std::vector<gamestate::player_id_t> disconnected_players{};
	uint64_t snapshot_revision{0};
	/** Number of full recovery snapshots accepted after initial bootstrap. */
	uint32_t snapshot_recoveries{0};
	/** Authenticated gameplay heartbeat replies, useful for link diagnostics. */
	uint32_t heartbeat_acknowledgements{0};
	std::string rooms{};
	/** Network version reported by the most recent room-directory response. */
	std::string relay_network_version{};
	uint64_t suggested_room_id{0};
	/** The same name/role assignments on every peer, established by StartMatch. */
	std::string roles{};
	/** Role assigned to this client by the host, not chosen locally. */
	std::string local_role{};
	/** Feedback for the latest guest order (from an authenticated host result). */
	std::string order_feedback{};
	/** The latest Builder departure received from the authoritative host. */
	std::string departure_notice{};
	/** Number of guest orders waiting for an authoritative decision. */
	size_t pending_order_count{0};
	/** Seconds remaining in the five-second role reveal. */
	uint8_t role_seconds_remaining{0};
};

/**
 * In the current two-peer relay the host may continue only when its own
 * player is Hunter and the departed guest is a Builder. Bootstrap must
 * already be acknowledged. A departing authoritative host always ends the
 * guest's session, regardless of the host's gameplay role.
 */
bool host_can_continue_after_guest_loss(const RelayLobbyStatus &status);

/**
 * Background relay transport for the pre-match screen. Only the worker
 * touches its ConnectionSession, preserving sequence ordering.
 *
 * Match start rebinds the authenticated player slot and exchanges the
 * authoritative initial snapshot with a versioned acknowledgement. Subsequent
 * complete host captures are encoded as WORLD_DELTA packets for the guest's
 * ReplicationTracker. After the five-second reveal the host resumes its
 * simulation, and the guest sends authenticated COMMAND_INTENT frames over
 * this same relay connection. GUI rendering consumes host state instead of
 * advancing a separate guest simulation. Online gameplay currently supports
 * two peers and the basic Move, AttackMove, and BuildTower intents.
 */
class RelayLobbyClient {
public:
	RelayLobbyClient() = default;
	~RelayLobbyClient();
	RelayLobbyClient(const RelayLobbyClient &) = delete;
	RelayLobbyClient &operator=(const RelayLobbyClient &) = delete;

	void connect(RelayLobbyConfig settings);
	void refresh(RelayLobbyConfig settings);
	void request_start();
	/** The local machine actually runs the authoritative simulation. */
	bool is_authority() const;

	/** Supply a snapshot captured on the host simulation thread. */
	void provide_host_snapshot(WorldState snapshot);

	/**
	 * Queue the newest complete recipient-specific authoritative world.
	 * The relay worker calculates a delta against its last transmitted state.
	 * A single pending slot prevents a slow guest from growing an unbounded
	 * queue; skipped intermediate captures are folded into the next delta.
	 */
	void provide_host_update(WorldState world);

	/** Largest launch ID successfully transmitted to a particular guest.
	 * The capture thread uses this as an event cursor, not the newest
	 * offered snapshot, since pending samples may be overwritten. */
	uint64_t delivered_projectile_id(gamestate::player_id_t recipient) const;

	/** Queue one locally authored intent. Only guests send intents over TCP. */
	bool send_guest_command(CommandIntent intent);

	/** Drain remote intents paired with the immutable accepted guest identity. */
	std::vector<AuthenticatedCommand> take_host_commands();

	/**
	 * The simulation may finish a command after a disconnect or rematch.
	 * Replies must match both the original session identity and generation.
	 * False means that session no longer exists; the result is discarded.
	 */
	uint64_t get_session_generation() const;
	/** Revalidate an already queued command before simulation mutation. */
	bool accepts_host_command(const SessionIdentity &identity,
	                          uint64_t generation) const;
	bool send_command_result(const SessionIdentity &identity,
	                         uint64_t generation, CommandResult result);

	/** Consume command decisions (or rejections) on the guest presenter. */
	std::vector<CommandResult> take_command_results();

	/** Request a recipient-specific authoritative snapshot on the active guest. */
	bool request_recovery_snapshot();

	/** Guest's validated authoritative bootstrap state (once received). */
	std::shared_ptr<ReplicationTracker> get_replication() const;

	RelayLobbyStatus status() const;
	void close();

private:
	void run(RelayLobbyConfig settings);
	void run_refresh(RelayLobbyConfig settings);
	void publish(RelayLobbyStatus next);
	void publish_stream_revision(uint64_t revision, bool local_host);
	void run_multi_host(RelayLobbyConfig settings);
	void run_rehosted_guest(RelayLobbyConfig settings,
	                        const RehostOffer &offer);
	void begin_hunter_rehost(RelayLobbyConfig settings,
	                         const StartMatch &assignment,
	                         ConnectionSession &old_session,
	                         TcpConnection &old_socket);
	void forward_rehost_offers(
		const StartMatch &assignment,
		const std::map<gamestate::player_id_t,
		               std::pair<ConnectionSession *, TcpConnection *>> &old_peers,
		const RelayLobbyConfig &settings);
	std::shared_ptr<RelayLobbyClient> replacement() const;
	bool reveal_finished_locked() const;

	mutable std::mutex mutex;
	RelayLobbyStatus current{};
	std::shared_ptr<TcpConnection> connection{};
	std::optional<WorldState> pending_host_snapshot{};
	std::optional<WorldState> pending_host_update{};
	std::map<gamestate::player_id_t, WorldState> pending_multi_snapshots{};
	std::set<gamestate::player_id_t> offered_multi_snapshots{};
	std::map<gamestate::player_id_t, WorldState> pending_multi_updates{};
	std::deque<CommandIntent> pending_guest_commands{};
	std::deque<AuthenticatedCommand> incoming_host_commands{};
	std::deque<CommandResult> pending_command_results{};
	std::map<gamestate::player_id_t, std::deque<CommandResult>> pending_multi_results{};
	std::deque<CommandResult> guest_command_results{};
	std::map<uint64_t, CommandKind> guest_outstanding_commands{};
	uint64_t next_guest_command_id{1};
	uint64_t session_generation{0};
	std::optional<SessionIdentity> active_remote_identity{};
	std::map<gamestate::player_id_t, SessionIdentity> active_multi_identities{};
	bool guest_snapshot_requested{false};
	/** Largest revision submitted, including an update in flight. */
	uint64_t last_host_offered_revision{0};
	std::map<gamestate::player_id_t, uint64_t> delivered_projectiles{};
	std::shared_ptr<ReplicationTracker> guest_replication{};
	std::shared_ptr<RelayMultiHostLobby> multi_host_lobby{};
	std::shared_ptr<RelayMultiHostLobby> prepared_rehost_lobby{};
	std::shared_ptr<RelayLobbyClient> handoff_client{};
	bool hosting{false};
	std::optional<std::chrono::steady_clock::time_point> roles_assigned_at{};
	std::thread worker{};
	std::atomic<bool> stopping{false};
	std::atomic<bool> start_requested{false};
};

} // namespace openage::network
