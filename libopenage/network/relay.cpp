// Copyright 2026 the openage authors. See copying.md for legal info.

#include "relay.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "error/error.h"
#include "log/log.h"
#include "log/message.h"
#include "network/serialization.h"

namespace openage::network {

namespace {

constexpr size_t RELAY_CONNECT_PAYLOAD_SIZE = 52;
constexpr size_t RELAY_READY_PAYLOAD_SIZE = 16;
constexpr uint32_t MAX_RELAY_REJECTION_SIZE = 4096;
constexpr size_t RELAY_TOKEN_PAYLOAD_SIZE = RELAY_TOKEN_SIZE;
constexpr size_t RELAY_JOIN_PAYLOAD_SIZE = RELAY_TOKEN_SIZE + sizeof(uint64_t);
constexpr size_t MAX_RELAY_DIRECTORY_ROOMS = 4096;

struct RelayCreateRoomRequest {
	RelayToken token{};
	std::string room_name{};
	std::string host_name{};
};

struct RelayJoinRoomRequest {
	RelayToken token{};
	uint64_t room_id{};
};

bool is_known_relay_role(RelayRole role) {
	return role == RelayRole::HOST or role == RelayRole::GUEST;
}

bool is_zero_token(const RelayToken &token) {
	return std::all_of(token.begin(), token.end(), [](uint8_t value) {
		return value == 0;
	});
}

bool tokens_equal(const RelayToken &left, const RelayToken &right) {
	uint8_t difference{};
	for (size_t i = 0; i < RELAY_TOKEN_SIZE; ++i) {
		difference |= left[i] ^ right[i];
	}
	return difference == 0;
}

void require_relay_connect(const RelayConnect &connect) {
	if (connect.match_id == 0) {
		throw Error{MSG(err) << "FVD relay match ID must be nonzero"};
	}
	if (connect.player_id == 0) {
		throw Error{MSG(err) << "FVD relay player ID must be nonzero"};
	}
	if (not is_known_relay_role(connect.role)) {
		throw Error{MSG(err) << "Unknown FVD relay role "
		                     << static_cast<uint16_t>(connect.role)};
	}
	if (is_zero_token(connect.token)) {
		throw Error{MSG(err) << "FVD relay token must not be empty"};
	}
}

void require_pair_credentials(const RelayPairCredentials &credentials) {
	RelayConnect host{
		credentials.match_id,
		credentials.player_id,
		RelayRole::HOST,
		credentials.host_token};
	RelayConnect guest{
		credentials.match_id,
		credentials.player_id,
		RelayRole::GUEST,
		credentials.guest_token};
	require_relay_connect(host);
	require_relay_connect(guest);
	if (tokens_equal(credentials.host_token, credentials.guest_token)) {
		throw Error{MSG(err) << "FVD relay host and guest tokens must be distinct"};
	}
}

Frame relay_frame(MessageType type, std::vector<uint8_t> payload) {
	Frame frame{};
	frame.message_type = type;
	frame.session_id = 0;
	frame.sequence = 0;
	frame.payload = std::move(payload);
	return frame;
}

void require_relay_envelope(const Frame &frame) {
	if (frame.protocol_major != PROTOCOL_MAJOR
	    or frame.protocol_minor < RELAY_PROTOCOL_MINOR
	    or frame.protocol_minor > PROTOCOL_MINOR) {
		throw Error{MSG(err) << "Incompatible FVD relay protocol version "
		                     << frame.protocol_major << "." << frame.protocol_minor};
	}
	if (frame.session_id != 0 or frame.sequence != 0) {
		throw Error{MSG(err) << "FVD relay admission requires zero session and sequence"};
	}
}

void require_directory_envelope(const Frame &frame) {
	require_relay_envelope(frame);
	if (frame.protocol_minor < RELAY_DIRECTORY_PROTOCOL_MINOR) {
		throw Error{MSG(err) << "FVD relay directory requires protocol minor "
		                     << RELAY_DIRECTORY_PROTOCOL_MINOR};
	}
}

void require_directory_text(const std::string &value,
                            size_t maximum_size,
                            const char *field) {
	if (value.empty() or value.size() > maximum_size) {
		throw Error{MSG(err) << "FVD relay " << field << " is empty or too long"};
	}
	for (const auto ch : value) {
		auto byte = static_cast<unsigned char>(ch);
		if (byte < 32 or byte == 127) {
			throw Error{MSG(err) << "FVD relay " << field << " contains a control character"};
		}
	}
}

void append_directory_text(std::vector<uint8_t> &payload,
                           const std::string &value) {
	append_u32(payload, static_cast<uint32_t>(value.size()));
	payload.insert(payload.end(), value.begin(), value.end());
}

std::string read_directory_text(const std::vector<uint8_t> &payload,
                                size_t &offset,
                                size_t maximum_size,
                                const char *field) {
	if (payload.size() - offset < sizeof(uint32_t)) {
		throw Error{MSG(err) << "Truncated FVD relay " << field};
	}
	auto size = read_u32(payload.data() + offset);
	offset += sizeof(uint32_t);
	if (size > maximum_size or payload.size() - offset < size) {
		throw Error{MSG(err) << "Invalid FVD relay " << field << " size"};
	}
	std::string value{
		reinterpret_cast<const char *>(payload.data() + offset), size};
	offset += size;
	require_directory_text(value, maximum_size, field);
	return value;
}

std::vector<uint8_t> encode_create_room_request(
	const RelayToken &token,
	const std::string &room_name,
	const std::string &host_name) {
	require_directory_text(room_name, MAX_RELAY_ROOM_NAME_SIZE, "room name");
	require_directory_text(host_name, MAX_PARTICIPANT_NAME_SIZE, "host name");
	std::vector<uint8_t> payload{};
	payload.reserve(RELAY_TOKEN_SIZE + sizeof(uint32_t) * 2
	                + room_name.size() + host_name.size());
	payload.insert(payload.end(), token.begin(), token.end());
	append_directory_text(payload, room_name);
	append_directory_text(payload, host_name);
	return payload;
}

RelayCreateRoomRequest decode_create_room_request(
	const std::vector<uint8_t> &payload) {
	if (payload.size() < RELAY_TOKEN_SIZE + sizeof(uint32_t) * 2) {
		throw Error{MSG(err) << "Truncated FVD relay room creation request"};
	}
	RelayCreateRoomRequest request{};
	std::copy_n(payload.data(), RELAY_TOKEN_SIZE, request.token.begin());
	size_t offset{RELAY_TOKEN_SIZE};
	request.room_name = read_directory_text(
		payload, offset, MAX_RELAY_ROOM_NAME_SIZE, "room name");
	request.host_name = read_directory_text(
		payload, offset, MAX_PARTICIPANT_NAME_SIZE, "host name");
	if (offset != payload.size()) {
		throw Error{MSG(err) << "Trailing data in FVD relay room creation request"};
	}
	return request;
}

std::vector<uint8_t> encode_token_request(const RelayToken &token) {
	return {token.begin(), token.end()};
}

RelayToken decode_token_request(const std::vector<uint8_t> &payload) {
	if (payload.size() != RELAY_TOKEN_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "Invalid FVD relay token request size"};
	}
	RelayToken token{};
	std::copy_n(payload.data(), RELAY_TOKEN_SIZE, token.begin());
	return token;
}

std::vector<uint8_t> encode_join_room_request(const RelayToken &token,
	                                            uint64_t room_id) {
	if (room_id == 0) {
		throw Error{MSG(err) << "FVD relay room ID must be nonzero"};
	}
	auto payload = encode_token_request(token);
	append_u64(payload, room_id);
	return payload;
}

RelayJoinRoomRequest decode_join_room_request(
	const std::vector<uint8_t> &payload) {
	if (payload.size() != RELAY_JOIN_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "Invalid FVD relay room join request size"};
	}
	RelayJoinRoomRequest request{};
	std::copy_n(payload.data(), RELAY_TOKEN_SIZE, request.token.begin());
	request.room_id = read_u64(payload.data() + RELAY_TOKEN_SIZE);
	if (request.room_id == 0) {
		throw Error{MSG(err) << "FVD relay room ID must be nonzero"};
	}
	return request;
}

std::vector<uint8_t> encode_room_list(
	const std::vector<RelayRoomSummary> &rooms) {
	if (rooms.size() > MAX_RELAY_DIRECTORY_ROOMS) {
		throw Error{MSG(err) << "FVD relay room list exceeds its protocol limit"};
	}
	std::vector<uint8_t> payload{};
	append_u32(payload, static_cast<uint32_t>(rooms.size()));
	for (const auto &room : rooms) {
		if (room.room_id == 0) {
			throw Error{MSG(err) << "FVD relay room list contains an invalid ID"};
		}
		require_directory_text(
			room.room_name, MAX_RELAY_ROOM_NAME_SIZE, "room name");
		require_directory_text(
			room.host_name, MAX_PARTICIPANT_NAME_SIZE, "host name");
		append_u64(payload, room.room_id);
		append_directory_text(payload, room.room_name);
		append_directory_text(payload, room.host_name);
	}
	if (payload.size() > MAX_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "FVD relay room list exceeds the frame limit"};
	}
	return payload;
}

std::vector<RelayRoomSummary> decode_room_list(
	const std::vector<uint8_t> &payload) {
	if (payload.size() < sizeof(uint32_t)) {
		throw Error{MSG(err) << "Truncated FVD relay room list"};
	}
	auto count = read_u32(payload.data());
	if (count > MAX_RELAY_DIRECTORY_ROOMS) {
		throw Error{MSG(err) << "FVD relay room list exceeds its protocol limit"};
	}
	size_t offset{sizeof(uint32_t)};
	std::vector<RelayRoomSummary> rooms{};
	rooms.reserve(count);
	for (uint32_t i = 0; i < count; ++i) {
		if (payload.size() - offset < sizeof(uint64_t)) {
			throw Error{MSG(err) << "Truncated FVD relay room list entry"};
		}
		RelayRoomSummary room{};
		room.room_id = read_u64(payload.data() + offset);
		offset += sizeof(uint64_t);
		if (room.room_id == 0) {
			throw Error{MSG(err) << "FVD relay room list contains an invalid ID"};
		}
		room.room_name = read_directory_text(
			payload, offset, MAX_RELAY_ROOM_NAME_SIZE, "room name");
		room.host_name = read_directory_text(
			payload, offset, MAX_PARTICIPANT_NAME_SIZE, "host name");
		rooms.push_back(std::move(room));
	}
	if (offset != payload.size()) {
		throw Error{MSG(err) << "Trailing data in FVD relay room list"};
	}
	return rooms;
}

uint64_t decode_room_created(const std::vector<uint8_t> &payload) {
	if (payload.size() != sizeof(uint64_t)) {
		throw Error{MSG(err) << "Invalid FVD relay room creation response"};
	}
	auto room_id = read_u64(payload.data());
	if (room_id == 0) {
		throw Error{MSG(err) << "FVD relay created an invalid room ID"};
	}
	return room_id;
}

std::vector<uint8_t> encode_relay_ready(uint64_t match_id,
                                        gamestate::player_id_t player_id) {
	std::vector<uint8_t> payload{};
	payload.reserve(RELAY_READY_PAYLOAD_SIZE);
	append_u64(payload, match_id);
	append_u64(payload, player_id);
	return payload;
}

void require_relay_ready(const std::vector<uint8_t> &payload,
                         const RelayConnect &connect) {
	if (payload.size() != RELAY_READY_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "Invalid FVD relay ready payload size " << payload.size()};
	}
	auto match_id = read_u64(payload.data());
	auto player_id = read_u64(payload.data() + sizeof(uint64_t));
	if (match_id != connect.match_id or player_id != connect.player_id) {
		throw Error{MSG(err) << "FVD relay ready response does not match requested pair"};
	}
}

std::vector<uint8_t> encode_relay_reject(const std::string &reason) {
	auto reason_size = std::min(reason.size(), static_cast<size_t>(MAX_RELAY_REJECTION_SIZE));
	std::vector<uint8_t> payload{};
	payload.reserve(sizeof(uint32_t) + reason_size);
	append_u32(payload, static_cast<uint32_t>(reason_size));
	payload.insert(payload.end(), reason.begin(), reason.begin() + reason_size);
	return payload;
}

std::string decode_relay_reject(const std::vector<uint8_t> &payload) {
	if (payload.size() < sizeof(uint32_t)) {
		throw Error{MSG(err) << "Truncated FVD relay rejection payload"};
	}
	auto reason_size = read_u32(payload.data());
	if (reason_size > MAX_RELAY_REJECTION_SIZE
	    or payload.size() != sizeof(uint32_t) + reason_size) {
		throw Error{MSG(err) << "Invalid FVD relay rejection payload size"};
	}
	return std::string{
		reinterpret_cast<const char *>(payload.data() + sizeof(uint32_t)),
		reason_size};
}

void reject_relay_connection(TcpConnection &connection, const std::string &reason) {
	try {
		connection.send_frame(relay_frame(
			MessageType::RELAY_REJECT,
			encode_relay_reject(reason)));
	}
	catch (...) {
		// A peer closing during rejection cannot affect the provisioned pair.
	}
	connection.close();
}

Frame receive_directory_response(TcpConnection &connection,
                                 const char *operation) {
	auto response = connection.receive_frame();
	if (not response.has_value()) {
		throw Error{MSG(err) << "FVD relay disconnected during " << operation};
	}
	require_directory_envelope(*response);
	if (response->message_type == MessageType::RELAY_REJECT) {
		throw Error{MSG(err) << "FVD relay rejected " << operation << ": "
		                     << decode_relay_reject(response->payload)};
	}
	return std::move(*response);
}

std::optional<RelayConnect> receive_relay_connect(TcpConnection &connection) {
	auto frame = connection.receive_frame();
	if (not frame.has_value()) {
		return std::nullopt;
	}
	require_relay_envelope(*frame);
	if (frame->message_type != MessageType::RELAY_CONNECT) {
		throw Error{MSG(err) << "Expected FVD relay admission, received message "
		                     << static_cast<uint16_t>(frame->message_type)};
	}
	return decode_relay_connect(frame->payload);
}

bool is_authorized(const RelayConnect &connect,
                   const RelayPairCredentials &credentials) {
	if (connect.match_id != credentials.match_id
	    or connect.player_id != credentials.player_id) {
		return false;
	}
	const auto &expected_token = connect.role == RelayRole::HOST ? credentials.host_token : credentials.guest_token;
	return tokens_equal(connect.token, expected_token);
}

bool credentials_share_token(const RelayPairCredentials &left,
                             const RelayPairCredentials &right) {
	return tokens_equal(left.host_token, right.host_token)
	       or tokens_equal(left.host_token, right.guest_token)
	       or tokens_equal(left.guest_token, right.host_token)
	       or tokens_equal(left.guest_token, right.guest_token);
}

} // namespace

void reject_relay(TcpConnection &connection, const std::string &reason) {
	reject_relay_connection(connection, reason);
}

std::vector<uint8_t> encode_relay_connect(const RelayConnect &connect) {
	require_relay_connect(connect);

	std::vector<uint8_t> payload{};
	payload.reserve(RELAY_CONNECT_PAYLOAD_SIZE);
	append_u64(payload, connect.match_id);
	append_u64(payload, connect.player_id);
	append_u16(payload, static_cast<uint16_t>(connect.role));
	append_u16(payload, 0);
	payload.insert(payload.end(), connect.token.begin(), connect.token.end());
	return payload;
}

RelayConnect decode_relay_connect(const std::vector<uint8_t> &payload) {
	if (payload.size() != RELAY_CONNECT_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "Invalid FVD relay admission payload size " << payload.size()};
	}
	if (read_u16(payload.data() + 18) != 0) {
		throw Error{MSG(err) << "Unsupported FVD relay admission flags"};
	}

	RelayConnect connect{};
	connect.match_id = read_u64(payload.data());
	connect.player_id = read_u64(payload.data() + 8);
	connect.role = static_cast<RelayRole>(read_u16(payload.data() + 16));
	std::copy_n(payload.data() + 20, RELAY_TOKEN_SIZE, connect.token.begin());
	require_relay_connect(connect);
	return connect;
}

void authenticate_relay(TcpConnection &connection, const RelayConnect &connect) {
	auto payload = encode_relay_connect(connect);
	connection.send_frame(relay_frame(
		MessageType::RELAY_CONNECT,
		std::move(payload)));

	auto response = connection.receive_frame();
	if (not response.has_value()) {
		throw Error{MSG(err) << "FVD relay disconnected during admission"};
	}
	require_relay_envelope(*response);
	if (response->message_type == MessageType::RELAY_REJECT) {
		throw Error{MSG(err) << "FVD relay rejected connection: "
		                     << decode_relay_reject(response->payload)};
	}
	if (response->message_type != MessageType::RELAY_READY) {
		throw Error{MSG(err) << "Expected FVD relay admission reply, received message "
		                     << static_cast<uint16_t>(response->message_type)};
	}
	require_relay_ready(response->payload, connect);
}

TcpConnection connect_relay(const std::string &address,
                            uint16_t port,
                            const RelayConnect &connect) {
	auto connection = TcpConnection::connect(address, port);
	authenticate_relay(connection, connect);
	return connection;
}

uint64_t create_relay_room(TcpConnection &connection,
                           const RelayToken &token,
                           const std::string &room_name,
                           const std::string &host_name) {
	connection.send_frame(relay_frame(
		MessageType::RELAY_CREATE_ROOM,
		encode_create_room_request(token, room_name, host_name)));
	auto response = receive_directory_response(connection, "room creation");
	if (response.message_type != MessageType::RELAY_ROOM_CREATED) {
		throw Error{MSG(err) << "Expected FVD relay room creation response, received message "
		                     << static_cast<uint16_t>(response.message_type)};
	}
	return decode_room_created(response.payload);
}

HostedRelayRoom create_relay_room(const std::string &address,
                                  uint16_t port,
                                  const RelayToken &token,
                                  const std::string &room_name,
                                  const std::string &host_name) {
	auto connection = TcpConnection::connect(address, port);
	auto room_id = create_relay_room(connection, token, room_name, host_name);
	return {room_id, std::move(connection)};
}

gamestate::player_id_t attach_relay_room(
	TcpConnection &connection, const RelayToken &host_token, uint64_t room_id) {
	connection.send_frame(relay_frame(
		MessageType::RELAY_ATTACH_ROOM, encode_join_room_request(host_token, room_id)));
	auto answer = receive_directory_response(connection, "host slot attachment");
	while (answer.message_type == MessageType::PING) {
		if (not answer.payload.empty()) {
			throw Error{MSG(err) << "Invalid FVD relay attachment heartbeat"};
		}
		answer = receive_directory_response(connection, "host slot attachment");
	}
	if (answer.message_type != MessageType::RELAY_ROOM_ATTACHED
	    or answer.payload.size() != RELAY_READY_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "Relay did not acknowledge the additional host channel"};
	}
	const auto slot = read_u64(answer.payload.data() + 8);
	if (slot < 3 or slot > MAX_STATE_PLAYERS) {
		throw Error{MSG(err) << "Relay supplied an invalid additional host slot"};
	}
	require_relay_ready(answer.payload, {room_id, slot, RelayRole::HOST, {}});
	return slot;
}

void await_relay_room(TcpConnection &connection, uint64_t room_id,
                      gamestate::player_id_t slot) {
	if (room_id == 0) {
		throw Error{MSG(err) << "Cannot await an invalid FVD relay room"};
	}
	Frame response{};
	while (true) {
		response = receive_directory_response(connection, "room join");
		if (response.message_type != MessageType::PING) {
			break;
		}
		if (not response.payload.empty()) {
			throw Error{MSG(err) << "Invalid FVD relay room heartbeat"};
		}
	}
	if (response.message_type != MessageType::RELAY_READY) {
		throw Error{MSG(err) << "Expected FVD relay ready response, received message "
		                     << static_cast<uint16_t>(response.message_type)};
	}
	RelayConnect expected{
		room_id, slot, RelayRole::HOST, {}};
	require_relay_ready(response.payload, expected);
}

std::vector<RelayRoomSummary> list_relay_rooms(TcpConnection &connection,
                                               const RelayToken &token) {
	connection.send_frame(relay_frame(
		MessageType::RELAY_LIST_ROOMS,
		encode_token_request(token)));
	auto response = receive_directory_response(connection, "room listing");
	if (response.message_type != MessageType::RELAY_ROOM_LIST) {
		throw Error{MSG(err) << "Expected FVD relay room list, received message "
		                     << static_cast<uint16_t>(response.message_type)};
	}
	return decode_room_list(response.payload);
}

std::vector<RelayRoomSummary> list_relay_rooms(const std::string &address,
                                               uint16_t port,
                                               const RelayToken &token) {
	auto connection = TcpConnection::connect(address, port);
	return list_relay_rooms(connection, token);
}

gamestate::player_id_t join_relay_room_slot(TcpConnection &connection,
                                           const RelayToken &token,
                                           uint64_t room_id) {
	connection.send_frame(relay_frame(
		MessageType::RELAY_JOIN_ROOM,
		encode_join_room_request(token, room_id)));
	auto response = receive_directory_response(connection, "room join");
	if (response.message_type != MessageType::RELAY_READY) {
		throw Error{MSG(err) << "Expected FVD relay ready response, received message "
		                     << static_cast<uint16_t>(response.message_type)};
	}
	if (response.payload.size() != RELAY_READY_PAYLOAD_SIZE) {
		throw Error{MSG(err) << "Invalid FVD relay guest room slot acknowledgement"};
	}
	const auto slot = read_u64(response.payload.data() + 8);
	if (slot < 2 or slot > MAX_STATE_PLAYERS) {
		throw Error{MSG(err) << "Invalid provisional FVD guest slot"};
	}
	require_relay_ready(response.payload, {room_id, slot, RelayRole::GUEST, {}});
	return slot;
}

void join_relay_room(TcpConnection &connection,
                     const RelayToken &token,
                     uint64_t room_id) {
	(void) join_relay_room_slot(connection, token, room_id);
}

TcpConnection join_relay_room(const std::string &address,
                              uint16_t port,
                              const RelayToken &token,
                              uint64_t room_id) {
	auto connection = TcpConnection::connect(address, port);
	join_relay_room(connection, token, room_id);
	return connection;
}

RelayRoomDirectory::RelayRoomDirectory(
	size_t max_rooms,
	std::vector<RelayToken> authorized_tokens) :
	max_rooms{max_rooms},
	authorized_tokens{std::move(authorized_tokens)},
	mutex{},
	rooms{},
	next_room_id{1} {
	if (max_rooms == 0 or max_rooms > MAX_RELAY_DIRECTORY_ROOMS) {
		throw Error{MSG(err) << "FVD relay room capacity must be between 1 and "
		                     << MAX_RELAY_DIRECTORY_ROOMS};
	}
	for (size_t i = 0; i < this->authorized_tokens.size(); ++i) {
		if (is_zero_token(this->authorized_tokens[i])) {
			throw Error{MSG(err) << "FVD relay authorized token must not be empty"};
		}
		for (size_t j = 0; j < i; ++j) {
			if (tokens_equal(this->authorized_tokens[i], this->authorized_tokens[j])) {
				throw Error{MSG(err) << "FVD relay authorized tokens must be unique"};
			}
		}
	}
}

bool RelayRoomDirectory::authorized(const RelayToken &token) const {
	if (this->authorized_tokens.empty()) {
		return true;
	}
	return std::any_of(
		this->authorized_tokens.begin(),
		this->authorized_tokens.end(),
		[&token](const RelayToken &expected) {
			return tokens_equal(token, expected);
		});
}

uint64_t RelayRoomDirectory::allocate_room_id() {
	for (size_t attempt = 0; attempt <= this->rooms.size(); ++attempt) {
		auto candidate = this->next_room_id++;
		if (this->next_room_id == 0) {
			this->next_room_id = 1;
		}
		if (candidate != 0 and not this->rooms.contains(candidate)) {
			return candidate;
		}
	}
	throw Error{MSG(err) << "FVD relay room ID space is exhausted"};
}

std::optional<RelayPair> RelayRoomDirectory::admit(TcpConnection &connection) {
	std::optional<Frame> request{};
	try {
		request = connection.receive_frame();
		if (request) {
			require_directory_envelope(*request);
		}
	}
	catch (const Error &) {
		reject_relay_connection(connection, "malformed relay directory request");
		return std::nullopt;
	}
	if (not request) {
		return std::nullopt;
	}

	if (request->message_type == MessageType::RELAY_LIST_ROOMS) {
		RelayToken token{};
		try {
			token = decode_token_request(request->payload);
		}
		catch (const Error &) {
			reject_relay_connection(connection, "malformed room listing request");
			return std::nullopt;
		}
		if (not this->authorized(token)) {
			reject_relay_connection(connection, "player token is not authorized");
			return std::nullopt;
		}
		connection.send_frame(relay_frame(
			MessageType::RELAY_ROOM_LIST,
			encode_room_list(this->list())));
		return std::nullopt;
	}

	if (request->message_type == MessageType::RELAY_CREATE_ROOM) {
		RelayCreateRoomRequest create{};
		try {
			create = decode_create_room_request(request->payload);
		}
		catch (const Error &) {
			reject_relay_connection(connection, "malformed room creation request");
			return std::nullopt;
		}
		if (not this->authorized(create.token)) {
			reject_relay_connection(connection, "player token is not authorized");
			return std::nullopt;
		}

		std::scoped_lock lock{this->mutex};
		if (this->rooms.size() >= this->max_rooms) {
			reject_relay_connection(connection, "relay room directory is full");
			return std::nullopt;
		}
		auto room_id = this->allocate_room_id();
		RelayRoomSummary summary{
			room_id, std::move(create.room_name), std::move(create.host_name)};
		auto inserted = this->rooms.emplace(
			room_id, PendingRoom{summary, create.token, {}, {}, 3});
		inserted.first->second.host_slots.emplace(2, std::move(connection));
		std::vector<uint8_t> payload{};
		append_u64(payload, room_id);
		try {
			inserted.first->second.host_slots.at(2).send_frame(relay_frame(
				MessageType::RELAY_ROOM_CREATED, std::move(payload)));
			log::log(INFO << "[FVD/relay] room_created room=" << room_id
			              << " pending_slots=1");
		}
		catch (...) {
			this->rooms.erase(inserted.first);
		}
		return std::nullopt;
	}

	if (request->message_type == MessageType::RELAY_ATTACH_ROOM) {
		RelayJoinRoomRequest attach{};
		try {
			attach = decode_join_room_request(request->payload);
		}
		catch (const Error &) {
			reject_relay_connection(connection, "malformed host room attachment");
			return std::nullopt;
		}
		if (request->protocol_minor < RELAY_MULTIGUEST_PROTOCOL_MINOR) {
			reject_relay_connection(connection, "multi-guest room attachment requires protocol minor 3");
			return std::nullopt;
		}
		std::scoped_lock lock{this->mutex};
		auto found = this->rooms.find(attach.room_id);
		if (found == this->rooms.end()
		    or not this->authorized(attach.token)
		    or not tokens_equal(found->second.host_token, attach.token)
		    or is_zero_token(found->second.host_token)
		    or found->second.next_host_slot > MAX_STATE_PLAYERS) {
			reject_relay_connection(connection, "invalid or full host room attachment");
			return std::nullopt;
		}
		const auto slot = found->second.next_host_slot++;
		auto inserted = found->second.host_slots.emplace(slot, std::move(connection));
		try {
			inserted.first->second.send_frame(relay_frame(
				MessageType::RELAY_ROOM_ATTACHED,
				encode_relay_ready(attach.room_id, slot)));
			log::log(INFO << "[FVD/relay] host_slot_reserved room="
			              << attach.room_id << " slot=" << slot);
		}
		catch (...) {
			found->second.host_slots.erase(inserted.first);
		}
		return std::nullopt;
	}

	if (request->message_type == MessageType::RELAY_JOIN_ROOM) {
		RelayJoinRoomRequest join{};
		try {
			join = decode_join_room_request(request->payload);
		}
		catch (const Error &) {
			reject_relay_connection(connection, "malformed room join request");
			return std::nullopt;
		}
		if (not this->authorized(join.token)) {
			reject_relay_connection(connection, "player token is not authorized");
			return std::nullopt;
		}

		std::optional<RelayPair> completed{};
		std::string rejection{};
		{
			std::scoped_lock lock{this->mutex};
			auto found = this->rooms.find(join.room_id);
			if (found == this->rooms.end()) {
				rejection = "relay room is no longer available";
			}
			else if (this->requires_token()
			         and tokens_equal(found->second.host_token, join.token)) {
				rejection = "host and guest must use different player tokens";
			}
			else if (this->requires_token()
			         and std::find(found->second.admitted_guest_tokens.begin(),
			                      found->second.admitted_guest_tokens.end(), join.token)
			             != found->second.admitted_guest_tokens.end()) {
				rejection = "player token is already admitted to this room";
			}
			else {
				// A host publishes up to seven independent channels, each
				// consumed by one guest without disturbing earlier pairs.
				auto &slots = found->second.host_slots;
				while (not slots.empty() and not slots.begin()->second.peer_connected()) {
					slots.erase(slots.begin());
				}
				if (slots.empty()) {
					this->rooms.erase(found);
					rejection = "relay room host has disconnected or has no free channels";
				}
				else {
					auto available = slots.begin();
					completed.emplace(RelayPair{
						join.room_id, available->first,
						std::move(available->second), std::move(connection)});
					slots.erase(available);
					found->second.admitted_guest_tokens.push_back(join.token);
					if (slots.empty()) this->rooms.erase(found);
				}
			}
		}
		if (not rejection.empty()) {
			reject_relay_connection(connection, rejection);
			return std::nullopt;
		}

		auto ready = relay_frame(
			MessageType::RELAY_READY,
			encode_relay_ready(completed->match_id, completed->player_id));
		try {
			completed->host.send_frame(ready);
			completed->guest.send_frame(ready);
			// This transport is role-agnostic: it never sees the Hunter
			// lottery or authority epoch inside authenticated game frames.
			log::log(INFO << "[FVD/relay] pair_connected room="
			              << completed->match_id << " slot=" << completed->player_id);
		}
		catch (...) {
			completed->host.close();
			reject_relay_connection(
				completed->guest, "relay room host disconnected before join");
			return std::nullopt;
		}
		return completed;
	}

	reject_relay_connection(connection, "unsupported relay directory request");
	return std::nullopt;
}

std::optional<RelayPair> RelayRoomDirectory::admit(TcpConnection &&connection) {
	return this->admit(connection);
}

size_t RelayRoomDirectory::cancel_all() {
	std::scoped_lock lock{this->mutex};
	auto count = this->rooms.size();
	this->rooms.clear();
	return count;
}

std::vector<RelayRoomSummary> RelayRoomDirectory::list() {
	std::scoped_lock lock{this->mutex};
	std::vector<RelayRoomSummary> result{};
	result.reserve(this->rooms.size());
	auto heartbeat = relay_frame(MessageType::PING, {});
	for (auto room = this->rooms.begin(); room != this->rooms.end();) {
		// Each unconsumed host slot has its own socket. Keep the room
		// visible after the first join, but never advertise dead channels.
		auto &slots = room->second.host_slots;
		for (auto it = slots.begin(); it != slots.end();) {
			if (not it->second.peer_connected()) {
				it = slots.erase(it);
				continue;
			}
			try {
				it->second.send_frame(heartbeat);
				++it;
			}
			catch (...) {
				it = slots.erase(it);
			}
		}
		if (slots.empty()) {
			room = this->rooms.erase(room);
		}
		else {
			result.push_back(room->second.summary);
			++room;
		}
	}
	return result;
}

size_t RelayRoomDirectory::room_count() const {
	std::scoped_lock lock{this->mutex};
	return this->rooms.size();
}

bool RelayRoomDirectory::requires_token() const {
	return not this->authorized_tokens.empty();
}

RelayPairRegistry::RelayPairRegistry(size_t max_pairs) :
	max_pairs{max_pairs},
	mutex{},
	pairs{} {
	if (max_pairs == 0) {
		throw Error{MSG(err) << "FVD relay pair registry capacity must be nonzero"};
	}
}

bool RelayPairRegistry::PairKey::operator<(const PairKey &other) const {
	if (this->match_id != other.match_id) {
		return this->match_id < other.match_id;
	}
	return this->player_id < other.player_id;
}

void RelayPairRegistry::provision(const RelayPairCredentials &credentials) {
	require_pair_credentials(credentials);
	std::scoped_lock lock{this->mutex};
	if (this->pairs.size() >= this->max_pairs) {
		throw Error{MSG(err) << "FVD relay pair registry is full"};
	}

	PairKey key{credentials.match_id, credentials.player_id};
	if (this->pairs.contains(key)) {
		throw Error{MSG(err) << "FVD relay pair is already provisioned"};
	}
	for (const auto &entry : this->pairs) {
		const auto &pending = entry.second;
		if (credentials_share_token(credentials, pending.credentials)) {
			throw Error{MSG(err) << "FVD relay token is already provisioned"};
		}
	}
	this->pairs.emplace(
		key,
		PendingPair{credentials, std::nullopt, std::nullopt});
}

std::optional<RelayPair> RelayPairRegistry::admit(TcpConnection &connection) {
	std::optional<RelayConnect> connect{};
	try {
		connect = receive_relay_connect(connection);
	}
	catch (const Error &) {
		reject_relay_connection(connection, "malformed relay admission");
		return std::nullopt;
	}
	if (not connect.has_value()) {
		return std::nullopt;
	}

	std::optional<RelayPair> completed{};
	std::string rejection{};
	{
		std::scoped_lock lock{this->mutex};
		PairKey key{connect->match_id, connect->player_id};
		auto found = this->pairs.find(key);
		if (found == this->pairs.end()
		    or not is_authorized(*connect, found->second.credentials)) {
			rejection = "relay credential was rejected";
		}
		else {
			auto &slot = connect->role == RelayRole::HOST
			           ? found->second.host
			           : found->second.guest;
			if (slot.has_value()) {
				rejection = connect->role == RelayRole::HOST
				          ? "relay host is already connected"
				          : "relay guest is already connected";
			}
			else {
				slot = std::move(connection);
				if (found->second.host.has_value()
				    and found->second.guest.has_value()) {
					completed.emplace(RelayPair{
						connect->match_id,
						connect->player_id,
						std::move(*found->second.host),
						std::move(*found->second.guest)});
					this->pairs.erase(found);
				}
			}
		}
	}

	if (not rejection.empty()) {
		reject_relay_connection(connection, rejection);
		return std::nullopt;
	}
	if (not completed.has_value()) {
		return std::nullopt;
	}

	auto ready = relay_frame(
		MessageType::RELAY_READY,
		encode_relay_ready(completed->match_id, completed->player_id));
	completed->host.send_frame(ready);
	completed->guest.send_frame(ready);
	return completed;
}

std::optional<RelayPair> RelayPairRegistry::admit(TcpConnection &&connection) {
	return this->admit(connection);
}

bool RelayPairRegistry::cancel(uint64_t match_id,
                               gamestate::player_id_t player_id) {
	if (match_id == 0 or player_id == 0) {
		throw Error{MSG(err) << "FVD relay cancellation requires a valid pair identity"};
	}
	std::scoped_lock lock{this->mutex};
	return this->pairs.erase({match_id, player_id}) > 0;
}

size_t RelayPairRegistry::cancel_all() {
	std::scoped_lock lock{this->mutex};
	auto count = this->pairs.size();
	this->pairs.clear();
	return count;
}

size_t RelayPairRegistry::pair_count() const {
	std::scoped_lock lock{this->mutex};
	return this->pairs.size();
}

size_t RelayPairRegistry::connection_count() const {
	std::scoped_lock lock{this->mutex};
	size_t count{};
	for (const auto &entry : this->pairs) {
		const auto &pair = entry.second;
		count += pair.host.has_value() ? 1 : 0;
		count += pair.guest.has_value() ? 1 : 0;
	}
	return count;
}

RelayPair accept_relay_pair(TcpListener &listener,
                            const RelayPairCredentials &credentials) {
	RelayPairRegistry registry{1};
	registry.provision(credentials);
	while (true) {
		if (auto pair = registry.admit(listener.accept())) {
			return std::move(*pair);
		}
	}
}

void forward_relay_pair(TcpConnection &host, TcpConnection &guest) {
	std::atomic_bool stopping{};
	std::mutex error_mutex{};
	std::exception_ptr relay_error{};

	auto stop = [&](std::exception_ptr error) {
		auto first_stop = not stopping.exchange(true);
		if (first_stop and error) {
			std::scoped_lock lock{error_mutex};
			relay_error = error;
		}
		host.close();
		guest.close();
	};
	auto forward = [&](TcpConnection &source, TcpConnection &destination) {
		try {
			while (auto frame = source.receive_frame()) {
				destination.send_frame(*frame);
			}
			stop(nullptr);
		}
		catch (...) {
			if (not stopping.load()) {
				stop(std::current_exception());
			}
		}
	};

	std::jthread host_to_guest{forward, std::ref(host), std::ref(guest)};
	forward(guest, host);
	host_to_guest.join();
	if (relay_error) {
		std::rethrow_exception(relay_error);
	}
}

void forward_relay_pair(RelayPair pair) {
	forward_relay_pair(pair.host, pair.guest);
}

} // namespace openage::network
