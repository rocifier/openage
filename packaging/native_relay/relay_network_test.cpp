// Copyright 2026 the openage authors. See copying.md for legal info.

#include "network/relay.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "network/relay_service.h"

namespace {

void require(bool value, const char *message) {
	if (not value) {
		throw std::runtime_error{message};
	}
}

openage::network::RelayToken test_token(uint8_t seed) {
	openage::network::RelayToken token{};
	for (size_t i = 0; i < token.size(); ++i) {
		token[i] = static_cast<uint8_t>(seed + i);
	}
	return token;
}

void test_room_forwarding() {
	using namespace openage::network;

	const auto host_token = test_token(1);
	const auto guest_token = test_token(101);
	RelayRoomDirectory directory{1, {host_token, guest_token}};
	auto listener = TcpListener::listen("127.0.0.1", 0);
	const auto port = listener.get_port();

	std::exception_ptr service_error{};
	std::thread server{[&]() {
		try {
			serve_relay_directory(listener, directory, 8);
		}
		catch (...) {
			service_error = std::current_exception();
		}
	}};

	try {
		auto hosted = create_relay_room("127.0.0.1", port, host_token,
		                                "Native relay", "Host");
		auto listing = list_relay_rooms("127.0.0.1", port, guest_token);
		require(listing.rooms.size() == 1, "native relay did not advertise room");
		require(listing.rooms.front().room_id == hosted.room_id, "native room ID mismatch");

		auto guest = join_relay_room("127.0.0.1", port, guest_token, hosted.room_id);
		await_relay_room(hosted.connection, hosted.room_id);

		Frame sent{};
		sent.message_type = MessageType::PING;
		sent.session_id = 5;
		sent.sequence = 1;
		sent.payload = {11, 22, 33};
		hosted.connection.send_frame(sent);
		auto received = guest.receive_frame();
		require(received.has_value(), "native relay dropped the forwarded frame");
		require(received->session_id == sent.session_id, "native relay changed session ID");
		require(received->payload == sent.payload, "native relay changed frame payload");

		guest.close();
		hosted.connection.close();
	}
	catch (...) {
		listener.close();
		server.join();
		throw;
	}

	listener.close();
	server.join();
	if (service_error) {
		std::rethrow_exception(service_error);
	}
}

} // namespace

int main() {
	try {
		test_room_forwarding();
		return 0;
	}
	catch (const std::exception &error) {
		std::cerr << "FVD native relay integration: " << error.what() << '\n';
		return 1;
	}
}
