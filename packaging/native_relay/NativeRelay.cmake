# Copyright 2026 the openage authors. See copying.md for legal info.

get_filename_component(FVD_NATIVE_RELAY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Reuse the same isolated target in source builds and standalone host builds.
# Do not link libopenage: it pulls in Qt, nyan and Python/Cython adapters.
function(add_fvd_native_relay)
	find_package(Threads REQUIRED)
	set(_root "${FVD_NATIVE_RELAY_ROOT}")
	set(_generated "${CMAKE_CURRENT_BINARY_DIR}/fvd_native_relay_generated")
	file(MAKE_DIRECTORY "${_generated}")
	configure_file("${_root}/packaging/native_relay/config.h.in"
		"${_generated}/config.h" @ONLY)

	# Portable static musl builds omit optional symbolized C++ backtraces.
	if(FVD_RELAY_PORTABLE_STATIC)
		set(_relay_stackanalyzer "${_root}/packaging/native_relay/portable_stackanalyzer.cpp")
	else()
		set(_relay_stackanalyzer "${_root}/libopenage/error/stackanalyzer.cpp")
	endif()

	add_library(fvd_native_relay_core STATIC
		"${_root}/libopenage/network/relay_service.cpp"
		"${_root}/libopenage/network/relay.cpp"
		"${_root}/libopenage/network/transport.cpp"
		"${_root}/libopenage/network/protocol.cpp"
		"${_root}/libopenage/network/serialization.cpp"
		"${_root}/libopenage/error/error.cpp"
		"${_root}/libopenage/error/backtrace.cpp"
		${_relay_stackanalyzer}
		"${_root}/libopenage/log/level.cpp"
		"${_root}/libopenage/log/log.cpp"
		"${_root}/libopenage/log/logsink.cpp"
		"${_root}/libopenage/log/logsource.cpp"
		"${_root}/libopenage/log/named_logsource.cpp"
		"${_root}/libopenage/log/message.cpp"
		"${_root}/libopenage/log/stdout_logsink.cpp"
		"${_root}/libopenage/util/compiler.cpp"
		"${_root}/libopenage/util/enum.cpp"
		"${_root}/libopenage/util/externalsstream.cpp"
		"${_root}/libopenage/util/stringformatter.cpp"
		"${_root}/libopenage/util/strings.cpp"
		"${_root}/libopenage/util/thread_id.cpp"
		"${_root}/libopenage/util/timing.cpp"
	)
	target_compile_features(fvd_native_relay_core PUBLIC cxx_std_20)
	target_include_directories(fvd_native_relay_core PUBLIC
		"${_root}/libopenage"
		"${_generated}")
	target_compile_definitions(fvd_native_relay_core PUBLIC OPENAGE_STANDALONE_RELAY)
	target_link_libraries(fvd_native_relay_core PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
	if(WIN32)
		target_link_libraries(fvd_native_relay_core PUBLIC ws2_32 dbghelp)
	endif()

	add_executable(openage_native_relay
		"${_root}/packaging/native_relay_main.cpp"
		"${_root}/packaging/native_relay_args.cpp")
	set_target_properties(openage_native_relay PROPERTIES OUTPUT_NAME "openage-relay")
	target_compile_definitions(openage_native_relay PRIVATE
		OPENAGE_NATIVE_RELAY_VERSION=\"${VERSION_FULL_STRING}\")
	target_link_libraries(openage_native_relay PRIVATE fvd_native_relay_core)

	if(BUILD_TESTING)
		add_executable(openage_native_relay_args_test
			"${_root}/packaging/native_relay_args.cpp"
			"${_root}/packaging/native_relay_args_test.cpp")
		target_compile_features(openage_native_relay_args_test PRIVATE cxx_std_20)
		add_executable(openage_native_relay_network_test
			"${_root}/packaging/native_relay/relay_network_test.cpp")
		target_link_libraries(openage_native_relay_network_test PRIVATE fvd_native_relay_core)
		add_test(NAME fvd_native_relay_network COMMAND openage_native_relay_network_test)
		set_tests_properties(fvd_native_relay_network PROPERTIES TIMEOUT 30)
		add_test(NAME fvd_native_relay_args COMMAND openage_native_relay_args_test)
		add_test(NAME fvd_native_relay_help COMMAND openage_native_relay --help)
		add_test(NAME fvd_native_relay_version COMMAND openage_native_relay --version)
	endif()
endfunction()
