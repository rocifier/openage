# Copyright 2026 the openage authors. See copying.md for legal info.
# The sole supported FVD distribution: native C++ game, relay and FVDNative 7Z.
set(CPACK_PACKAGE_VENDOR "The FVD contributors")
set(CPACK_PACKAGE_CONTACT "https://github.com/rocifier/openage")
set(CPACK_PACKAGE_DESCRIPTION "Fast Villager Defense native game and relay")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast Villager Defense")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/rocifier/openage")
set(CPACK_VERSION_FULL_STRING "${VERSION_FULL_STRING}")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/packaging/CPackOptions.cmake")

# Windows Qt deployment must not discover Python or run legacy installers.
if(WIN32)
	set(vcpkg_dir "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
	find_program(windeployqt NAMES windeployqt.exe
		HINTS "${vcpkg_dir}/tools/Qt6/bin" "${vcpkg_dir}/tools/Qt6")
endif()

# The relay is intentionally linked only against the native network core.
# No Python/Cython, game engine, Qt or nyan dependencies are required.
include("${CMAKE_CURRENT_SOURCE_DIR}/native_relay/NativeRelay.cmake")
add_fvd_native_relay()


# Native game uses the tracked engine API and strict asset verification.
find_package(toml11 REQUIRED)
add_executable(openage_native_game native_game_main.cpp native_game_args.cpp)
set_target_properties(openage_native_game PROPERTIES OUTPUT_NAME "openage-native")
target_include_directories(openage_native_game PRIVATE "${CMAKE_SOURCE_DIR}/libopenage")
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)
target_link_libraries(openage_native_game PRIVATE libopenage Qt6::Core toml11::toml11)
# The sole install payload is the strict FVDNative component.
include("${CMAKE_CURRENT_SOURCE_DIR}/native_game/NativePackage.cmake")
# Generated from the single CMake compatibility source.
include("${CMAKE_CURRENT_SOURCE_DIR}/native_game/EngineApiVersion.cmake")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/native_game/EngineApiVersion.h.in"
	"${CMAKE_CURRENT_BINARY_DIR}/fvd_engine_api_version.h" @ONLY)
target_include_directories(openage_native_game PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(openage_native_game PRIVATE
	FVD_NATIVE_RELEASE=$<BOOL:${FVD_NATIVE_RELEASE}>)

if(BUILD_TESTING)
	add_test(NAME fvd_native_game_manifest
		COMMAND "${CMAKE_COMMAND}"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/native_game/NativeManifestTest.cmake")
	add_executable(openage_native_game_args_test native_game_args_test.cpp native_game_args.cpp)
	add_test(NAME fvd_native_game_args COMMAND openage_native_game_args_test)
	add_test(NAME fvd_native_game_help COMMAND openage_native_game --help)
	add_test(NAME fvd_native_game_version COMMAND openage_native_game --version)
	add_test(NAME fvd_native_game_missing_assets COMMAND openage_native_game --asset-dir __fvd_missing_assets__)
	set_tests_properties(fvd_native_game_missing_assets PROPERTIES WILL_FAIL TRUE)
	add_test(NAME fvd_native_game_invalid_options COMMAND openage_native_game --network-update-interval-ms 0)
	set_tests_properties(fvd_native_game_invalid_options PROPERTIES WILL_FAIL TRUE)
endif()

# Native C++ test executable has no Python/Cython interface and is not
# part of the player package. Its registry and test bodies live in libopenage
# for the developer build (Windows engine internals need not be exported).
if(BUILD_TESTING AND NOT FVD_NATIVE_RELEASE)
	add_executable(openage_native_cpp_tests native_cpp_tests_main.cpp)
	target_compile_features(openage_native_cpp_tests PRIVATE cxx_std_20)
	target_include_directories(openage_native_cpp_tests PRIVATE
		"${CMAKE_SOURCE_DIR}/libopenage")
	target_link_libraries(openage_native_cpp_tests PRIVATE libopenage)
	# The runner imports only one DLL symbol, but its engine DLL must be next
	# to the executable on Windows; the source tests are not shipped.
	if(WIN32)
		add_custom_command(TARGET openage_native_cpp_tests POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different
				"$<TARGET_FILE:libopenage>" "$<TARGET_FILE_DIR:openage_native_cpp_tests>"
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different
				"$<TARGET_FILE:nyan::nyan>" "$<TARGET_FILE_DIR:openage_native_cpp_tests>"
			COMMENT "Stage developer engine DLLs beside native C++ tests")
	endif()
	add_test(NAME fvd_native_game_cpp_list
		COMMAND openage_native_cpp_tests --list)
	set_tests_properties(fvd_native_game_cpp_list PROPERTIES
		PASS_REGULAR_EXPRESSION "openage::gamestate::tests::fvd_hunter_rally_point")
	add_test(NAME fvd_native_game_cpp_all
		COMMAND openage_native_cpp_tests --all)
	add_test(NAME fvd_native_game_cpp_exact
		COMMAND openage_native_cpp_tests --exact openage::testing::tests::comparison)
	add_test(NAME fvd_native_game_cpp_prefix
		COMMAND openage_native_cpp_tests --prefix openage::util::tests::)
	add_test(NAME fvd_native_game_cpp_error_report
		COMMAND "${CMAKE_COMMAND}"
		"-DFVD_CPP_TEST_EXECUTABLE=$<TARGET_FILE:openage_native_cpp_tests>"
		-P "${CMAKE_CURRENT_SOURCE_DIR}/native_game/NativeCppTestErrorCheck.cmake")
	add_test(NAME fvd_native_game_cpp_missing
		COMMAND openage_native_cpp_tests --exact openage::not_a_test)
	set_tests_properties(fvd_native_game_cpp_missing PROPERTIES WILL_FAIL TRUE)
endif()

########################################################################
# Native-only CPack archive; preserve existing Windows/Linux archive naming.
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/copying.md")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME}")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	set(CPACK_SET_DESTDIR ON)
endif()
set(CPACK_GENERATOR "7Z")
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL FVDNative)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
include(CPack)

if(BUILD_TESTING)
	add_test(NAME fvd_native_game_package_config
		COMMAND "${CMAKE_COMMAND}"
			"-DFVD_CPACK_CONFIG=${CMAKE_BINARY_DIR}/CPackConfig.cmake"
			-P "${CMAKE_CURRENT_SOURCE_DIR}/native_game/NativePackageConfigTest.cmake")
endif()
