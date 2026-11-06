# Copyright 2026 the openage authors. See copying.md for legal info.
# Pass -DFVD_NATIVE_STAGE=<install prefix> for a native-only install check.

file(TO_CMAKE_PATH "${FVD_NATIVE_STAGE}" FVD_NATIVE_STAGE)

if(NOT DEFINED FVD_NATIVE_STAGE OR NOT IS_DIRECTORY "${FVD_NATIVE_STAGE}")
	message(FATAL_ERROR "Specify -DFVD_NATIVE_STAGE=<existing staging directory>")
endif()

if(WIN32)
	set(_executable_suffix ".exe")
else()
	set(_executable_suffix "")
endif()
foreach(_required
	"bin/openage-native${_executable_suffix}"
	"bin/openage-relay${_executable_suffix}"
	"share/openage/fvd/modpack.toml"
	"share/openage/fvd/data/scenario.nyan"
	"share/openage/shaders/identity.vert.glsl"
	"etc/openage/audio.oac"
	"etc/openage/keybinds.oac")
	if(NOT EXISTS "${FVD_NATIVE_STAGE}/${_required}")
		message(FATAL_ERROR "Native FVD stage is missing: ${_required}")
	endif()
endforeach()

if(FVD_NATIVE_REQUIRE_MANIFEST)
	include("${CMAKE_CURRENT_LIST_DIR}/CheckEngineApi.cmake")
	fvd_validate_engine_api("${FVD_NATIVE_STAGE}/share/openage/converted/engine")
	foreach(_required
		"share/openage/fvd-native-manifest.toml"
		"share/openage/converted/engine/modpack.toml"
		"share/openage/converted/engine/manifest.toml")
		if(NOT EXISTS "${FVD_NATIVE_STAGE}/${_required}")
			message(FATAL_ERROR "Strict FVD native release is missing: ${_required}")
		endif()
	endforeach()
endif()

file(GLOB_RECURSE _files RELATIVE "${FVD_NATIVE_STAGE}"
	LIST_DIRECTORIES FALSE "${FVD_NATIVE_STAGE}/*")

foreach(_file IN LISTS _files)
	string(TOLOWER "${_file}" _lower)
	if(_lower MATCHES "\\.(py|pyc|pyo|pyd|whl)$"
	   OR _lower MATCHES "(^|/)(libpython|python[0-9]+)([0-9._-]*)(\\.dll|\\.so([.][0-9]+)*|\\.dylib|\\.exe)?$"
	   OR _lower MATCHES "(^|/)(__pycache__|site-packages|dist-packages)/")
		message(FATAL_ERROR "Python developer artifact in FVDNative component: ${_file}")
	endif()
endforeach()

list(LENGTH _files _count)
message(STATUS "FVDNative staged ${_count} files with no Python modules or interpreter")
