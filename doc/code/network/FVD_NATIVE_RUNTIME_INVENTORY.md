# FVD native-runtime migration inventory

This document records the dependency boundary found on the FVD branch
when work on the native runtime started. It is not a claim that either
installed binary is already Python-free.

## Existing entry points

- Packaged Windows game: \`packaging/openage_launcher.cpp\` launches an
  embedded \`python.exe -m openage game\`. Windows deployment must replace
  this launcher before the Python-free acceptance criterion is met.
- Relay: \`openage/relay/main.py\` validates arguments and runs the service
  through \`openage/relay/main_cpp.pyx\` (Cython). The native relay now
  parses the same relay options and calls \`network::run_relay_service\`
  directly, without that Python entry point.
- Game: \`libopenage/main.cpp\` already exposes a C++ \`run_game\` function,
  but \`main_arguments\` carries a \`util::Path\` root created by the Python
  startup flow.

## Native-library boundary still to resolve

- \`libopenage/util/path.h\` includes \`pyinterface/pyobject.h\` and
  accepts a Python object. \`libopenage/util/path.cpp\` creates Python
  filesystem adapters for non-native paths.
- \`libopenage/util/fslike/python.cpp\` and
  \`libopenage/util/filelike/python.cpp\` forward filesystem operations
  through Python callbacks. Native \`fslike::Directory\` already exists.
- \`libopenage/pyinterface/\` is built into \`libopenage\`, rather than
  into a separate optional adapter library.
- \`CMakeLists.txt\` globally configures Python/Cython and the generated
  embedded runner. \`packaging/CMakeLists.txt\` still packages the Python
  launcher and dependencies.

## Migration gates

1. Link and run the standalone relay on Windows and Linux, including
   help/version, invalid options, a real connection and SIGINT/SIGTERM.
2. Inspect the import tables of the executable **and all recursively loaded
   libraries**. Native source alone does not prove no Python ABI dependency.
3. Separate the Python filesystem/exception adapters and native engine root.
4. Add native game bootstrap, prebuilt asset validation, and package-content
   checks before removing the embedded launcher.

The initial parser unit test is independent of Cython or the engine and can
run before the entire engine dependency boundary is migrated.
