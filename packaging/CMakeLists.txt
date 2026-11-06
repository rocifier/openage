# Copyright 2017-2019 the openage authors. See copying.md for legal info.

# Keeps all required CPack configuration in one place and include(CPack)


########################################################################
# Basic information

set(CPACK_PACKAGE_VENDOR "The openage authors")
set(CPACK_PACKAGE_CONTACT "openage-Maintainers https://github.com/SFTtech/openage#contact")
set(CPACK_PACKAGE_DESCRIPTION "Free (as in freedom) open source clone of the Age of Empires II engine 🚀")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A FLOSS RTS-engine in tribute to Genie 🚀")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://openage.dev/")


########################################################################
# CPackOptions for Generators + Version details
set(CPACK_VERSION_FULL_STRING "${VERSION_FULL_STRING}")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/packaging/CPackOptions.cmake")


########################################################################
# Environment

if(MSVC)
	add_executable(openage_launcher WIN32 openage_launcher.cpp)
	set_target_properties(openage_launcher PROPERTIES OUTPUT_NAME "openage")
	target_link_libraries(openage_launcher PRIVATE user32)
	install(TARGETS openage_launcher RUNTIME DESTINATION ".")

	# HACKHACK: Update the following if vcpkg breaks compatibility for
	# internal directories, scripts, or variables.
	set(vcpkg_dir "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
	set(_python_embed_version "${PYTHON_VERSION_STRING}")
	# Python 3.12.10 was the final 3.12 release with Windows installers.
	# Later security releases retain the same extension-module ABI.
	if(PYTHON_VERSION_STRING VERSION_GREATER "3.12.10"
	   AND PYTHON_VERSION_STRING VERSION_LESS "3.13")
		set(_python_embed_version "3.12.10")
	endif()
	set(PYTHON_EMBED_VERSION "${_python_embed_version}" CACHE STRING
		"Python embeddable distribution version used for Windows packages")
	find_program(windeployqt windeployqt.exe)

	configure_file(
		"${BUILDSYSTEM_DIR}/templates/ForwardVariables.cmake.in"
		"${CMAKE_CURRENT_BINARY_DIR}/ForwardVariables.cmake"
	)
	install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/ForwardVariables.cmake")
	install(SCRIPT "${BUILDSYSTEM_DIR}/scripts/EmbedWinDependencies.cmake")
	foreach(_UTILITY ${REQUIRED_UTILITIES})
		install(FILES "${${_UTILITY}_EXECUTABLE}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
	endforeach()
	configure_file(
		"${BUILDSYSTEM_DIR}/templates/openage.bat.in"
		"${CMAKE_CURRENT_BINARY_DIR}/openage.bat"
	)
	install(FILES "${CMAKE_CURRENT_BINARY_DIR}/openage.bat" DESTINATION ".")

	include(InstallRequiredSystemLibraries)
endif()


# The relay is intentionally linked only against the native network core.
# No Python/Cython, game engine, Qt or nyan dependencies are required.
include("${CMAKE_CURRENT_SOURCE_DIR}/native_relay/NativeRelay.cmake")
add_fvd_native_relay()
install(TARGETS openage_native_relay RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")


# Native game startup is opt-in until the Python adapter boundary and
# packaged-asset manifest have passed runtime verification.
add_executable(openage_native_game native_game_main.cpp native_game_args.cpp)
set_target_properties(openage_native_game PROPERTIES OUTPUT_NAME "openage-native")
target_include_directories(openage_native_game PRIVATE "${CMAKE_SOURCE_DIR}/libopenage")
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
target_link_libraries(openage_native_game PRIVATE libopenage Qt6::Core)
install(TARGETS openage_native_game RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
# Independent staged runtime component excludes Python/Cython artifacts.
include("${CMAKE_CURRENT_SOURCE_DIR}/native_game/NativePackage.cmake")
# NativePackage.cmake declares FVD_NATIVE_RELEASE; set this only after
# the release option has been evaluated.
target_compile_definitions(openage_native_game PRIVATE
	FVD_NATIVE_RELEASE=$<BOOL:${FVD_NATIVE_RELEASE}>)

if(BUILD_TESTING)
	add_executable(openage_native_game_args_test native_game_args_test.cpp native_game_args.cpp)
	add_test(NAME fvd_native_game_args COMMAND openage_native_game_args_test)
	add_test(NAME fvd_native_game_help COMMAND openage_native_game --help)
	add_test(NAME fvd_native_game_version COMMAND openage_native_game --version)
	add_test(NAME fvd_native_game_missing_assets COMMAND openage_native_game --asset-dir __fvd_missing_assets__)
	set_tests_properties(fvd_native_game_missing_assets PROPERTIES WILL_FAIL TRUE)
	add_test(NAME fvd_native_game_invalid_options COMMAND openage_native_game --network-update-interval-ms 0)
	set_tests_properties(fvd_native_game_invalid_options PROPERTIES WILL_FAIL TRUE)
endif()

########################################################################
# Resource files

set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/copying.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")

# TODO: Still really blurry, another possibility to get a higher res pic in there?
#set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/assets/logo\\\\cpack.bmp")

########################################################################
# File output
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")
set(CPACK_PACKAGE_CHECKSUM "SHA256")

########################################################################
# General package config

if(FVD_NATIVE_RELEASE)
	# A direct player archive, not the developer or embedded-Python bundle.
	set(CPACK_GENERATOR "7Z")
	set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
	set(CPACK_COMPONENTS_ALL FVDNative)
	set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
else()
	set(CPACK_GENERATOR "NSIS" "7Z")
endif()


include(CPack)
