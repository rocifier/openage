# Copyright 2026 the openage authors. See copying.md for legal info.

# Build the relay CLI parser and tests without configuring the Python/Cython
# engine build. This is also usable by hosts that do not have Python installed.
cmake_minimum_required(VERSION 3.16)
project(fvd_native_relay_cli_test LANGUAGES CXX)

enable_testing()

add_executable(fvd_native_relay_cli_test
	../native_relay_args.cpp
	../native_relay_args_test.cpp
)
target_compile_features(fvd_native_relay_cli_test PRIVATE cxx_std_20)
add_test(NAME fvd_native_relay_cli_test COMMAND fvd_native_relay_cli_test)
