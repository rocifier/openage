// Copyright 2026 the openage authors. See copying.md for legal info.

#include "relay_service.h"

#include <chrono>
#include <exception>
#include <thread>

#include "network/relay.h"
#include "testing/testing.h"

namespace openage::network::tests {

namespace {

RelayToken service_test_token(uint8_t seed) {
	RelayToken token{};
	for (size_t i = 0; i < token.size(); ++i) {
		token[i] = static_cast<uint8_t>(seed + i);
	}
	return token;
}

} // namespace

void relay_service() {
	auto host_token = service_test_token(1);
	auto guest_token = service_test_token(101);
	auto guest_token_two = service_test_token(151);
	auto guest_token_three = service_test_token(201);
	RelayRoomDirectory directory{
		1, {host_token, guest_token, guest_token_two, guest_token_three}};
	TESTEQUALS(directory.requires_token(), true);
	TESTEQUALS(directory.room_count(), size_t{0});

	auto listener = TcpListener::listen("127.0.0.1", 0);
	auto port = listener.get_port();
	std::exception_ptr service_error{};
	std::jthread service{[&]() {
		try {
			serve_relay_directory(listener, directory, 8);
		}
		catch (...) {
			service_error = std::current_exception();
		}
	}};

	RelayToken unauthorized{};
	TESTTHROWS(list_relay_rooms("127.0.0.1", port, unauthorized));
	auto hosted = create_relay_room(
		"127.0.0.1", port, host_token, "Test Room", "Host Player");
	TESTEQUALS(hosted.room_id != 0, true);
	TESTEQUALS(directory.room_count(), size_t{1});
	TESTTHROWS(create_relay_room(
		"127.0.0.1", port, guest_token, "Overflow Room", "Other Host"));
	TESTEQUALS(directory.room_count(), size_t{1});

	auto rooms = list_relay_rooms("127.0.0.1", port, guest_token);
	TESTEQUALS(rooms.protocol_version.major, PROTOCOL_MAJOR);
	TESTEQUALS(rooms.protocol_version.minor, PROTOCOL_MINOR);
	TESTEQUALS(rooms.rooms.size(), size_t{1});
	TESTEQUALS(rooms.rooms[0].room_id, hosted.room_id);
	TESTEQUALS(rooms.rooms[0].room_name, std::string{"Test Room"});
	TESTEQUALS(rooms.rooms[0].host_name, std::string{"Host Player"});
	TESTTHROWS(join_relay_room("127.0.0.1", port, host_token, hosted.room_id));
	TESTEQUALS(directory.room_count(), size_t{1});

	auto guest = join_relay_room(
		"127.0.0.1", port, guest_token, hosted.room_id);
	await_relay_room(hosted.connection, hosted.room_id);
	TESTEQUALS(directory.room_count(), size_t{0});
	TESTEQUALS(list_relay_rooms("127.0.0.1", port, guest_token).rooms.empty(), true);

	Frame frame{};
	frame.message_type = MessageType::PING;
	frame.session_id = 201;
	frame.sequence = 1;
	frame.payload = {1, 2, 3};
	hosted.connection.send_frame(frame);
	auto forwarded = guest.receive_frame();
	forwarded.has_value() or TESTFAIL;
	TESTEQUALS(forwarded->session_id, frame.session_id);
	TESTEQUALS(forwarded->payload == frame.payload, true);

	// One public room may reserve seven separately forwarded host-to-guest
	// channels. A guest consumes only its own channel; other slots remain
	// advertised and preserve their independent session frame streams.
	auto group = create_relay_room(
		"127.0.0.1", port, host_token, "Multi Guest", "Host Player");
	auto host_three = TcpConnection::connect("127.0.0.1", port);
	auto host_four = TcpConnection::connect("127.0.0.1", port);
	TESTEQUALS(attach_relay_room(host_three, host_token, group.room_id),
	           gamestate::player_id_t{3});
	TESTEQUALS(attach_relay_room(host_four, host_token, group.room_id),
	           gamestate::player_id_t{4});
	TESTEQUALS(directory.room_count(), size_t{1});
	TESTEQUALS(list_relay_rooms("127.0.0.1", port, guest_token).rooms.size(), size_t{1});
	auto guest_two = TcpConnection::connect("127.0.0.1", port);
	TESTEQUALS(join_relay_room_slot(guest_two, guest_token, group.room_id),
	           gamestate::player_id_t{2});
	await_relay_room(group.connection, group.room_id);
	TESTEQUALS(directory.room_count(), size_t{1});
	TESTEQUALS(list_relay_rooms("127.0.0.1", port, guest_token_two).rooms.size(), size_t{1});
	TESTTHROWS(join_relay_room("127.0.0.1", port, guest_token, group.room_id));
	auto guest_three = TcpConnection::connect("127.0.0.1", port);
	TESTEQUALS(join_relay_room_slot(guest_three, guest_token_two, group.room_id),
	           gamestate::player_id_t{3});
	await_relay_room(host_three, group.room_id, 3);
	TESTEQUALS(directory.room_count(), size_t{1});
	auto guest_four = TcpConnection::connect("127.0.0.1", port);
	TESTEQUALS(join_relay_room_slot(guest_four, guest_token_three, group.room_id),
	           gamestate::player_id_t{4});
	await_relay_room(host_four, group.room_id, 4);
	TESTEQUALS(directory.room_count(), size_t{0});
	auto sample = Frame{};
	sample.message_type = MessageType::PING;
	sample.session_id = 301;
	sample.sequence = 1;
	sample.payload = {3, 7};
	host_three.send_frame(sample);
	auto echo = guest_three.receive_frame();
	echo.has_value() or TESTFAIL;
	TESTEQUALS(echo->session_id, uint64_t{301});
	TESTEQUALS(echo->payload == sample.payload, true);
	// A second pair's data must not be received on the third's socket.
	TESTEQUALS(guest_four.try_receive_frame().has_value(), false);
	host_four.send_frame(sample);
	echo = guest_four.receive_frame();
	echo.has_value() or TESTFAIL;
	TESTEQUALS(echo->session_id, uint64_t{301});
	TESTEQUALS(echo->payload == sample.payload, true);
	group.connection.close();
	host_three.close();
	host_four.close();
	guest_two.close();
	guest_three.close();
	guest_four.close();

	// A closed host must not leave an advertised, joinable phantom room.
	// The relay's old send-only heartbeat could succeed after peer close.
	auto abandoned = create_relay_room(
		"127.0.0.1", port, host_token, "Abandoned Room", "Former Host");
	TESTEQUALS(directory.room_count(), size_t{1});
	abandoned.connection.close();
	bool expired = false;
	for (size_t attempt = 0; attempt < 50; ++attempt) {
		if (list_relay_rooms("127.0.0.1", port, guest_token).rooms.empty()) {
			expired = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{10});
	}
	TESTEQUALS(expired, true);
	TESTEQUALS(directory.room_count(), size_t{0});
	TESTTHROWS(join_relay_room("127.0.0.1", port, guest_token, abandoned.room_id));

	listener.close();
	service.join();
	if (service_error) {
		std::rethrow_exception(service_error);
	}
	hosted.connection.close();
	guest.close();

	RelayRoomDirectory public_directory{1};
	TESTEQUALS(public_directory.requires_token(), false);
}

} // namespace openage::network::tests
