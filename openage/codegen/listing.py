# Copyright 2015-2026 the openage authors. See copying.md for legal info.

"""Deprecated developer codegen interface; no C++ sources are generated now.

The native coordinate types and test registry are checked into libopenage.
The remaining openage/codegen command itself is removed under OpenSpec Task 3.
"""

def generate_all(projectdir):
    """No-op while the obsolete developer codegen CLI remains accessible."""
    del projectdir
