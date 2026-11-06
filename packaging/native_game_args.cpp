// Copyright 2026 the openage authors. See copying.md for legal info.

#include "native_game_args.h"

#include <charconv>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace openage::native_game {

namespace {

int positive_number(std::string_view value, std::string_view name, int maximum) {
	unsigned int number{};
	const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
	if (value.empty() or parsed.ec != std::errc{} or parsed.ptr != value.data() + value.size()
	    or number == 0 or number > static_cast<unsigned int>(maximum)) {
		throw std::invalid_argument{"invalid " + std::string{name} + ": " + std::string{value}};
	}
	return static_cast<int>(number);
}

std::string_view option_value(std::string_view argument, int &index,
                              int argc, const char *const argv[]) {
	const auto equals = argument.find('=');
	if (equals != std::string_view::npos) {
		if (equals + 1 == argument.size()) {
			throw std::invalid_argument{"missing value for " + std::string{argument.substr(0, equals)}};
		}
		return argument.substr(equals + 1);
	}
	if (index + 1 >= argc or argv[index + 1][0] == '-') {
		throw std::invalid_argument{"missing value for " + std::string{argument}};
	}
	return argv[++index];
}

} // namespace

Options parse_arguments(int argc, const char *const argv[]) {
	Options options{};
	for (int index = 1; index < argc; ++index) {
		std::string_view arg{argv[index]};
		if (arg == "--help" or arg == "-h") {
			options.help = true;
			return options;
		}
		if (arg == "--version" or arg == "-V") {
			options.version = true;
			return options;
		}
		if (arg == "--gl-debug") {
			options.gl_debug = true;
		}
		else if (arg == "--headless") {
			options.headless = true;
		}
		else if (arg == "--solo" or arg == "--single-player") {
			options.solo = true;
		}
		else if (arg == "--no-audio") {
			options.no_audio = true;
		}
		else if (arg == "--vsync") {
			options.vsync = true;
		}
		else if (arg == "-v" or arg == "--verbose") {
			++options.verbosity;
		}
		else if (arg == "-q" or arg == "--quiet") {
			--options.verbosity;
		}
		else {
			const auto name = arg.substr(0, arg.find('='));
			if (name == "--asset-dir") {
				options.asset_dir = option_value(arg, index, argc, argv);
			}
			else if (name == "--cfg-dir") {
				options.cfg_dir = option_value(arg, index, argc, argv);
			}
			else if (name == "--network-update-interval-ms") {
				options.network_update_interval_ms = positive_number(
					option_value(arg, index, argc, argv), name, 60000);
			}
			else if (name == "--window-mode") {
				options.window_mode = option_value(arg, index, argc, argv);
				if (options.window_mode != "windowed"
				    and options.window_mode != "fullscreen"
				    and options.window_mode != "borderless") {
					throw std::invalid_argument{"invalid --window-mode: " + options.window_mode};
				}
			}
			else if (name == "--window-size") {
				if (arg.find('=') != std::string_view::npos) {
					throw std::invalid_argument{"--window-size requires separate WIDTH HEIGHT values"};
				}
				options.width = positive_number(option_value(arg, index, argc, argv), name, 16384);
				options.height = positive_number(option_value(arg, index, argc, argv), name, 16384);
			}
			else if (name == "--modpacks") {
				const auto value = option_value(arg, index, argc, argv);
				if (value != "fvd") {
					throw std::invalid_argument{"native FVD supports only --modpacks fvd"};
				}
			}
			else {
				throw std::invalid_argument{"unknown game option: " + std::string{arg}};
			}
		}
	}
	return options;
}

} // namespace openage::native_game
