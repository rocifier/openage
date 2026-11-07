# Copyright 2026 the openage authors. See copying.md for legal info.
# Native C++ failures preserve the test name, source file/line and message.
if(NOT EXISTS "${FVD_CPP_TEST_EXECUTABLE}")
	message(FATAL_ERROR "Native C++ test executable missing: ${FVD_CPP_TEST_EXECUTABLE}")
endif()
execute_process(
	COMMAND "${FVD_CPP_TEST_EXECUTABLE}" --verify-error-report
	RESULT_VARIABLE _status
	OUTPUT_VARIABLE _stdout
	ERROR_VARIABLE _stderr)
if(_status EQUAL 0)
	message(FATAL_ERROR "Native C++ deliberate exception unexpectedly passed")
endif()
if(NOT _stdout MATCHES "native::failure_diagnostic"
   OR NOT _stderr MATCHES "native::failure_diagnostic"
   OR NOT _stderr MATCHES "native_test_runner.cpp:[0-9]+"
   OR NOT _stderr MATCHES "native failure diagnostic")
	message(FATAL_ERROR "Native C++ error output lost test name or source: ${_stdout}\n${_stderr}")
endif()
