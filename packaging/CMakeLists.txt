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

set(CPACK_GENERATOR "NSIS" "7Z")


include(CPack)
