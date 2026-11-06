# Copyright 2026 the openage authors. See copying.md for legal info.
#
# Separate install component for the Python-free FVD distribution.
# Qt/vendor runtime deployment and the release asset manifest are future gates.

set(FVD_NATIVE_API_ASSET_DIR "" CACHE PATH
	"Pre-exported engine API modpack directory; optional during development")

option(FVD_NATIVE_RELEASE
	"Require Python-free adapters and complete exported API when staging FVD" OFF)
if(FVD_NATIVE_RELEASE)
	if(FVD_WITH_PYTHON_ADAPTERS)
		message(FATAL_ERROR "FVD native releases must use -DFVD_WITH_PYTHON_ADAPTERS=OFF")
	endif()
	if(NOT FVD_NATIVE_API_ASSET_DIR)
		message(FATAL_ERROR "FVD native releases require -DFVD_NATIVE_API_ASSET_DIR=<exported engine>")
	endif()
endif()

install(TARGETS openage_native_game openage_native_relay
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT FVDNative)
install(TARGETS libopenage
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT FVDNative
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT FVDNative
	ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT FVDNative)

# The presenter also uses the legacy missing-texture test asset.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/fvd/"
	DESTINATION "${ASSET_DIR}/fvd"
	COMPONENT FVDNative
	PATTERN "CMakeLists.txt" EXCLUDE
	PATTERN "README.md" EXCLUDE
	PATTERN "prompts.md" EXCLUDE)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/qml/"
	DESTINATION "${ASSET_DIR}/qml"
	COMPONENT FVDNative FILES_MATCHING PATTERN "*.qml")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/shaders/"
	DESTINATION "${ASSET_DIR}/shaders"
	COMPONENT FVDNative FILES_MATCHING PATTERN "*.glsl")
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/test/textures/"
	DESTINATION "${ASSET_DIR}/test/textures"
	COMPONENT FVDNative PATTERN "CMakeLists.txt" EXCLUDE)
install(FILES
	"${CMAKE_SOURCE_DIR}/cfg/audio.oac"
	"${CMAKE_SOURCE_DIR}/cfg/keybinds.oac"
	DESTINATION "etc/openage" COMPONENT FVDNative)

# Windows needs nyan.dll beside the executable; Unix platforms put it in lib.
if(WIN32)
	install(FILES "$<TARGET_FILE:nyan::nyan>"
		DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT FVDNative)
else()
	install(FILES "$<TARGET_FILE:nyan::nyan>"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT FVDNative)
endif()

# Only the standalone Windows portable game package deploys Qt binaries here.
# The legacy all-component installer still has its separate Python deployer.
if(MSVC)
	# Keep the portable archive runnable on Windows without VS redistributables.
	include(InstallRequiredSystemLibraries)
	if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
		install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
			DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT FVDNative)
	endif()
endif()

if(WIN32)
	configure_file(
		"${CMAKE_CURRENT_LIST_DIR}/DeployWindows.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/DeployWindows.cmake" @ONLY)
	install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/DeployWindows.cmake"
		COMPONENT FVDNative)
endif()

# Conversion is a development/build-time tool, never a player operation.
if(FVD_NATIVE_API_ASSET_DIR)
	if(NOT EXISTS "${FVD_NATIVE_API_ASSET_DIR}/modpack.toml"
	   OR NOT EXISTS "${FVD_NATIVE_API_ASSET_DIR}/manifest.toml")
		message(FATAL_ERROR
			"FVD_NATIVE_API_ASSET_DIR must contain modpack.toml and manifest.toml")
	endif()
	install(DIRECTORY "${FVD_NATIVE_API_ASSET_DIR}/"
		DESTINATION "${ASSET_DIR}/converted/engine"
		COMPONENT FVDNative)

	# Generate integrity hashes on the build machine, never in the player.
	set(_fvd_manifest "${CMAKE_CURRENT_BINARY_DIR}/fvd-native-manifest.toml")
	execute_process(
		COMMAND "${CMAKE_COMMAND}"
			"-DFVD_MANIFEST_SOURCE_ASSETS=${CMAKE_SOURCE_DIR}/assets"
			"-DFVD_MANIFEST_API_ASSETS=${FVD_NATIVE_API_ASSET_DIR}"
			"-DFVD_MANIFEST_OUTPUT=${_fvd_manifest}"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/native_game/GenerateNativeManifest.cmake"
		RESULT_VARIABLE _fvd_manifest_exit)
	if(NOT _fvd_manifest_exit EQUAL 0)
		message(FATAL_ERROR "FVD native release manifest generation failed")
	endif()
	install(FILES "${_fvd_manifest}"
		DESTINATION "${ASSET_DIR}" COMPONENT FVDNative)
endif()
