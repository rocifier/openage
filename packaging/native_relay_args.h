// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace openage::native_relay {

/**
 * Options accepted by the standalone relay. Tokens are decoded to 32 raw bytes.
 */
struct Options {
	std::string listen = "0.0.0.0";
	uint16_t port = 32456;
	std::vector<std::string> player_tokens{};
	size_t max_rooms = 64;
	size_t max_connections = 128;
	bool help = false;
	bool version = false;
};

/**
 * Parse and validate the native relay command line before opening a listener.
 * Throws std::invalid_argument for malformed options.
 */
Options parse_arguments(int argc, const char *const argv[]);

} // namespace openage::native_relay
