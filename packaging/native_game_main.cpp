// Copyright 2026 the openage authors. See copying.md for legal info.

#include "native_game_args.h"
#include "fvd_engine_api_version.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_set>

#include <QCryptographicHash>
#include <QFile>
#include <QString>
#include <toml.hpp>

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

void validate_release_manifest(const fs::path &assets) {
	const auto manifest = assets / "fvd-native-manifest.toml";
	if (not fs::is_regular_file(manifest)) {
#if FVD_NATIVE_RELEASE
		throw std::runtime_error{
			"Packaged FVD release integrity manifest missing: " + manifest.string()};
#else
		// Non-release developer checkouts can use tracked source assets.
		return;
#endif
	}

	const auto definition = toml::parse(manifest.string());
	const auto metadata = toml::find<toml::table>(definition, "fvd_native");
	if (metadata.at("format").as_integer() != 1 or
	    toml::get<std::string>(metadata.at("api_version")) != openage::native_game::engine_api_version) {
		throw std::runtime_error{"Unsupported FVD release manifest: " + manifest.string()};
	}

	const auto entries = toml::find<toml::array>(definition, "file");
	std::unordered_set<std::string> checked{};
	for (const auto &entry : entries) {
		const auto &item = entry.as_table();
		const auto relative_text = toml::get<std::string>(item.at("path"));
		const auto expected = toml::get<std::string>(item.at("sha256"));
		const fs::path relative{relative_text};
		if (relative.empty() or relative.is_absolute() or expected.size() != 64
		    or not checked.insert(relative_text).second) {
			throw std::runtime_error{"Invalid or duplicate FVD asset manifest entry: " + relative_text};
		}
		for (const auto &part : relative) {
			if (part == ".." or part == ".") {
				throw std::runtime_error{"Invalid FVD manifest traversal: " + relative_text};
			}
		}
		const auto asset = assets / relative;
#if defined(_WIN32)
		QFile file{QString::fromStdWString(asset.wstring())};
#else
		QFile file{QString::fromStdString(asset.string())};
#endif
		if (not file.open(QIODevice::ReadOnly)) {
			throw std::runtime_error{"Missing or unreadable FVD release asset: " + asset.string()};
		}
		QCryptographicHash hasher{QCryptographicHash::Sha256};
		if (not hasher.addData(&file) or hasher.result().toHex().toStdString() != expected) {
			throw std::runtime_error{"FVD release asset checksum mismatch: " + asset.string()};
		}
	}

	for (const char *required : {
	         "fvd/modpack.toml",
	         "fvd/data/scenario.nyan",
	         "converted/engine/modpack.toml",
	         "converted/engine/manifest.toml"}) {
		if (not checked.contains(required)) {
			throw std::runtime_error{"Incomplete FVD release manifest (missing " +
			                         std::string{required} + ")"};
		}
	}
}

void validate_assets(const fs::path &assets) {
	const auto fvd = assets / "fvd" / "modpack.toml";
	if (not fs::is_regular_file(fvd)) {
		throw std::runtime_error{"FVD modpack missing from " + assets.string()};
	}
	const auto fvd_info = toml::find<toml::table>(toml::parse(fvd.string()), "info");
	if (fvd_info.at("name").as_string() != "fvd") {
		throw std::runtime_error{"FVD modpack has an incompatible identity: " + fvd.string()};
	}

	// Source checkouts own the API under fvd/engine_api. Native FSLike maps
	// the legacy nyan mount into that tracked source without copying files.
	const auto source_api = assets / "fvd" / "engine_api";
	const auto engine_dir = fs::is_regular_file(source_api / "modpack.toml")
		? source_api : assets / "converted" / "engine";
	const auto engine = engine_dir / "modpack.toml";
	if (not fs::is_regular_file(engine) or
	    not fs::is_regular_file(engine_dir / "manifest.toml")) {
		throw std::runtime_error{
			"Tracked nyan API or manifest is missing from " + engine_dir.string()
			+ "; restore the tracked source snapshot or package files"};
	}
	const auto api = toml::find<toml::table>(toml::parse(engine.string()), "info");
	const auto actual_version = toml::get<std::string>(api.at("version"));
	if (api.at("name").as_string() != "engine" or
	    actual_version != openage::native_game::engine_api_version) {
		throw std::runtime_error{
			"Incompatible tracked nyan API at " + engine.string()
			+ " (expected engine version " + std::string{openage::native_game::engine_api_version}
			+ ", found " + actual_version + ")"};
	}
	std::size_t nyan_count = 0;
	for (const auto &entry : fs::recursive_directory_iterator(engine_dir)) {
		if (entry.is_regular_file() and entry.path().extension() == ".nyan") {
			++nyan_count;
		}
	}
	if (nyan_count != openage::native_game::engine_api_nyan_files) {
		throw std::runtime_error{
			"Incomplete tracked nyan API at " + engine_dir.string()
			+ " (expected " + std::to_string(openage::native_game::engine_api_nyan_files)
			+ " files, found " + std::to_string(nyan_count) + ")"};
	}
	validate_release_manifest(assets);
}

void print_help() {
	std::cout
		<< "Usage: openage-native [options]\n"
		<< "  --asset-dir PATH                  Packaged FVD assets override\n"
		<< "  --verify-assets                   Validate package integrity, then exit\n"
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
	if (options.verify_assets) {
		std::cout << "FVD native release assets verified: " << assets.string() << '\n';
		return 0;
	}

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
