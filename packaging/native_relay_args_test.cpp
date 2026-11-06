// Copyright 2026 the openage authors. See copying.md for legal info.

#include "native_relay_args.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

openage::native_relay::Options parse(const std::vector<std::string> &args) {
	std::vector<const char *> argv{};
	argv.reserve(args.size());
	for (const auto &arg : args) {
		argv.push_back(arg.c_str());
	}
	return openage::native_relay::parse_arguments(static_cast<int>(argv.size()), argv.data());
}

void expect(bool condition) {
	if (not condition) {
		throw std::runtime_error{"native relay CLI test failed"};
	}
}

void rejects(const std::vector<std::string> &args) {
	try {
		parse(args);
	}
	catch (const std::invalid_argument &) {
		return;
	}
	throw std::runtime_error{"invalid native relay CLI was accepted"};
}

} // namespace

int main() {
	auto defaults = parse({"openage-relay"});
	expect(defaults.listen == "0.0.0.0");
	expect(defaults.port == 32456);
	expect(defaults.max_rooms == 64);
	expect(defaults.max_connections == 128);
	expect(defaults.player_tokens.empty());

	const std::string token_a(63, 'a');
	const std::string token_b(63, 'b');
	auto custom = parse({"openage-relay", "--listen=::1", "--port", "23456",
	                     "--max-rooms=4096", "--max-connections", "512",
	                     "--player-token", token_a + "A", "--player-token=" + token_b + "B"});
	expect(custom.listen == "::1");
	expect(custom.port == 23456);
	expect(custom.max_rooms == 4096);
	expect(custom.max_connections == 512);
	expect(custom.player_tokens.size() == 2);
	expect(custom.player_tokens[0].size() == 32);
	expect(custom.player_tokens[1].size() == 32);

	expect(parse({"openage-relay", "--help"}).help);
	expect(parse({"openage-relay", "--version"}).version);

	rejects({"openage-relay", "--port", "0"});
	rejects({"openage-relay", "--port", "65536"});
	rejects({"openage-relay", "--port", "-1"});
	rejects({"openage-relay", "--port", "80suffix"});
	rejects({"openage-relay", "--port"});
	rejects({"openage-relay", "--listen="});
	rejects({"openage-relay", "--max-rooms", "4097"});
	rejects({"openage-relay", "--max-rooms", "0"});
	rejects({"openage-relay", "--max-connections", "0"});
	rejects({"openage-relay", "--max-connections", "999999999999999999999999999999"});
	rejects({"openage-relay", "--unknown"});
	rejects({"openage-relay", "--player-token", "xyz"});
	rejects({"openage-relay", "--player-token", std::string(64, '0')});
	rejects({"openage-relay", "--player-token", std::string(63, 'f') + "z"});
	rejects({"openage-relay", "--player-token", token_a + "A",
	         "--player-token", token_a + "a"});
	return 0;
}
