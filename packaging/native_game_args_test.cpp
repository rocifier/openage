// Copyright 2026 the openage authors. See copying.md for legal info.

#include "native_game_args.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

openage::native_game::Options parse(const std::vector<std::string> &args) {
	std::vector<const char *> argv{};
	for (const auto &arg : args) {
		argv.push_back(arg.c_str());
	}
	return openage::native_game::parse_arguments(static_cast<int>(argv.size()), argv.data());
}

void require(bool ok) {
	if (not ok) {
		throw std::runtime_error{"native FVD game argument test failed"};
	}
}

void reject(const std::vector<std::string> &args) {
	try {
		parse(args);
	}
	catch (const std::invalid_argument &) {
		return;
	}
	throw std::runtime_error{"invalid native game arguments were accepted"};
}

} // namespace

int main() {
	auto defaults = parse({"openage-native"});
	require(defaults.width == 1331 and defaults.height == 998);
	require(defaults.network_update_interval_ms == 50);
	require(defaults.vsync and defaults.max_fps == 0);
	require(defaults.window_mode == "windowed");
	require(not defaults.solo and not defaults.headless and not defaults.no_audio);

	auto options = parse({"openage-native", "--asset-dir=example/assets",
	                      "--cfg-dir", "example/cfg", "--gl-debug", "--headless",
	                      "--solo", "--no-audio", "--no-vsync", "--max-fps", "144",
	                      "--network-update-interval-ms=17", "--window-size",
	                      "1920", "1080", "--window-mode", "borderless",
	                      "--modpacks", "fvd", "-v", "-q", "-v"});
	require(options.asset_dir == "example/assets" and options.cfg_dir == "example/cfg");
	require(options.gl_debug and options.headless and options.solo);
	require(options.no_audio and not options.vsync and options.max_fps == 144);
	require(options.width == 1920 and options.height == 1080);
	require(options.network_update_interval_ms == 17);
	require(options.window_mode == "borderless" and options.verbosity == 1);
	require(parse({"openage-native", "--help"}).help);
	require(parse({"openage-native", "--version"}).version);
	require(parse({"openage-native", "--verify-assets"}).verify_assets);
	require(parse({"openage-native", "--single-player"}).solo);
	require(parse({"openage-native", "--no-vsync", "--vsync"}).vsync);

	reject({"openage-native", "--window-mode", "floating"});
	reject({"openage-native", "--window-size", "800"});
	reject({"openage-native", "--window-size", "800x600"});
	reject({"openage-native", "--window-size", "-1", "768"});
	reject({"openage-native", "--window-size", "100", "0"});
	reject({"openage-native", "--window-size", "1", "999999"});
	reject({"openage-native", "--network-update-interval-ms", "0"});
	reject({"openage-native", "--network-update-interval-ms", "10ms"});
	reject({"openage-native", "--network-update-interval-ms"});
	reject({"openage-native", "--max-fps", "0"});
	reject({"openage-native", "--max-fps", "1001"});
	reject({"openage-native", "--max-fps"});
	reject({"openage-native", "--cfg-dir="});
	reject({"openage-native", "--modpacks", "aoe2"});
	reject({"openage-native", "--unknown"});
	return 0;
}
