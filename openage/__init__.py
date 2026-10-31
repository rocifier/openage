# Copyright 2013-2026 the openage authors. See copying.md for legal info.

"""
The Python part of openage, a free engine re-write of
Age of Empires II.

See https://openage.dev and http://github.com/SFTtech/openage

Requires Python 3.12.
"""

from importlib.util import find_spec
from sys import version_info as py_version

from .log import setup_logging

if py_version < (3, 12):
    raise RuntimeError("openage requires python 3.12 or higher.")


if find_spec(f"{__package__}.config") is not None:
    # TODO pylint: disable=wrong-import-position,import-self
    from . import config

    VERSION = config.VERSION

    LONGVERSION = (
        f"openage {config.VERSION}"
        f"{' [devmode]' if config.DEVMODE else ''}\n"
        f"{config.CONFIG_OPTIONS}, ci-cfg {config.CICFGVERSION}\n"
        f"{config.COMPILER} [{config.COMPILERFLAGS}]\n"
        "\n"
        "== Python ==\n"
        f"Python        {config.PYTHONINTERPRETER}\n"
        f"Python C API  {config.PYTHONCAPI}\n"
        f"Cython        {config.CYTHONVERSION}\n"
        f"Mako          {config.MAKOVERSION}\n"
        f"NumPy         {config.NUMPYVERSION}\n"
        f"Pillow        {config.PILVERSION}\n"
        f"Pygments      {config.PYGMENTSVERSION}"
    )
else:
    VERSION = "< unknown version; ./configure incomplete >"
    LONGVERSION = VERSION


setup_logging()
