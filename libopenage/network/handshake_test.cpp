// Copyright 2026 the openage authors. See copying.md for legal info.

#include "handshake.h"

#include <exception>
#include <thread>

#include "network/transport.h"
#include "testing/testing.h"

namespace openage::network::tests {

namespace {

AssetDigest test_digest(uint8_t seed) {
	AssetDigest digest{};
	for (size_t i = 0; i < digest.size(); ++i) {
		digest[i] = static_cast<uint8_t>(seed + i);
	}
	return digest;
}

ClientHello test_hello(const AssetDigest &digest) {
	return {
		PROTOCOL_MINIMUM_COMPATIBLE_MINOR,
		"Builder",
		"fvd-test",
		digest,
	};
}

} // namespace

void handshake() {
	auto digest = test_digest(1);
	auto hello_frame = make_client_hello_frame(test_hello(digest));
	hello_frame.message_type == MessageType::CLIENT_HELLO or TESTFAIL;
	TESTEQUALS(hello_frame.session_id, uint64_t{0});
	TESTEQUALS(hello_frame.sequence, uint64_t{0});

	auto first_id = generate_session_id();
	auto second_id = generate_session_id();
	first_id != 0 or TESTFAIL;
	second_id != 0 or TESTFAIL;
	first_id != second_id or TESTFAIL;

	auto listener = TcpListener::listen("127.0.0.1", 0);
	std::exception_ptr server_error{};
	SessionIdentity server_identity{};
	std::jthread server{[&]() {
		try {
			auto connection = listener.accept();
			auto session = host_handshake(connection, 3, digest);
			session.has_value() or TESTFAIL;
			server_identity = session->get_identity();
		}
		catch (...) {
			server_error = std::current_exception();
		}
	}};

	auto connection = TcpConnection::connect("127.0.0.1", listener.get_port());
	auto client_session = client_handshake(connection, test_hello(digest));
	server.join();
	if (server_error) {
		std::rethrow_exception(server_error);
	}
	TESTEQUALS(client_session.get_identity().session_id, server_identity.session_id);
	TESTEQUALS(client_session.get_identity().player_id, gamestate::player_id_t{3});

	auto compatible_listener = TcpListener::listen("127.0.0.1", 0);
	std::exception_ptr compatibility_server_error{};
	std::jthread compatibility_server{[&]() {
		try {
			auto compatible_connection = compatible_listener.accept();
			auto session = host_handshake(compatible_connection, 3, digest);
			session.has_value() or TESTFAIL;
			TESTEQUALS(session->get_protocol_version().minor, PROTOCOL_MINIMUM_COMPATIBLE_MINOR);
		}
		catch (...) {
			compatibility_server_error = std::current_exception();
		}
	}};

	auto compatible_connection = TcpConnection::connect(
		"127.0.0.1", compatible_listener.get_port());
	auto compatible_hello = make_client_hello_frame(test_hello(digest));
	compatible_hello.protocol_minor = PROTOCOL_MINIMUM_COMPATIBLE_MINOR;
	compatible_connection.send_frame(compatible_hello);
	auto compatibility_response = compatible_connection.receive_frame();
	compatibility_response.has_value() or TESTFAIL;
	compatibility_response->message_type == MessageType::SERVER_ACCEPT or TESTFAIL;
	TESTEQUALS(compatibility_response->protocol_minor, PROTOCOL_MINIMUM_COMPATIBLE_MINOR);
	compatibility_server.join();
	if (compatibility_server_error) {
		std::rethrow_exception(compatibility_server_error);
	}

	auto reject_listener = TcpListener::listen("127.0.0.1", 0);
	bool host_rejected{};
	std::exception_ptr rejection_server_error{};
	std::jthread rejection_server{[&]() {
		try {
			auto rejected_connection = reject_listener.accept();
			auto session = host_handshake(rejected_connection, 3, digest);
			host_rejected = not session.has_value();
		}
		catch (...) {
			rejection_server_error = std::current_exception();
		}
	}};

	auto rejected_connection = TcpConnection::connect(
		"127.0.0.1", reject_listener.get_port());
	TESTTHROWS(client_handshake(rejected_connection, test_hello(test_digest(2))));
	rejection_server.join();
	if (rejection_server_error) {
		std::rethrow_exception(rejection_server_error);
	}
	host_rejected or TESTFAIL;
}

} // namespace openage::network::tests
