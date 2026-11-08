// Copyright 2015-2024 the openage authors. See copying.md for legal info.

#include "main.h"

#include <algorithm>
#include <chrono>
#include <memory>

#include "cvar/cvar.h"
#include "engine/engine.h"
#include "util/timer.h"

namespace openage {

/*
 * This function is called from Cython, see main_cpp.pyx.
 *
 * This is the main entry point to the C++ part.
 */
int run_game(const main_arguments &args) {
	// TODO: store args.gl_debug as default in the cvar system.

	util::Timer timer;
	timer.start();

	// read and apply the configuration files
	auto cvar_manager = std::make_shared<cvar::CVarManager>(args.root_path["cfg"], args.native_runtime);
	cvar_manager->load_all();

	// set engine run_mode
	openage::engine::Engine::mode run_mode = openage::engine::Engine::mode::FULL;
	if (args.headless) {
		run_mode = openage::engine::Engine::mode::HEADLESS;
	}

	// convert window arguments to window settings
	renderer::window_settings win_settings = {};
	win_settings.width = args.window_args.width;
	win_settings.height = args.window_args.height;
	win_settings.vsync = args.window_args.vsync;
	win_settings.max_fps = args.window_args.max_fps;

	renderer::window_mode wmode;
	if (args.window_args.mode == "fullscreen") {
		wmode = renderer::window_mode::FULLSCREEN;
	}
	else if (args.window_args.mode == "borderless") {
		wmode = renderer::window_mode::BORDERLESS;
	}
	else if (args.window_args.mode == "windowed") {
		wmode = renderer::window_mode::WINDOWED;
	}
	else {
		throw Error(MSG(err) << "Invalid window mode: " << args.window_args.mode);
	}
	win_settings.mode = wmode;
	win_settings.debug = args.gl_debug;

	openage::engine::Engine engine{run_mode, args.root_path, args.mods, win_settings, not args.solo and not args.headless and std::find(args.mods.begin(), args.mods.end(), "fvd") != args.mods.end(), std::chrono::milliseconds{args.network_update_interval_ms}, not args.no_audio, args.native_runtime};

	engine.loop();

	return 0;
}

} // namespace openage
