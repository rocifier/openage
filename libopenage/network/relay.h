// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "gamestate/types.h"
#include "network/transport.h"

namespace openage::network {

/** Protocol minor version that introduced relay admission messages. */
constexpr uint16_t RELAY_PROTOCOL_MINOR = 1;

/** Protocol minor version that introduced public relay rooms. */
constexpr uint16_t RELAY_DIRECTORY_PROTOCOL_MINOR = 2;

/** Multi-slot room registration was introduced in minor version 3. */
constexpr uint16_t RELAY_MULTIGUEST_PROTOCOL_MINOR = 3;

/** Size of a lobby-issued relay admission token. */
constexpr size_t RELAY_TOKEN_SIZE = 32;

/** A 256-bit relay bearer credential. */
using RelayToken = std::array<uint8_t, RELAY_TOKEN_SIZE>;

/** Largest room name advertised by the relay directory. */
constexpr size_t MAX_RELAY_ROOM_NAME_SIZE = 64;

/** Side of one relayed host-to-guest connection. */
enum class RelayRole : uint16_t {
	HOST = 1,
	GUEST = 2,
};

/** Admission request sent before the ordinary match handshake. */
struct RelayConnect {
	uint64_t match_id{};
	gamestate::player_id_t player_id{};
	RelayRole role{RelayRole::GUEST};
	RelayToken token{};
};

/** Credentials provisioned by the lobby for one host-to-guest pair. */
struct RelayPairCredentials {
	uint64_t match_id{};
	gamestate::player_id_t player_id{};
	RelayToken host_token{};
	RelayToken guest_token{};
};

/** The two authenticated connections for one relay pair. */
struct RelayPair {
	uint64_t match_id{};
	gamestate::player_id_t player_id{};
	TcpConnection host{};
	TcpConnection guest{};
};

/** A room visible to clients browsing the relay directory. */
struct RelayRoomSummary {
	uint64_t room_id{};
	std::string room_name{};
	std::string host_name{};
};

/** A room-directory response and the network version advertised by its relay. */
struct RelayRoomListing {
	ProtocolVersion protocol_version{};
	std::vector<RelayRoomSummary> rooms{};
};

/** A newly advertised room and its still-connected host socket. */
struct HostedRelayRoom {
	uint64_t room_id{};
	TcpConnection connection{};
};

/**
 * Thread-safe directory of rooms created by connected hosts.
 *
 * An empty authorized-token list makes the directory public. Otherwise each
 * request must provide a token from the list, and a host token cannot join its
 * own room.
 */
class RelayRoomDirectory {
public:
	/** Create a bounded public or token-restricted room directory. */
	RelayRoomDirectory(size_t max_rooms,
	                   std::vector<RelayToken> authorized_tokens = {});

	/**
	 * Handle one room creation, listing or join request.
	 *
	 * @return A completed host/guest pair after a successful join, or nullopt
	 * after a listing, room creation or rejected request.
	 */
	std::optional<RelayPair> admit(TcpConnection &connection);

	/** Admit a temporary connection. */
	std::optional<RelayPair> admit(TcpConnection &&connection);

	/** Close and remove every waiting room. */
	size_t cancel_all();

	/** Live room summaries, ordered by room ID. */
	std::vector<RelayRoomSummary> list();

	/** Number of rooms that still have a host channel available. */
	size_t room_count() const;

	/** Whether clients must present an authorized token. */
	bool requires_token() const;

private:
	struct PendingRoom {
		RelayRoomSummary summary;
		RelayToken host_token;
		std::map<gamestate::player_id_t, TcpConnection> host_slots{};
		std::vector<RelayToken> admitted_guest_tokens{};
		gamestate::player_id_t next_host_slot{3};
	};

	bool authorized(const RelayToken &token) const;
	uint64_t allocate_room_id();

	const size_t max_rooms;
	const std::vector<RelayToken> authorized_tokens;
	mutable std::mutex mutex;
	std::map<uint64_t, PendingRoom> rooms;
	uint64_t next_room_id{1};
};

/**
 * Thread-safe registry of lobby-provisioned relay pairs awaiting admission.
 *
 * A pair leaves the registry permanently when both roles authenticate.
 * Cancelling a pending pair closes whichever side has already connected.
 */
class RelayPairRegistry {
public:
	/** Create a registry with a fixed upper bound on pending pairs. */
	explicit RelayPairRegistry(size_t max_pairs);

	/**
	 * Add credentials for one pair.
	 *
	 * Pair identities and tokens must be unique among pending pairs.
	 */
	void provision(const RelayPairCredentials &credentials);

	/**
	 * Authenticate one newly accepted connection.
	 *
	 * @return A completed pair when this connection supplies its second role,
	 * or nullopt after storing the first role or rejecting the connection.
	 */
	std::optional<RelayPair> admit(TcpConnection &connection);

	/** Admit a temporary connection. */
	std::optional<RelayPair> admit(TcpConnection &&connection);

	/** Close and remove a pair that has not completed admission. */
	bool cancel(uint64_t match_id, gamestate::player_id_t player_id);

	/** Close and remove every pair that has not completed admission. */
	size_t cancel_all();

	/** Number of provisioned pairs still awaiting one or both roles. */
	size_t pair_count() const;

	/** Number of authenticated connections held by pending pairs. */
	size_t connection_count() const;

private:
	struct PairKey {
		uint64_t match_id;
		gamestate::player_id_t player_id;

		bool operator<(const PairKey &other) const;
	};

	struct PendingPair {
		RelayPairCredentials credentials;
		std::optional<TcpConnection> host;
		std::optional<TcpConnection> guest;
	};

	const size_t max_pairs;
	mutable std::mutex mutex;
	std::map<PairKey, PendingPair> pairs;
};

/** Encode and validate a relay admission payload. */
std::vector<uint8_t> encode_relay_connect(const RelayConnect &connect);

/** Decode and validate a relay admission payload. */
RelayConnect decode_relay_connect(const std::vector<uint8_t> &payload);

/** Send a bounded rejection reason and close a pre-session connection. */
void reject_relay(TcpConnection &connection, const std::string &reason);

/**
 * Open an outbound relay connection and authenticate one side of a pair.
 *
 * The returned connection is ready for the ordinary FVD host/client
 * handshake. Relay admission frames are consumed by this function.
 */
/** Authenticate an already-open relay socket (allows a caller to cancel the wait). */
void authenticate_relay(TcpConnection &connection, const RelayConnect &connect);

TcpConnection connect_relay(const std::string &address,
                            uint16_t port,
                            const RelayConnect &connect);

/** Create and advertise a room without waiting for a guest. */
uint64_t create_relay_room(TcpConnection &connection,
                           const RelayToken &token,
                           const std::string &room_name,
                           const std::string &host_name);

/** Connect, create and advertise a room without waiting for a guest. */
HostedRelayRoom create_relay_room(const std::string &address,
                                  uint16_t port,
                                  const RelayToken &token,
                                  const std::string &room_name,
                                  const std::string &host_name);

/** Reserve an additional independently forwarded host connection in a room.
 * The room stays discoverable until all seven slots are used.
 * Each additional host connection must use the original creator token.
 * Returns the reserved provisional participant ID (3..8).
 */
gamestate::player_id_t attach_relay_room(
	TcpConnection &connection, const RelayToken &host_token, uint64_t room_id);

/** Wait for a guest on one reserved host channel (slot 2 for the first). */
void await_relay_room(TcpConnection &connection, uint64_t room_id,
                      gamestate::player_id_t slot = 2);

/** Fetch the current public room list, authenticating when required. */
RelayRoomListing list_relay_rooms(TcpConnection &connection,
                                  const RelayToken &token);

/** Connect and fetch the current public room list. */
RelayRoomListing list_relay_rooms(const std::string &address,
                                  uint16_t port,
                                  const RelayToken &token);

/** Join a listed room and wait until the relay connects it to the host. */
/** Join one available room slot, receiving the assigned provisional participant ID. */
gamestate::player_id_t join_relay_room_slot(TcpConnection &connection,
                                           const RelayToken &token,
                                           uint64_t room_id);

/** Legacy two-player convenience wrapper (the slot is not returned). */
void join_relay_room(TcpConnection &connection,
                     const RelayToken &token,
                     uint64_t room_id);

/** Connect, join a listed room and wait for its host. */
TcpConnection join_relay_room(const std::string &address,
                              uint16_t port,
                              const RelayToken &token,
                              uint64_t room_id);

/**
 * Accept both sides of one provisioned pair in either arrival order.
 *
 * Malformed and unauthorized connections are rejected without consuming a
 * role. Credentials are consumed by returning the pair and must not be reused.
 */
RelayPair accept_relay_pair(TcpListener &listener,
                            const RelayPairCredentials &credentials);

/**
 * Forward complete protocol frames in both directions until either peer
 * disconnects.
 */
void forward_relay_pair(TcpConnection &host, TcpConnection &guest);

/** Forward an owned pair until either peer disconnects. */
void forward_relay_pair(RelayPair pair);

} // namespace openage::network
