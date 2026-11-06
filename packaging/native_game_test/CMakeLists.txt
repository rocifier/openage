# Copyright 2026 the openage authors. See copying.md for legal info.

# Validate native gameplay argument handling without configuring Qt or Python.
cmake_minimum_required(VERSION 3.16)
project(fvd_native_game_args LANGUAGES CXX)

include(CTest)
add_executable(fvd_native_game_args_test
	../native_game_args.cpp
	../native_game_args_test.cpp)
target_compile_features(fvd_native_game_args_test PRIVATE cxx_std_20)
add_test(NAME fvd_native_game_args COMMAND fvd_native_game_args_test)

add_test(NAME fvd_native_manifest
	COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_LIST_DIR}/../native_game/NativeManifestTest.cmake")
