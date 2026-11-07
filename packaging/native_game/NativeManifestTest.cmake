# Copyright 2026 the openage authors. See copying.md for legal info.
# Independent CMake-only regression test: no engine, Qt or Python required.

set(_root "${CMAKE_CURRENT_BINARY_DIR}/fvd-manifest-selftest")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY
	"${_root}/assets/fvd/data"
	"${_root}/assets/fvd/sound"
	"${_root}/assets/shaders"
	"${_root}/assets/qml"
	"${_root}/api")
file(WRITE "${_root}/assets/fvd/modpack.toml"
	"[info]\nname = \"fvd\"\nversion = \"0.1.0\"\n")
file(WRITE "${_root}/assets/fvd/data/scenario.nyan" "test scenario")
# Documentation is excluded recursively by the FVDNative CMake install rules.
# The integrity manifest must never reference any of these excluded files.
file(WRITE "${_root}/assets/fvd/README.md" "root readme")
file(WRITE "${_root}/assets/fvd/prompts.md" "root prompts")
file(WRITE "${_root}/assets/fvd/sound/README.md" "nested readme")
file(WRITE "${_root}/assets/fvd/sound/prompts.md" "nested prompts")
file(WRITE "${_root}/assets/fvd/sound/CMakeLists.txt" "not a runtime asset")

file(WRITE "${_root}/assets/shaders/identity.vert.glsl" "test shader")
file(WRITE "${_root}/assets/qml/main.qml" "import QtQuick\nItem {}\n")
file(WRITE "${_root}/api/modpack.toml"
	"[info]\nname = \"engine\"\nversion = \"0.5.0\"\n")
# Construct a complete miniature (content-wise) API snapshot with the same
# file count, original SHA3-256 manifest and installed layout as production.
file(WRITE "${_root}/api/root.nyan" "# NYAN FILE\n!version 0.2.0\n\nObject():\n    pass\n")
foreach(_index RANGE 1 152)
	file(WRITE "${_root}/api/test_${_index}.nyan" "Test_${_index}():\n    pass\n")
endforeach()
set(_api_manifest "[info]\nhash = \"sha3_256\"\n\n[hash-values]\n")
file(GLOB_RECURSE _api_input_files RELATIVE "${_root}/api" "${_root}/api/*")
list(SORT _api_input_files)
foreach(_path IN LISTS _api_input_files)
	file(SHA3_256 "${_root}/api/${_path}" _hash)
	string(APPEND _api_manifest "${_hash} = \"${_path}\"\n")
endforeach()
file(WRITE "${_root}/api/manifest.toml" "${_api_manifest}")

set(_manifest "${_root}/fvd-native-manifest.toml")
set(_generator "${CMAKE_CURRENT_LIST_DIR}/GenerateNativeManifest.cmake")
execute_process(
	COMMAND "${CMAKE_COMMAND}"
		"-DFVD_MANIFEST_SOURCE_ASSETS=${_root}/assets"
		"-DFVD_MANIFEST_API_ASSETS=${_root}/api"
		"-DFVD_MANIFEST_OUTPUT=${_manifest}"
		-P "${_generator}"
	RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
	message(FATAL_ERROR "Unable to generate FVD native release manifest")
endif()
file(READ "${_manifest}" _text)
string(REGEX MATCHALL "\\[\\[file\\]\\]" _records "${_text}")
list(LENGTH _records _count)
if(NOT _count EQUAL 159)
	message(FATAL_ERROR "Expected 159 hashed FVD release records; got ${_count}")
endif()
foreach(_excluded
	"fvd/README.md"
	"fvd/prompts.md"
	"fvd/sound/README.md"
	"fvd/sound/prompts.md"
	"fvd/sound/CMakeLists.txt")
	string(FIND "${_text}" "path = \"${_excluded}\"" _position)
	if(NOT _position EQUAL -1)
		message(FATAL_ERROR "Manifest included excluded runtime asset: ${_excluded}")
	endif()
endforeach()
file(SHA256 "${_root}/assets/fvd/modpack.toml" _digest)
string(FIND "${_text}" "${_digest}" _position)
if(_position LESS 0)
	message(FATAL_ERROR "Manifest omitted the real FVD modpack SHA-256")
endif()
string(FIND "${_text}" "qml/main.qml" _position)
if(_position LESS 0)
	message(FATAL_ERROR "Manifest omitted packaged FVD QML")
endif()
string(FIND "${_text}" "converted/engine/manifest.toml" _position)
if(_position LESS 0)
	message(FATAL_ERROR "Manifest omitted the exported engine manifest")
endif()

# Exercise independent native release SHA-256 verification on fixture data,
# then prove a valid manifest cannot mask tampering or missing files.
set(_stage "${_root}/stage")
foreach(_dir fvd shaders qml)
	file(MAKE_DIRECTORY "${_stage}/share/openage/${_dir}")
	file(COPY "${_root}/assets/${_dir}/"
		DESTINATION "${_stage}/share/openage/${_dir}")
endforeach()
file(READ "${_manifest}" _original_manifest)
file(WRITE "${_stage}/share/openage/fvd-native-manifest.toml" "${_original_manifest}")
file(MAKE_DIRECTORY "${_stage}/share/openage/converted/engine")
file(COPY "${_root}/api/"
	DESTINATION "${_stage}/share/openage/converted/engine")
set(_integrity "${CMAKE_CURRENT_LIST_DIR}/VerifyNativeManifest.cmake")
function(_check_integrity _expected _label)
	execute_process(COMMAND "${CMAKE_COMMAND}" "-DFVD_NATIVE_STAGE=${_stage}"
		-P "${_integrity}"
		RESULT_VARIABLE _rc OUTPUT_QUIET ERROR_QUIET)
	if(_expected AND NOT _rc EQUAL 0)
		message(FATAL_ERROR "${_label}: valid native asset stage rejected")
	endif()
	if(NOT _expected AND _rc EQUAL 0)
		message(FATAL_ERROR "${_label}: compromised stage accepted")
	endif()
endfunction()
_check_integrity(TRUE "normal stage")
file(APPEND "${_stage}/share/openage/fvd/modpack.toml" "\n# deliberate corruption\n")
_check_integrity(FALSE "tampered FVD modpack")
file(READ "${_root}/assets/fvd/modpack.toml" _original_modpack)
file(WRITE "${_stage}/share/openage/fvd/modpack.toml" "${_original_modpack}")
_check_integrity(TRUE "restored asset")
file(REMOVE "${_stage}/share/openage/shaders/identity.vert.glsl")
_check_integrity(FALSE "missing shader")
file(COPY "${_root}/assets/shaders/identity.vert.glsl"
	DESTINATION "${_stage}/share/openage/shaders")
file(APPEND "${_stage}/share/openage/fvd-native-manifest.toml"
	"[[file]]\npath = \"../unsafe\"\nsha256 = \"a\"\n")
_check_integrity(FALSE "invalid manifest record")
file(COPY "${_manifest}" DESTINATION "${_stage}/share/openage")
_check_integrity(TRUE "restored manifest")

# Both the checked-in path and the installed runtime path use the same
# validator. Assert the installed layout is viable before negative cases.
file(MAKE_DIRECTORY "${_root}/stage/share/openage/converted/engine")
file(COPY "${_root}/api/" DESTINATION "${_root}/stage/share/openage/converted/engine")
set(_validator "${CMAKE_CURRENT_LIST_DIR}/CheckEngineApi.cmake")
function(_check_api _path _should_pass _label)
	execute_process(COMMAND "${CMAKE_COMMAND}" "-DFVD_ENGINE_API_ROOT=${_path}"
		-P "${_validator}"
		RESULT_VARIABLE _exit OUTPUT_QUIET ERROR_QUIET)
	if(_should_pass AND NOT _exit EQUAL 0)
		message(FATAL_ERROR "${_label}: valid API unexpectedly rejected")
	endif()
	if(NOT _should_pass AND _exit EQUAL 0)
		message(FATAL_ERROR "${_label}: invalid API unexpectedly accepted")
	endif()
endfunction()
_check_api("${_root}/api" TRUE "tracked source")
_check_api("${_root}/stage/share/openage/converted/engine" TRUE "installed runtime path")
_check_api("${_root}/api-nonexistent" FALSE "missing snapshot")
file(REMOVE "${_root}/api/root.nyan")
_check_api("${_root}/api" FALSE "incomplete nyan snapshot")
file(WRITE "${_root}/api/root.nyan" "# NYAN FILE\n!version 0.2.0\n\nObject():\n    pass\n")
file(APPEND "${_root}/api/root.nyan" "# corrupted")
_check_api("${_root}/api" FALSE "checksum mismatch")
file(WRITE "${_root}/api/root.nyan" "# NYAN FILE\n!version 0.2.0\n\nObject():\n    pass\n")
file(REMOVE "${_root}/api/manifest.toml")
_check_api("${_root}/api" FALSE "missing manifest")
file(WRITE "${_root}/api/manifest.toml" "${_api_manifest}")
file(WRITE "${_root}/api/modpack.toml"
	"[info]\nname = \"engine\"\nversion = \"0.4.0\"\n")
_check_api("${_root}/api" FALSE "incompatible API version")

# Asset content/format changes require a different hash.
file(WRITE "${_root}/api/modpack.toml"
	"[info]\nname = \"engine\"\nversion = \"0.4.0\"\n")
execute_process(
	COMMAND "${CMAKE_COMMAND}"
		"-DFVD_MANIFEST_SOURCE_ASSETS=${_root}/assets"
		"-DFVD_MANIFEST_API_ASSETS=${_root}/api"
		"-DFVD_MANIFEST_OUTPUT=${_manifest}"
		-P "${_generator}"
	RESULT_VARIABLE _invalid
	OUTPUT_QUIET ERROR_QUIET)
if(_invalid EQUAL 0)
	message(FATAL_ERROR "Generator accepted a stale nyan API")
endif()
file(REMOVE_RECURSE "${_root}")
