// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "coord/phys.h"
#include "coord/tile.h"
#include "gamestate/types.h"
#include "network/protocol.h"
#include "time/time.h"

namespace openage::network {

/** Byte length of the gameplay asset manifest digest. */
constexpr size_t ASSET_DIGEST_SIZE = 32;

/** Largest participant name accepted during the handshake. */
constexpr uint32_t MAX_PARTICIPANT_NAME_SIZE = 128;

/** Largest game build version accepted during the handshake. */
constexpr uint32_t MAX_BUILD_VERSION_SIZE = 128;

/** Largest displayable handshake rejection reason. */
constexpr uint32_t MAX_HANDSHAKE_REJECTION_SIZE = 4096;

/** Largest entity selection accepted in one network command. */
constexpr uint32_t MAX_COMMAND_ENTITIES = 1024;

/** Largest entity definition identifier accepted in world state. */
constexpr uint32_t MAX_ENTITY_DEFINITION_SIZE = 256;

/** Defensive collection limits for one world state payload. */
constexpr uint32_t MAX_STATE_PLAYERS = 8;
constexpr uint32_t MAX_STATE_ENTITIES = 16384;
constexpr uint32_t MAX_STATE_RESERVATIONS = 16384;
constexpr uint32_t MAX_STATE_CONSTRUCTIONS = 1024;
constexpr uint32_t MAX_STATE_ATTACK_ORDERS = 16384;
constexpr uint32_t MAX_STATE_ARROWS = 2048;
/** Bounded future position keyframes sent per entity and snapshot. */
constexpr uint16_t MAX_ENTITY_MOTION_KEYS = 16;
constexpr uint16_t MAX_ENTITY_FACING_KEYS = 16;

/** Largest displayable command rejection reason. */
constexpr uint32_t MAX_REJECTION_REASON_SIZE = 4096;
/** Maximum sites in one ordered tower placement line. */
constexpr uint32_t MAX_BUILD_LINE_SITES = 256;

/** Commands a client may request from the authoritative host. */
enum class CommandKind : uint16_t {
	MOVE = 1,
	ATTACK_MOVE = 2,
	BUILD_TOWER = 3,
	FOCUS_ATTACK = 4,
	PRIORITIZE_FOUNDATION = 5,
	SET_STANCE = 6,
	PATROL = 7,
	DEMOLISH_TOWER = 8,
	/** The Hunter tower (sole entity) sets where new horses ride to. */
	SET_RALLY_POINT = 9,
	/** Enter a completed owned tower with one selected Builder. */
	GARRISON = 10,
	/** Eject a Builder from one selected occupied tower. */
	UNGARRISON = 11,
};

/** Host decision for one client command. */
enum class CommandStatus : uint16_t {
	ACCEPTED = 1,
	REJECTED = 2,
};

/** Reason an accepted participant connection ended. */
enum class DisconnectReason : uint16_t {
	CONNECTION_LOST = 1,
	LEFT_MATCH = 2,
	PROTOCOL_ERROR = 3,
	HOST_SHUTDOWN = 4,
};

/** Whether a disconnect reason is defined by this protocol version. */
bool is_known_disconnect_reason(DisconnectReason reason);

/** SHA-256 digest of gameplay-affecting modpack manifests and data. */
using AssetDigest = std::array<uint8_t, ASSET_DIGEST_SIZE>;

/** Compatibility information sent by a connecting participant. */
struct ClientHello {
	uint16_t minimum_protocol_minor{PROTOCOL_MINIMUM_COMPATIBLE_MINOR};
	std::string participant_name{};
	std::string build_version{};
	AssetDigest asset_digest{};
};

/** Identity assigned by the host to an accepted connection. */
struct ServerAccept {
	uint64_t session_id{0};
	gamestate::player_id_t player_id{0};
};

/** Displayable reason why the host refused a connection. */
struct ServerReject {
	std::string reason{};
};

/** Player-independent client intent. Ownership comes from the connection. */
struct CommandIntent {
	uint64_t command_id{0};
	CommandKind kind{CommandKind::MOVE};
	std::vector<gamestate::entity_id_t> entity_ids{};
	coord::phys3 target{0, 0, 0};
	/** Optional target entity for focus attacks and foundation prioritization. */
	std::optional<gamestate::entity_id_t> target_entity{};
	/** Append a tower to an existing Builder construction queue. */
	bool queue{false};
	/** Site positions in drag order; empty for ordinary one-site placement. */
	std::vector<coord::phys3> build_line{};
	/** Requested Hunter stance; present exactly for SET_STANCE. */
	std::optional<gamestate::HunterStance> stance{};
};

/** Authoritative result for one submitted command. */
struct CommandResult {
	uint64_t command_id{0};
	CommandStatus status{CommandStatus::REJECTED};
	uint64_t world_revision{0};
	time::time_t simulation_time{};
	std::string reason{};
};

/** A future keyframe in the authoritative linear position curve. */
struct MotionKeyframe {
	time::time_t time{};
	coord::phys3 position{0, 0, 0};
};

/** One future instantaneous facing turn authored by the simulation. */
struct FacingKeyframe {
	time::time_t time{};
	coord::phys_angle_t angle{0};
};

/** One authoritative entity position and its future movement curve. */
struct EntityState {
	gamestate::entity_id_t entity_id{0};
	gamestate::player_id_t owner_id{0};
	std::string definition{};
	coord::phys3 position{0, 0, 0};
	std::optional<int64_t> health{};
	std::optional<int64_t> maximum_health{};
	/** The receiving player's own Tower kills; omitted for other entities. */
	std::optional<uint64_t> tower_kills{};
	/** Future authoritative position keys; empty for stationary entities. */
	std::vector<MotionKeyframe> motion_keys{};
	/** Sampled orientation and future turn events, not a guest movement guess. */
	coord::phys_angle_t angle{0};
	std::vector<FacingKeyframe> facing_keys{};
	/** Host-selected animation and absolute start time of its active loop. */
	gamestate::AnimationAction animation_action{gamestate::AnimationAction::IDLE};
	time::time_t animation_started_at{time::TIME_ZERO};
	/** Stance of the receiving Hunter's own horses; omitted for other entities. */
	std::optional<gamestate::HunterStance> hunter_stance{};
	/**
	 * An enemy building last seen here, now hidden by fog. It may have
	 * changed or been destroyed since; it carries no health or motion.
	 */
	bool remembered{false};
};

/** FVD role assigned to one playable player. */
enum class PlayerRole : uint16_t {
	HUNTER = 1,
	BUILDER = 2,
};

/** A stable identity in the pre-match lobby; not yet a game player slot. */
struct LobbyParticipant {
	uint64_t participant_id{0};
	std::string name{};
	bool is_host{false};
};

/** Complete host-authored pre-match participant list. */
struct ParticipantSnapshot {
	uint64_t revision{0};
	uint64_t host_participant_id{0};
	std::vector<LobbyParticipant> participants{};
};

/** One immutable participant-to-game-player mapping established at start. */
struct LobbyAssignment {
	uint64_t participant_id{0};
	gamestate::player_id_t player_id{0};
	PlayerRole role{PlayerRole::BUILDER};
};

/** Host-only match-start announcement with the Hunter draw and Builder slots. */
struct StartMatch {
	uint64_t lobby_revision{0};
	uint64_t host_participant_id{0};
	std::vector<LobbyAssignment> assignments{};
};

/**
 * One side's view of the map, one bit per tile: bit i of word i / 64 for
 * tile index i = ne * map_size + se. Visible tiles are always explored.
 */
struct TeamFog {
	PlayerRole team{PlayerRole::BUILDER};
	std::vector<uint64_t> explored{};
	std::vector<uint64_t> visible{};
	/** Enemy buildings remembered in fog; local presentation only, never sent. */
	std::vector<gamestate::entity_id_t> remembered{};

	bool operator==(const TeamFog &other) const = default;
};

/** Number of 64-bit words holding one bit per tile of a square map. */
size_t fog_word_count(uint32_t map_size);

/** Whether a tile is set in a fog bitset; out-of-map tiles never are. */
bool fog_tile_set(const std::vector<uint64_t> &bits, uint32_t map_size, const coord::tile &tile);

/** One playable player in authoritative world state. */
struct PlayerState {
	gamestate::player_id_t player_id{0};
	PlayerRole role{PlayerRole::BUILDER};
	bool connected{true};
};

/** One tile held by an unfinished construction order. */
struct TileReservation {
	coord::tile tile{0, 0};
	gamestate::entity_id_t builder_id{0};
};

/** Network-visible phase of an unfinished construction order. */
enum class ConstructionPhase : uint16_t {
	PLANNED = 1,
	UNDER_CONSTRUCTION = 2,
};

/** One authoritative unfinished tower construction order. */
struct ConstructionState {
	uint64_t order_id{0};
	gamestate::player_id_t owner_id{0};
	gamestate::entity_id_t builder_id{0};
	coord::tile target{0, 0};
	ConstructionPhase phase{ConstructionPhase::PLANNED};
	time::time_t phase_end_time{};
	/** Host-authored phase start for time-indexed construction progress. */
	time::time_t phase_start_time{};
	gamestate::entity_id_t site_entity_id{0};

	/** Completion percentage evaluated on the authoritative phase interval. */
	int progress_percent(const time::time_t &now) const;
};

/** One active Hunter AttackMove or focus-attack order. */
struct AttackOrderState {
	gamestate::entity_id_t horse_id{0};
	coord::phys3 destination{0, 0, 0};
	std::optional<gamestate::entity_id_t> target_id{};
	bool reached_destination{false};
	bool focus_target{false};
};

/** One authoritative tower arrow currently in flight. */
struct ArrowState {
	uint64_t arrow_id{0};
	gamestate::player_id_t owner_id{0};
	gamestate::entity_id_t target_id{0};
	coord::phys3 start{0, 0, 0};
	coord::phys3 destination{0, 0, 0};
	time::time_t launch_time{};
	time::time_t impact_time{};
};

/** The authoritative winner, never inferred by a recipient from fogged state. */
enum class MatchWinner : uint16_t {
	HUNTER = 1,
	BUILDERS = 2,
};

/** Why the host ended the game. Reasons have fixed winner semantics. */
enum class MatchEndReason : uint16_t {
	ALL_BUILDERS_DEAD = 1,
	TIME_LIMIT = 2,
};

struct MatchOutcome {
	MatchWinner winner{MatchWinner::HUNTER};
	MatchEndReason reason{MatchEndReason::ALL_BUILDERS_DEAD};
	time::time_t ended_at{time::TIME_ZERO};

	bool operator==(const MatchOutcome &other) const = default;
};

/** Complete authoritative FVD state carried by a snapshot or delta. */
struct WorldState {
	uint64_t base_revision{0};
	uint64_t world_revision{0};
	time::time_t simulation_time{};
	uint32_t map_size{0};
	gamestate::player_id_t local_player_id{0};
	uint64_t local_kill_count{0};
	/** Pending first Hunter spawn deadline, authored by the simulation. */
	std::optional<time::time_t> first_hunter_spawn_time{};
	/** Set only after authoritative victory detection; terminal and public. */
	std::optional<MatchOutcome> match_outcome{};
	std::vector<PlayerState> players{};
	std::vector<EntityState> entities{};
	std::vector<TileReservation> reservations{};
	std::vector<ConstructionState> constructions{};
	std::vector<AttackOrderState> attack_orders{};
	std::vector<ArrowState> arrows{};
	/**
	 * The recipient's side's view on the wire (in a delta: only when it
	 * changed); both sides in a local presentation snapshot.
	 */
	std::vector<TeamFog> fog{};
};

/** Request for a full snapshot after a client cannot apply a world delta. */
struct SnapshotRequest {
	uint64_t last_applied_revision{0};
};

/** Authoritative notification that a participant left the match. */
struct ParticipantDisconnected {
	gamestate::player_id_t player_id{0};
	DisconnectReason reason{DisconnectReason::CONNECTION_LOST};
};

/** Encode a ClientHello payload. */
std::vector<uint8_t> encode_client_hello(const ClientHello &hello);

/** Decode and validate a ClientHello payload. */
ClientHello decode_client_hello(const std::vector<uint8_t> &payload);

/** Encode a ServerAccept payload. */
std::vector<uint8_t> encode_server_accept(const ServerAccept &accept);

/** Decode and validate a ServerAccept payload. */
ServerAccept decode_server_accept(const std::vector<uint8_t> &payload);

/** Encode a ServerReject payload. */
std::vector<uint8_t> encode_server_reject(const ServerReject &reject);

/** Decode and validate a ServerReject payload. */
ServerReject decode_server_reject(const std::vector<uint8_t> &payload);

/** Guest receipt of the first authoritative snapshot after StartMatch. */
struct MatchReady {
	gamestate::player_id_t player_id{0};
	uint64_t snapshot_revision{0};
};

std::vector<uint8_t> encode_match_ready(const MatchReady &ready);
MatchReady decode_match_ready(const std::vector<uint8_t> &payload);

/** Encode/decode complete pre-start lobby participant snapshots. */
std::vector<uint8_t> encode_participant_snapshot(const ParticipantSnapshot &snapshot);
ParticipantSnapshot decode_participant_snapshot(const std::vector<uint8_t> &payload);

/** Encode/decode the authoritative participant-to-game-player assignment. */
std::vector<uint8_t> encode_start_match(const StartMatch &start);
StartMatch decode_start_match(const std::vector<uint8_t> &payload);

/** Encode a CommandIntent payload. */
std::vector<uint8_t> encode_command_intent(const CommandIntent &intent);

/** Decode and validate a CommandIntent payload. */
CommandIntent decode_command_intent(const std::vector<uint8_t> &payload);

/** Encode a CommandResult payload. */
std::vector<uint8_t> encode_command_result(const CommandResult &result);

/** Decode and validate a CommandResult payload. */
CommandResult decode_command_result(const std::vector<uint8_t> &payload);

/** Encode a WorldSnapshot or WorldDelta payload. */
std::vector<uint8_t> encode_world_state(const WorldState &state, bool partial = false);

/** Decode and validate a WorldSnapshot or WorldDelta payload. */
WorldState decode_world_state(const std::vector<uint8_t> &payload, bool partial = false);

/** Encode a SnapshotRequest payload. */
std::vector<uint8_t> encode_snapshot_request(const SnapshotRequest &request);

/** Decode and validate a SnapshotRequest payload. */
SnapshotRequest decode_snapshot_request(const std::vector<uint8_t> &payload);

/** Encode a ParticipantDisconnected payload. */
std::vector<uint8_t> encode_participant_disconnected(
	const ParticipantDisconnected &disconnected);

/** Decode and validate a ParticipantDisconnected payload. */
ParticipantDisconnected decode_participant_disconnected(
	const std::vector<uint8_t> &payload);

} // namespace openage::network
