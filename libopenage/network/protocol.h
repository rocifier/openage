// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace openage::network {

/**
 * Magic value at the start of every FVD network frame (ASCII "FVDN").
 */
constexpr uint32_t FRAME_MAGIC = 0x4656444e;

/**
 * Current protocol version.
 */
// Fog of war appends each side's map view to world state and adds
// remembered buildings, which older clients cannot parse (v15 added Builder
// tower demolition). v17 adds authoritative match outcomes to full and delta
// world state. Refuse older clients at the handshake rather than mid-match.
// Minor versions continue to gate compatible relay features.
constexpr uint16_t PROTOCOL_MAJOR = 17;
constexpr uint16_t PROTOCOL_MINOR = 4;

/** Oldest protocol minor version compatible with this implementation. */
constexpr uint16_t PROTOCOL_MINIMUM_COMPATIBLE_MINOR = 0;

/** Protocol version advertised during handshake or selected for a session. */
struct ProtocolVersion {
	uint16_t major{PROTOCOL_MAJOR};
	uint16_t minor{PROTOCOL_MINOR};
};

/**
 * Size of the fixed frame header in bytes.
 */
constexpr size_t FRAME_HEADER_SIZE = 32;

/**
 * Largest payload accepted from one frame.
 */
constexpr uint32_t MAX_PAYLOAD_SIZE = 1024 * 1024;

/**
 * Messages exchanged by an FVD lobby or match session.
 */
enum class MessageType : uint16_t {
	CLIENT_HELLO = 1,
	SERVER_ACCEPT = 2,
	SERVER_REJECT = 3,
	PARTICIPANT_SNAPSHOT = 4,
	START_MATCH = 5,
	COMMAND_INTENT = 6,
	COMMAND_RESULT = 7,
	WORLD_SNAPSHOT = 8,
	WORLD_DELTA = 9,
	PARTICIPANT_DISCONNECTED = 10,
	PING = 11,
	PONG = 12,
	SNAPSHOT_REQUEST = 13,
	RELAY_CONNECT = 14,
	RELAY_READY = 15,
	RELAY_REJECT = 16,
	MATCH_READY = 17,
	RELAY_CREATE_ROOM = 18,
	RELAY_ROOM_CREATED = 19,
	RELAY_LIST_ROOMS = 20,
	RELAY_ROOM_LIST = 21,
	RELAY_JOIN_ROOM = 22,
	RELAY_ATTACH_ROOM = 23,
	RELAY_ROOM_ATTACHED = 24,
	AUTHORITY_REHOST_OFFER = 25,
	AUTHORITY_REHOST_PROOF = 26,
};

/** Whether a message type is defined by this protocol version. */
bool is_known_message_type(MessageType message_type);

/**
 * One decoded protocol frame.
 *
 * The session ID is zero during the handshake. The host assigns a nonzero ID
 * when it accepts the connection. Sequence numbers are monotonic per
 * connection and direction; handshake frames use sequence zero.
 */
struct Frame {
	uint16_t protocol_major{PROTOCOL_MAJOR};
	uint16_t protocol_minor{PROTOCOL_MINOR};
	MessageType message_type{MessageType::CLIENT_HELLO};
	uint16_t flags{0};
	uint64_t session_id{0};
	uint64_t sequence{0};
	std::vector<uint8_t> payload{};
};

/**
 * Encode a frame in network byte order.
 *
 * @param frame Frame to encode.
 * @return Header and payload bytes ready for a stream transport.
 * @throws Error if the frame contains unsupported flags or an oversized
 * payload.
 */
std::vector<uint8_t> encode_frame(const Frame &frame);

/**
 * Decode exactly one complete frame.
 *
 * @param bytes Complete encoded frame.
 * @return Decoded frame.
 * @throws Error if the bytes do not contain one valid frame.
 */
Frame decode_frame(const std::vector<uint8_t> &bytes);

/**
 * Incrementally separates frames from an ordered byte stream.
 */
class FrameParser {
public:
	/**
	 * Add bytes received from the transport.
	 *
	 * @return Every complete frame made available by this input. An incomplete
	 * trailing frame remains buffered for the next call.
	 * @throws Error if a frame header is invalid or declares an oversized
	 * payload.
	 */
	std::vector<Frame> push(const uint8_t *bytes, size_t size);

	/**
	 * Convenience overload for a byte vector.
	 */
	std::vector<Frame> push(const std::vector<uint8_t> &bytes);

	/**
	 * Number of bytes currently buffered for an incomplete frame.
	 */
	size_t buffered_size() const;

private:
	std::vector<uint8_t> buffer{};
};

} // namespace openage::network
