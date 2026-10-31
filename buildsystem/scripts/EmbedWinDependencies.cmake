# Copyright 2017-2020 the openage authors. See copying.md for legal info.

if(NOT forward_variables)
	message(FATAL_ERROR "CMake configuration variables not available. Please include(ForwardVariables.cmake)")
endif()

get_filename_component(cl_directory "${cmake_linker}" DIRECTORY)
find_program(dumpbin dumpbin.exe PATHS "${cl_directory}")
if(NOT dumpbin)
	message(SEND_ERROR "Cannot locate dumpbin.")
	return()
endif()

# Paths for applocal and powershell
set(APPLOCAL_SCRIPT "${vcpkg_dir}/../../scripts/buildsystems/msbuild/applocal.ps1")
set(POWERSHELL_COMMAND "$ENV{SYSTEMROOT}/System32/WindowsPowerShell/v1.0/powershell.exe")

# function to wrap the dependency resolving and copying logic on Windows.
# Inspired by `applocal.ps1` from vcpkg which recursively descends into the
# dependency tree of a binary and copies each to the target directory.
function(resolve binary)
	set(bin_dir "${CMAKE_INSTALL_PREFIX}/bin")
	execute_process(COMMAND "${dumpbin}" /DEPENDENTS "${binary}"
		RESULT_VARIABLE dump_result
		OUTPUT_VARIABLE dump_output
	)
	if(NOT dump_result EQUAL 0)
		message(SEND_ERROR "dumpbin returned ${dump_result}.")
		return()
	endif()
	string(REGEX MATCHALL "    [^\\\n]+\\.dll" dll_list ${dump_output})
	foreach(item ${dll_list})
		string(STRIP "${item}" dll_name)
		set(dll_src_path "${vcpkg_dir}/bin/${dll_name}")
		set(dll_dest_path "${bin_dir}/${dll_name}")
		if(NOT EXISTS "${dll_dest_path}" AND EXISTS "${dll_src_path}")
			message(STATUS "Copying ${dll_name}")
			file(COPY "${dll_src_path}" DESTINATION "${bin_dir}")
			resolve("${dll_dest_path}")
		endif()
	endforeach()
endfunction()


# Resolve dependencies for DLLs and Python extension modules.
foreach(file ${CMAKE_INSTALL_MANIFEST_FILES})
	if(file MATCHES "\\.(dll|pyd)$")
		resolve("${file}")
	endif()
endforeach()

# Deploy Qt plugins and QML imports.
if(use_windeployqt)
	if(windeployqt)
		foreach(file ${CMAKE_INSTALL_MANIFEST_FILES})
			if(file MATCHES "\\.(dll|pyd)$")
				message(STATUS "Calling windeployqt with ${file}.")
				execute_process(COMMAND "${CMAKE_COMMAND}" -E env "PATH=${vcpkg_dir}/bin;$ENV{PATH}"
					"${windeployqt}"
					--qmldir "${CMAKE_INSTALL_PREFIX}/${asset_dir}"
					--dir "${CMAKE_INSTALL_PREFIX}/bin/qml"
					--plugindir "${CMAKE_INSTALL_PREFIX}/bin/plugins"
					--libdir "${CMAKE_INSTALL_PREFIX}/bin"
					--list "target"
					--verbose 2
					"${file}"
					RESULT_VARIABLE windeployqt_result
				)
				if(NOT windeployqt_result EQUAL 0)
					message(FATAL_ERROR "windeployqt failed with exit code ${windeployqt_result}.")
				endif()
			endif()
		endforeach()
	else()
		set(qmlimportscanner "${vcpkg_dir}/tools/Qt6/bin/qmlimportscanner.exe")
		if(NOT EXISTS "${qmlimportscanner}")
			message(FATAL_ERROR "Cannot locate windeployqt or the vcpkg qmlimportscanner fallback.")
		endif()
		message(STATUS "windeployqt is unavailable; deploying the vcpkg Qt runtime directly.")
		execute_process(COMMAND
			"${python}" "${buildsystem_dir}/scripts/deploy_qt.py"
			"${qmlimportscanner}"
			"${CMAKE_INSTALL_PREFIX}/${asset_dir}"
			"${vcpkg_dir}/Qt6"
			"${CMAKE_INSTALL_PREFIX}/bin"
			RESULT_VARIABLE deploy_qt_result
		)
		if(NOT deploy_qt_result EQUAL 0)
			message(FATAL_ERROR "Qt runtime deployment failed with exit code ${deploy_qt_result}.")
		endif()
	endif()

	file(GLOB_RECURSE qt_runtime_binaries
		"${CMAKE_INSTALL_PREFIX}/bin/plugins/*.dll"
		"${CMAKE_INSTALL_PREFIX}/bin/qml/*.dll"
	)
	foreach(file ${qt_runtime_binaries})
		resolve("${file}")
	endforeach()
endif()

# applocal
if((EXISTS ${POWERSHELL_COMMAND}) AND (EXISTS ${APPLOCAL_SCRIPT}))
	foreach(file ${CMAKE_INSTALL_MANIFEST_FILES})
		if(file MATCHES "\\.dll$")
			message(STATUS "Calling applocal.ps1 with ${file}.")
			execute_process(COMMAND "${POWERSHELL_COMMAND}" "${APPLOCAL_SCRIPT}"
					-targetBinary "${file}"
					-installedDir "${vcpkg_dir}/bin"
					-tlogFile "${LOGFILES_DIR}/applocal.log"
					-copiedFilesLog "${LOGFILES_DIR}/applocal_copied_files.log"
					ERROR_FILE "${LOGFILES_DIR}/applocal_error.log"
					OUTPUT_FILE "${LOGFILES_DIR}/applocal_output.log"
			)
		endif()
	endforeach()
endif()

# Copy qt.conf to python-directory
file(COPY "${buildsystem_dir}/templates/qt.conf"
	DESTINATION "${CMAKE_INSTALL_PREFIX}/${py_install_prefix}"
)

# The fonts directory defaults to the same directory as the executable.
# In this case, it is python.exe at `py_install_prefix`.
set(fontconfig_dir "${vcpkg_dir}/etc/fonts")
if(NOT EXISTS "${fontconfig_dir}")
	set(fontconfig_dir "${vcpkg_dir}/tools/fontconfig/fonts")
endif()
if(NOT EXISTS "${fontconfig_dir}")
	message(FATAL_ERROR "Cannot locate the vcpkg Fontconfig configuration directory.")
endif()
file(COPY "${fontconfig_dir}"
	DESTINATION "${CMAKE_INSTALL_PREFIX}/${py_install_prefix}"
)

include("${CMAKE_CURRENT_LIST_DIR}/EmbedFont.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/EmbedPython.cmake")
