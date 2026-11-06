// Copyright 2026 the openage authors. See copying.md for legal info.

#include "cvar.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>

#include "testing/testing.h"
#include "util/fslike/mounted_directories.h"

namespace openage::cvar::tests {

namespace {

struct TemporaryRoot {
	std::filesystem::path path;

	TemporaryRoot() :
		path{std::filesystem::temp_directory_path() /
		     ("fvd-native-cfg-" + std::to_string(
			     std::chrono::steady_clock::now().time_since_epoch().count()))} {
		std::filesystem::create_directories(this->path / "assets" / "fvd");
		std::filesystem::create_directories(this->path / "cfg");
	}

	~TemporaryRoot() {
		std::error_code ignored{};
		std::filesystem::remove_all(this->path, ignored);
	}
};

} // namespace

void native_config() {
	TemporaryRoot temporary{};
	const auto assets = temporary.path / "assets";
	const auto cfg = temporary.path / "cfg";
	{
		std::ofstream file{assets / "fvd" / "modpack.toml"};
		file << "[info]\nname = \"fvd\"\n";
	}
	{
		std::ofstream file{cfg / "keybinds.oac"};
		file << "# skipped comment\nset FVD_TEST first value\n"
		     << "load nested.oac\n";
	}
	{
		std::ofstream file{cfg / "nested.oac"};
		file << "set FVD_TEST final value\n"
		     << "load keybinds.oac\n"; // A recursive include must not loop.
	}

	auto root = std::make_shared<util::fslike::MountedDirectories>(
		assets.string(), cfg.string())->root();
	TESTEQUALS(root["assets"]["fvd"]["modpack.toml"].is_file(), true);
	TESTEQUALS(root["assets"].is_dir(), true);
	TESTEQUALS(root["cfg"]["keybinds.oac"].is_file(), true);
	TESTEQUALS(root["unknown"].is_dir(), false);
	TESTTHROWS(root["assets"]["not-writable.oac"].open_w());

	std::string value{};
	CVarManager manager{root["cfg"], true};
	TESTEQUALS(manager.create(
		"FVD_TEST",
		{[&]() { return value; }, [&](std::string next) { value = next; }}), true);
	manager.load_all();
	TESTEQUALS(value, std::string{"final value"});
	TESTEQUALS(root["cfg"].writable(), true);
}

} // namespace openage::cvar::tests
