# Copyright 2026 the openage authors. See copying.md for legal info.
"""Optional source-only FVD Python development helpers.

The native game, relay and C++ tests do not use or install this package.
Importing a Python test helper must not initialize the former game launcher,
Cython bindings or a generated CMake config module.
"""
