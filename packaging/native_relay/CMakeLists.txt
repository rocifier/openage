# Copyright 2026 the openage authors. See copying.md for legal info.

# Minimal production build: cmake -S packaging/native_relay -B build/native-relay
cmake_minimum_required(VERSION 3.16)
project(fvd_native_relay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(VERSION_FULL_STRING "standalone" CACHE STRING "Relay build version")

include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/NativeRelay.cmake")
add_fvd_native_relay()

install(TARGETS openage_native_relay RUNTIME DESTINATION bin)
