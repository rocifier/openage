# Copyright 2026 the openage authors. See copying.md for legal info.

# Minimal production build: cmake -S packaging/native_relay -B build/native-relay
cmake_minimum_required(VERSION 3.16)
project(fvd_native_relay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(VERSION_FULL_STRING "standalone" CACHE STRING "Relay build version")

include(CTest)
option(FVD_RELAY_PORTABLE_STATIC "Static musl relay for stock Pelican Python Generic eggs" OFF)
include("${CMAKE_CURRENT_LIST_DIR}/NativeRelay.cmake")
add_fvd_native_relay()

# Only the opt-in Pelican upload build links a wholly standalone static
# Linux binary. Build this in Alpine/musl *outside* the hosting container.
# No host library, dynamic loader, compiler or runtime installation needed.
if(FVD_RELAY_PORTABLE_STATIC)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Pelican portable static relay requires Linux/musl")
    endif()
    target_link_options(openage_native_relay PRIVATE -static)
endif()

install(TARGETS openage_native_relay RUNTIME DESTINATION bin)
