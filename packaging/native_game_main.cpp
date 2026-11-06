// Copyright 2026 the openage authors. See copying.md for legal info.

#include "native_game_args.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
	#define NOMINMAX
	#include <windows.h>
#elif defined(__APPLE__)
	#include <mach-o/dyld.h>
#else
	#include <unistd.h>
#endif

#include "log/log.h"
#include "log/stdout_logsink.h"
#include "main.h"
#include "util/fslike/mounted_directories.h"
#include "version.h"

namespace {

namespace fs = std::filesystem;

fs::path executable_path() {
#if defined(_WIN32)
	std::vector<wchar_t> buffer(1024);
	while (true) {
		const auto length = GetModuleFileNameW(
			nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0) {
			throw std::runtime_error{"Cannot locate native FVD executable"};
		}
		if (length < buffer.size()) {
			return fs::path{std::wstring(buffer.data(), length)};
		}
		buffer.resize(buffer.size() * 2);
	}
#elif defined(__APPLE__)
	uint32_t length{};
	_NSGetExecutablePath(nullptr, &length);
	std::vector<char> buffer(length + 1);
	if (_NSGetExecutablePath(buffer.data(), &length) != 0) {
		throw std::runtime_error{"Cannot locate native FVD executable"};
	}
	return fs::weakly_canonical(buffer.data());
#else
	std::vector<char> buffer(1024);
	while (true) {
		const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
		if (length < 0) {
			throw std::runtime_error{"Cannot locate native FVD executable"};
		}
		if (static_cast<size_t>(length) < buffer.size()) {
			return fs::path{std::string(buffer.data(), length)};
		}
		buffer.resize(buffer.size() * 2);
	}
#endif
}

fs::path installed_asset_dir(const fs::path &exe) {
	const auto dir = exe.parent_path();
	for (const auto &candidate : {
	         dir / "share" / "openage",
	         dir.parent_path() / "share" / "openage",
	         dir / "assets",
	         dir.parent_path() / "assets"}) {
		if (fs::is_regular_file(candidate / "fvd" / "modpack.toml")) {
			return fs::absolute(candidate).lexically_normal();
		}
	}
	throw std::runtime_error{
		"Cannot find FVD assets relative to the executable; use --asset-dir"};
}

fs::path user_config_dir() {
#if defined(_WIN32)
	if (const char *appdata = std::getenv("APPDATA")) {
		return fs::path{appdata} / "openage";
	}
#else
	if (const char *xdg = std::getenv("XDG_CONFIG_HOME")) {
		if (xdg[0] != '\0') {
			return fs::path{xdg} / "openage";
		}
	}
#endif
	if (const char *home = std::getenv(
#if defined(_WIN32)
		"USERPROFILE"
#else
		"HOME"
#endif
		)) {
#if defined(__APPLE__)
		return fs::path{home} / "Library" / "Application Support" / "openage";
#else
		return fs::path{home} / ".config" / "openage";
#endif
	}
	throw std::runtime_error{"Cannot find user configuration directory; use --cfg-dir"};
}

void seed_default_config(const fs::path &exe, const fs::path &cfg) {
	const auto dir = exe.parent_path();
	for (const auto &candidate : {
	         dir / "etc" / "openage",
	         dir.parent_path() / "etc" / "openage"}) {
		if (not fs::is_directory(candidate)) {
			continue;
		}
		for (const char *name : {"audio.oac", "keybinds.oac"}) {
			const auto source = candidate / name;
			const auto destination = cfg / name;
			if (fs::is_regular_file(source) and not fs::exists(destination)) {
				fs::copy_file(source, destination, fs::copy_options::skip_existing);
			}
		}
		break;
	}
}

void validate_assets(const fs::path &assets) {
	if (not fs::is_regular_file(assets / "fvd" / "modpack.toml")) {
		throw std::runtime_error{"FVD modpack missing from " + assets.string()};
	}
	const auto engine = assets / "converted" / "engine" / "modpack.toml";
	if (not fs::is_regular_file(engine)) {
		throw std::runtime_error{
			"Exported nyan API is missing at " + engine.string()
			+ "; export assets during release preparation, not at runtime"};
	}
}

void print_help() {
	std::cout
		<< "Usage: openage-native [options]\n"
		<< "  --asset-dir PATH                  Packaged FVD assets override\n"
		<< "  --cfg-dir PATH                    Writable configuration override\n"
		<< "  --solo, --single-player           Skip the online lobby\n"
		<< "  --headless                        Run without graphics\n"
		<< "  --gl-debug                        Enable GL diagnostics\n"
		<< "  --no-audio                        Disable sound\n"
		<< "  --network-update-interval-ms MS   Replication interval (default: 50)\n"
		<< "  --window-size WIDTH HEIGHT       Window size (default: 1331 998)\n"
		<< "  --window-mode MODE               windowed, borderless or fullscreen\n"
		<< "  --vsync                           Enable vertical synchronization\n"
		<< "  --modpacks fvd                    FVD only\n"
		<< "  -v, --verbose / -q, --quiet       Logging verbosity\n"
		<< "  -h, --help / -V, --version        Show information and exit\n";
}

int run(const openage::native_game::Options &options) {
	(void) openage::log::global_stdoutsink();
	if (options.verbosity > 0) {
		openage::log::set_level(openage::log::level::dbg);
	}
	else if (options.verbosity < 0) {
		openage::log::set_level(openage::log::level::warn);
	}

	const auto exe = executable_path();
	const auto assets = options.asset_dir.empty()
		? installed_asset_dir(exe)
		: fs::absolute(options.asset_dir).lexically_normal();
	validate_assets(assets);

	const auto cfg = options.cfg_dir.empty()
		? user_config_dir()
		: fs::absolute(options.cfg_dir).lexically_normal();
	fs::create_directories(cfg);
	seed_default_config(exe, cfg);

	openage::main_arguments args{};
	args.root_path = std::make_shared<openage::util::fslike::MountedDirectories>(
		assets.string(), cfg.string())->root();
	args.gl_debug = options.gl_debug;
	args.headless = options.headless;
	args.solo = options.solo;
	args.no_audio = options.no_audio;
	args.network_update_interval_ms = options.network_update_interval_ms;
	args.mods = {"fvd"};
	args.window_args = {options.width, options.height, options.vsync, options.window_mode};
	args.native_runtime = true;

	openage::log::log(INFO << "Launching FVD native game from " << assets.string());
	return openage::run_game(args);
}

} // namespace

int main(int argc, const char *argv[]) {
	try {
		const auto options = openage::native_game::parse_arguments(argc, argv);
		if (options.help) {
			print_help();
			return 0;
		}
		if (options.version) {
			std::cout << "openage-native " << openage::version::version << '\n';
			return 0;
		}
		return run(options);
	}
	catch (const std::exception &error) {
		std::cerr << "openage-native: " << error.what() << '\n';
		return 1;
	}
}
