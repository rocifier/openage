# Copyright 2026 the openage authors. See copying.md for legal info.
# NativePackage.cmake stages runtime assets. This target only aids QtCreator.
file(GLOB QML_SRC "qml/*.qml")
add_custom_target(qtcreator-show-qml SOURCES ${QML_SRC})
