# Copyright 2026 the openage authors. See copying.md for legal info.
#
# Separate install component for the Python-free FVD distribution.
# Qt/vendor runtime deployment and the release asset manifest are future gates.

include("${CMAKE_CURRENT_LIST_DIR}/CheckEngineApi.cmake")
set(_fvd_api_source "${CMAKE_SOURCE_DIR}/assets/fvd/engine_api")
# Fail at configure time, including developer builds. No conversion bootstrap.
fvd_validate_engine_api("${_fvd_api_source}")

option(FVD_NATIVE_RELEASE
	"Require Python-free adapters and verified tracked API when staging FVD" OFF)
if(FVD_NATIVE_RELEASE)
	if(FVD_WITH_PYTHON_ADAPTERS)
		message(FATAL_ERROR "FVD native releases must use -DFVD_WITH_PYTHON_ADAPTERS=OFF")
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
	PATTERN "prompts.md" EXCLUDE
	PATTERN "engine_api" EXCLUDE)
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
	# TARGET_FILE is the versioned real library (e.g. libnyan.so.0.3.0),
	# whereas the runtime loader requests its SONAME (e.g. libnyan.so.1).
	# Stage both the file and the SONAME symlink. Installing the real
	# library alone makes an otherwise correct native archive unlaunchable.
	install(FILES "$<TARGET_FILE:nyan::nyan>"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT FVDNative)
	install(FILES "$<TARGET_SONAME_FILE:nyan::nyan>"
		DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT FVDNative)
endif()

# The tracked FVD API is installed at the existing engine mount point.
# Do not also install it beneath fvd/: the source folder is not a runtime alias.
install(DIRECTORY "${_fvd_api_source}/"
	DESTINATION "${ASSET_DIR}/converted/engine"
	COMPONENT FVDNative)

# Generate integrity hashes only from reviewed tracked source inputs.
set(_fvd_manifest "${CMAKE_CURRENT_BINARY_DIR}/fvd-native-manifest.toml")
execute_process(
	COMMAND "${CMAKE_COMMAND}"
		"-DFVD_MANIFEST_SOURCE_ASSETS=${CMAKE_SOURCE_DIR}/assets"
		"-DFVD_MANIFEST_API_ASSETS=${_fvd_api_source}"
		"-DFVD_MANIFEST_OUTPUT=${_fvd_manifest}"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/native_game/GenerateNativeManifest.cmake"
	RESULT_VARIABLE _fvd_manifest_exit)
if(NOT _fvd_manifest_exit EQUAL 0)
	message(FATAL_ERROR "Tracked FVD engine API release manifest generation failed")
endif()
install(FILES "${_fvd_manifest}"
	DESTINATION "${ASSET_DIR}" COMPONENT FVDNative)

# Deploy dependencies and audit the *completed* native stage, including the
# engine API and integrity manifest. Run this at the END of component install.
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


