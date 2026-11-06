# Copyright 2026 the openage authors. See copying.md for legal info.
# Independent CMake-only regression test: no engine, Qt or Python required.

set(_root "${CMAKE_CURRENT_BINARY_DIR}/fvd-manifest-selftest")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY
	"${_root}/assets/fvd/data"
	"${_root}/assets/shaders"
	"${_root}/assets/qml"
	"${_root}/api")
file(WRITE "${_root}/assets/fvd/modpack.toml"
	"[info]\nname = \"fvd\"\nversion = \"0.1.0\"\n")
file(WRITE "${_root}/assets/fvd/data/scenario.nyan" "test scenario")
file(WRITE "${_root}/assets/shaders/identity.vert.glsl" "test shader")
file(WRITE "${_root}/assets/qml/main.qml" "import QtQuick\nItem {}\n")
file(WRITE "${_root}/api/modpack.toml"
	"[info]\nname = \"engine\"\nversion = \"0.5.0\"\n")
file(WRITE "${_root}/api/manifest.toml" "# exported engine manifest\n")

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
if(NOT _count EQUAL 6)
	message(FATAL_ERROR "Expected 6 hashed FVD release records; got ${_count}")
endif()
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
