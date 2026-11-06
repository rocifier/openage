// Copyright 2026 the openage authors. See copying.md for legal info.

#include "native_relay_args.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <exception>
#include <iostream>
#include <thread>

#include "network/relay_service.h"

namespace {

volatile std::sig_atomic_t stopping = 0;

extern "C" void request_stop(int) {
	stopping = 1;
}

void print_help() {
	std::cout
		<< "Usage: openage-relay [options]\n"
		<< "  --listen ADDRESS        Listen address (default: 0.0.0.0)\n"
		<< "  --port PORT             TCP port (default: 32456)\n"
		<< "  --player-token HEX      Authorized 32-byte token, repeatable (default: public)\n"
		<< "  --max-rooms COUNT       Maximum advertised rooms (default: 64; max: 4096)\n"
		<< "  --max-connections COUNT Maximum concurrent connections (default: 128)\n"
		<< "  -h, --help              Show this help\n"
		<< "  --version               Show the engine version\n";
}

int run(const openage::native_relay::Options &options) {
	std::signal(SIGINT, request_stop);
	std::signal(SIGTERM, request_stop);

	std::exception_ptr service_error{};
	std::atomic_bool finished{false};
	std::thread service{[&]() {
		try {
			openage::network::run_relay_service(options.listen,
			                                   options.port,
			                                   options.player_tokens,
			                                   options.max_rooms,
			                                   options.max_connections);
		}
		catch (...) {
			service_error = std::current_exception();
		}
		finished.store(true, std::memory_order_release);
	}};

	// Signal handlers may only set a flag. Closing the listener is not
	// async-signal-safe, so request shutdown from this ordinary thread.
	// Repeating the request handles a signal received before listen() completes.
	while (not finished.load(std::memory_order_acquire)) {
		if (stopping) {
			openage::network::stop_relay_service();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds{50});
	}
	service.join();
	if (service_error) {
		std::rethrow_exception(service_error);
	}
	return 0;
}

} // namespace

int main(int argc, const char *argv[]) {
	try {
		auto options = openage::native_relay::parse_arguments(argc, argv);
		if (options.help) {
			print_help();
			return 0;
		}
		if (options.version) {
			std::cout << "openage-relay " << OPENAGE_NATIVE_RELAY_VERSION << '\n';
			return 0;
		}
		return run(options);
	}
	catch (const std::exception &error) {
		std::cerr << "openage-relay: " << error.what() << '\n';
		return 1;
	}
}
