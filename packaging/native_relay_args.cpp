// Copyright 2026 the openage authors. See copying.md for legal info.

#include "native_relay_args.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace openage::native_relay {

namespace {

size_t parse_positive_number(std::string_view value, std::string_view option, size_t maximum) {
	unsigned long long number{};
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
	if (value.empty() or parsed.ec != std::errc{} or parsed.ptr != value.data() + value.size()
	    or number == 0 or number > maximum) {
		throw std::invalid_argument{"invalid " + std::string{option} + ": " + std::string{value}};
	}
	return static_cast<size_t>(number);
}

int hex_digit(char digit) {
	if (digit >= '0' and digit <= '9') {
		return digit - '0';
	}
	if (digit >= 'a' and digit <= 'f') {
		return digit - 'a' + 10;
	}
	if (digit >= 'A' and digit <= 'F') {
		return digit - 'A' + 10;
	}
	return -1;
}

std::string parse_token(std::string_view value) {
	if (value.size() != 64) {
		throw std::invalid_argument{"relay player tokens must contain exactly 32 bytes (64 hexadecimal characters)"};
	}

	std::string bytes(32, '\0');
	bool nonzero = false;
	for (size_t index = 0; index < bytes.size(); ++index) {
		const int high = hex_digit(value[index * 2]);
		const int low = hex_digit(value[index * 2 + 1]);
		if (high < 0 or low < 0) {
			throw std::invalid_argument{"relay player tokens must be hexadecimal"};
		}
		const auto byte = static_cast<char>((high << 4) | low);
		bytes[index] = byte;
		nonzero |= byte != 0;
	}
	if (not nonzero) {
		throw std::invalid_argument{"relay player tokens must not be all zero"};
	}
	return bytes;
}

} // namespace

Options parse_arguments(int argc, const char *const argv[]) {
	Options options{};
	for (int index = 1; index < argc; ++index) {
		std::string_view argument{argv[index]};
		if (argument == "--help" or argument == "-h") {
			options.help = true;
			return options;
		}
		if (argument == "--version") {
			options.version = true;
			return options;
		}

		const auto equals = argument.find('=');
		const auto name = argument.substr(0, equals);
		if (name != "--listen" and name != "--port" and name != "--player-token"
		    and name != "--max-rooms" and name != "--max-connections") {
			throw std::invalid_argument{"unknown relay option: " + std::string{argument}};
		}

		std::string_view value{};
		if (equals != std::string_view::npos) {
			value = argument.substr(equals + 1);
		}
		else if (index + 1 < argc) {
			value = argv[++index];
		}
		else {
			throw std::invalid_argument{"missing value for " + std::string{name}};
		}
		if (value.empty()) {
			throw std::invalid_argument{"empty value for " + std::string{name}};
		}

		if (name == "--listen") {
			options.listen = value;
		}
		else if (name == "--port") {
			options.port = static_cast<uint16_t>(parse_positive_number(value, name, 65535));
		}
		else if (name == "--max-rooms") {
			options.max_rooms = parse_positive_number(value, name, 4096);
		}
		else if (name == "--max-connections") {
			options.max_connections = parse_positive_number(value, name, std::numeric_limits<size_t>::max());
		}
		else {
			auto token = parse_token(value);
			if (std::find(options.player_tokens.begin(), options.player_tokens.end(), token) != options.player_tokens.end()) {
				throw std::invalid_argument{"relay player tokens must be unique"};
			}
			options.player_tokens.push_back(std::move(token));
		}
	}
	return options;
}

} // namespace openage::native_relay
