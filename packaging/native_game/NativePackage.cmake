# Copyright 2026 the openage authors. See copying.md for legal info.
#
# Separate install component for the Python-free FVD distribution.
# Qt/vendor runtime deployment and the release asset manifest are future gates.

set(FVD_NATIVE_API_ASSET_DIR "" CACHE PATH
	"Pre-exported engine API modpack directory; optional during development")

install(TARGETS openage_native_game openage_native_relay
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT FVDNative)
install(TARGETS libopenage
	RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
	LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
	COMPONENT FVDNative)

# The presenter also uses the legacy missing-texture test asset.
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/fvd/"
	DESTINATION "${ASSET_DIR}/fvd"
	COMPONENT FVDNative
	PATTERN "CMakeLists.txt" EXCLUDE
	PATTERN "README.md" EXCLUDE
	PATTERN "prompts.md" EXCLUDE)
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
endif()
