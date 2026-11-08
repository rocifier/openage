# Copyright 2013-2023 the openage authors. See copying.md for legal info.

cmake_minimum_required(VERSION 3.16)


# Fast Villager Defense: native C++ game, relay and C++ tests.
# The original-game Python/Cython converter and generic upstream launcher
# are intentionally not part of this build.
message(STATUS "Configuring Fast Villager Defense native C++")

##################################################
# main buildsystem setup entry point
project(openage CXX)

# C++ standard requirement
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The FVD engine, relay, tests and distribution are native C++ only.
# Optional pure Python developer tests run separately (never during CMake).
option(FVD_NATIVE_RELEASE "Package the Python-free native FVD runtime" OFF)

# CMake policies
foreach(pol
        CMP0074  # use <pkg>_ROOT vars in find_package()
        CMP0067  # honor language standard in try_compile()
        CMP0071  # enable automoc for generated files
        CMP0072  # prefers GLVND by default FindOpenGL
        CMP0048  # project() command manages VERSION variables
        CMP0082  # run add_subdirectory() in the declaration order
        CMP0102  # Don't create empty cache entries
       )
	if (POLICY ${pol})
		cmake_policy(SET ${pol} NEW)
	endif()
endforeach()

# don't print 'Built target ...' messages
# upstream since cmake v3.4.0-rc1 (by commit 1d3984780df8)
set_property(GLOBAL PROPERTY TARGET_MESSAGES OFF)

# Ensure CMAKE_BUILD_TYPE is set correctly.
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug")
endif()
string(TOUPPER "CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}" BUILD_TYPE_CXX_FLAGS)


##################################################
# Native options: controlled directly by CMake and build-fvd wrappers.
if(NOT DEFINED WANT_BACKTRACE)
	set(WANT_BACKTRACE if_available)
endif()

if(NOT DEFINED WANT_INOTIFY)
	set(WANT_INOTIFY if_available)
endif()

if(NOT DEFINED WANT_OPENGL)
	set(WANT_OPENGL if_available)
endif()

if(NOT DEFINED WANT_VULKAN)
	set(WANT_VULKAN if_available)
endif()

if(NOT DEFINED WANT_GPERFTOOLS_PROFILER)
	set(WANT_GPERFTOOLS_PROFILER if_available)
endif()

if(NOT DEFINED WANT_GPERFTOOLS_TCMALLOC)
	set(WANT_GPERFTOOLS_TCMALLOC false)
endif()

if(NOT DEFINED WANT_NCURSES)
	set(WANT_NCURSES if_available)
endif()

if(NOT DEFINED WANT_IWYU)
	set(WANT_IWYU false)
endif()

##################################################
# static content filesystem locations
if(NOT DEFINED GLOBAL_ASSET_DIR)
	set(ASSET_DIR "share/openage")
	if(MSVC)
		set(GLOBAL_ASSET_DIR "${ASSET_DIR}")
	else()
		set(GLOBAL_ASSET_DIR "${CMAKE_INSTALL_PREFIX}/${ASSET_DIR}")
	endif()
endif()

if(NOT DEFINED GLOBAL_CONFIG_DIR)
	set(CONFIG_DIR "etc/openage")
	if(MSVC)
		set(GLOBAL_CONFIG_DIR "${CONFIG_DIR}")
	else()
		set(GLOBAL_CONFIG_DIR "${CMAKE_INSTALL_PREFIX}/${CONFIG_DIR}")
	endif()
endif()


##################################################
# ccache setup

# distros can also do this but they don't use this mechanism
option(ENABLE_CCACHE "prefix each compile command with ccache")

if(ENABLE_CCACHE)
	find_program(CCACHE_FOUND "ccache")

	if(CCACHE_FOUND)
		set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
		set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
	else()
		message(FATAL_ERROR "ccache not found, but you requested it")
	endif(CCACHE_FOUND)
endif()


##################################################
# clang tidy static analysis
option(
	ENABLE_CLANG_TIDY
	"activate clang tidy messages"
	OFF
)
if(ENABLE_CLANG_TIDY)
	set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=-*,readability-*")
endif()


# option processing is now done.

##################################################
# include buildsystem features

# add search paths to helper modules
set(BUILDSYSTEM_DIR "${CMAKE_SOURCE_DIR}/buildsystem")
set(CMAKE_MODULE_PATH "${BUILDSYSTEM_DIR}" "${BUILDSYSTEM_DIR}/modules/")

# prioritize macOS frameworks since they're probably newer
# than the system libraries
set(CMAKE_FIND_FRAMEWORK LAST)
set(CMAKE_FIND_APPBUNDLE LAST)

# load helper modules
include(GNUInstallDirs)
include(CheckInSourceBuild)
include(HandleCXXOptions)
include(CheckCompilerFeatures)
include(CMakeParseArguments)
include(DetectProjectVersion)
include(DependencyFetch)
include(FindPackageHandleStandardArgs)

# include build configuration modules
include(CTest)

# initialize language support
include(cpp)
include(doxygen)
include(options)
include(util)


# now that all modules and settings are loaded,
# apply those to the project.


##################################################
# set project version
if(USED_GIT_VERSION)
	# VERSION_FULL_STRING is the full git describe
	set(VERSION_FULL_STRING "${PROJECT_VERSION}")
	# Increment PATCH by the number of commits since the version tag.
	if(VERSION_FULL_STRING MATCHES "^[^0-9]*([0-9]+)\\.([0-9]+)\\.([0-9]+)-([0-9]+)-g[a-f0-9]+$")
		set(VERSION_MAJOR "${CMAKE_MATCH_1}")
		set(VERSION_MINOR "${CMAKE_MATCH_2}")
		math(EXPR VERSION_PATCH "${CMAKE_MATCH_3} + ${CMAKE_MATCH_4}")
		set(PROJECT_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
	else()
		message(FATAL_ERROR "Unsupported git version: ${VERSION_FULL_STRING}")
	endif()
endif()
if(NOT VERSION_FULL_STRING)
	set(VERSION_FULL_STRING "${PROJECT_VERSION}")
endif()
project(openage VERSION "${PROJECT_VERSION}")

# set CI version
if(DEFINED ENV{CI_CFG_VERSION})
    set(CI_CFG_VERSION "$ENV{CI_CFG_VERSION}")
else()
    set(CI_CFG_VERSION "NOT SET")
endif()


##################################################
# documentation generation

# create documentation
doxygen_configure(libopenage/ openage/ doc/ README.md)


##################################################
# static content
add_subdirectory(assets/)


##################################################
# C++ content
add_subdirectory(libopenage/)


##################################################
# Python extensions and C++ code generation were retired from FVD.
# Use python -m unittest for optional pure Python tests (no CMake).

##################################################
# packaging.

# Ensure that packaging is always the last step.
add_subdirectory(packaging)


##################################################
# show build configuration overview

message("")
print_config_options()

message("${PROJECT_NAME} ${PROJECT_VERSION}

   version string | ${VERSION_FULL_STRING}
         compiler | ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}
       build type | ${CMAKE_BUILD_TYPE}
         cxxflags | ${CMAKE_CXX_FLAGS} ${EXTRA_FLAGS}
 build type flags | ${${BUILD_TYPE_CXX_FLAGS}}
        build dir | ${CMAKE_BINARY_DIR}
   install prefix | ${CMAKE_INSTALL_PREFIX}
")

##################################################
# done! that was easy, right?
