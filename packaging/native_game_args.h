// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <string>

namespace openage::native_game {

/** Validated packaged FVD game command-line options. */
struct Options {
	bool help = false;
	bool version = false;
	bool gl_debug = false;
	bool headless = false;
	bool solo = false;
	bool no_audio = false;
	bool vsync = false;
	int network_update_interval_ms = 50;
	int width = 1331;
	int height = 998;
	int verbosity = 0;
	std::string window_mode = "windowed";
	std::string asset_dir{};
	std::string cfg_dir{};
};

/**
 * Parse native game arguments before any engine/Qt initialization.
 * Throws std::invalid_argument on unknown or invalid options.
 */
Options parse_arguments(int argc, const char *const argv[]);

} // namespace openage::native_game
