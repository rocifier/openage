// Copyright 2026 the openage authors. See copying.md for legal info.
// Native-only C++ test dispatcher: no Python, Cython or generated test list.
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>

#include "error/error.h"
#include "testing/native_test_runner.h"
#include "testing/testing.h"

namespace openage::coord::tests { void coord(); }
namespace openage::cvar::tests { void native_config(); }
namespace openage::network::tests {
void messages();
void protocol();
void handshake();
void command_mailbox();
void connection_mailbox();
void rehost_ticket();
void replication();
void world_delta();
void world_delta_flares();
}
namespace openage::testing::tests { void comparison(); }
namespace openage::renderer::resources::tests { void alpha_mask(); void texture_metadata(); void palette_colors(); }
namespace openage::util::tests { void fixed_point(); void matrix(); }

namespace {
struct Test {
	std::string_view name;
	void (*function)();
};

// Test registration is explicit C++ source. Add a test and its translation
// unit to packaging/CMakeLists.txt together; never regenerate this table.
constexpr Test tests[] = {
	{"openage::coord::tests::coord", &openage::coord::tests::coord},
	{"openage::cvar::tests::native_config", &openage::cvar::tests::native_config},
	{"openage::network::tests::messages", &openage::network::tests::messages},
	{"openage::network::tests::protocol", &openage::network::tests::protocol},
	{"openage::network::tests::handshake", &openage::network::tests::handshake},
	{"openage::network::tests::command_mailbox", &openage::network::tests::command_mailbox},
	{"openage::network::tests::connection_mailbox", &openage::network::tests::connection_mailbox},
	{"openage::network::tests::rehost_ticket", &openage::network::tests::rehost_ticket},
	{"openage::network::tests::replication", &openage::network::tests::replication},
	{"openage::network::tests::world_delta", &openage::network::tests::world_delta},
	{"openage::network::tests::world_delta_flares", &openage::network::tests::world_delta_flares},
	{"openage::testing::tests::comparison", &openage::testing::tests::comparison},
	{"openage::renderer::resources::tests::alpha_mask", &openage::renderer::resources::tests::alpha_mask},
	{"openage::renderer::resources::tests::texture_metadata", &openage::renderer::resources::tests::texture_metadata},
	{"openage::renderer::resources::tests::palette_colors", &openage::renderer::resources::tests::palette_colors},
	{"openage::util::tests::fixed_point", &openage::util::tests::fixed_point},
	{"openage::util::tests::matrix", &openage::util::tests::matrix},
};

void help() {
	std::cout << "Usage: openage-native-tests [--list | --all | --exact NAME | --prefix PREFIX]\n"
	             "  --list             List registered native C++ tests\n"
	             "  --exact NAME       Run one fully qualified test\n"
	             "  --prefix PREFIX    Run all tests starting with PREFIX\n"
	             "  --all              Run every registered native C++ test\n";
}

void diagnostic_failure() {
	// Exercise the failure path without registering a permanently failing test.
	TESTFAILMSG(__FILE__ << ":" << __LINE__ << ": native failure diagnostic");
}

int run(const Test &test) {
	std::cout << "[ RUN      ] " << test.name << std::endl;
	try {
		test.function();
		std::cout << "[       OK ] " << test.name << '\n';
		return 0;
	}
	catch (const openage::error::Error &error) {
		std::cerr << "[  FAILED  ] " << test.name << ": " << error.what() << '\n';
		std::cerr << error << '\n'; // preserved C++ exception and source location
	}
	catch (const std::exception &error) {
		std::cerr << "[  FAILED  ] " << test.name << ": " << error.what() << '\n';
	}
	catch (...) {
		std::cerr << "[  FAILED  ] " << test.name << ": unknown C++ exception\n";
	}
	return 1;
}
} // namespace

int openage::testing::native_test_main(int argc, char **argv) {
	if (argc == 2 && std::string_view{argv[1]} == "--verify-error-report") {
		return run({"native::failure_diagnostic", diagnostic_failure});
	}
	if (argc == 2 && std::string_view{argv[1]} == "--help") {
		help();
		return 0;
	}
	if (argc == 2 && std::string_view{argv[1]} == "--list") {
		for (const auto &test : tests) {
			std::cout << test.name << '\n';
		}
		return 0;
	}
	const bool all = argc == 2 && std::string_view{argv[1]} == "--all";
	const bool exact = argc == 3 && std::string_view{argv[1]} == "--exact";
	const bool prefix = argc == 3 && std::string_view{argv[1]} == "--prefix";
	if (!all && !exact && !prefix) {
		help();
		return 2;
	}

	const std::string_view wanted = all ? std::string_view{} : argv[2];
	int failed = 0;
	int matched = 0;
	for (const auto &test : tests) {
		const bool selected = all || (exact && test.name == wanted) ||
			(prefix && test.name.starts_with(wanted));
		if (!selected) {
			continue;
		}
		++matched;
		failed += run(test);
	}
	if (matched == 0) {
		std::cerr << "No registered native C++ test matched: " << wanted << '\n';
		return 2;
	}
	std::cout << "Native C++ tests: " << matched - failed << "/" << matched
	          << " passed\n";
	return failed ? 1 : 0;
}
