# Copyright 2017-2020 the openage authors. See copying.md for legal info.

"""
Copies the specified modules to the required directory.
Used for packaging the python dependencies.
"""

import argparse
import importlib
import importlib.abc
import importlib.metadata
import importlib.util
import os
import re
import shutil
import sys
from pathlib import Path


def copy_distribution(name, destination, copied):
    """Copy a distribution and its runtime dependencies."""
    distribution = importlib.metadata.distribution(name)
    distribution_name = distribution.metadata["Name"]
    normalized_name = re.sub(r"[-_.]+", "-", distribution_name).lower()
    if normalized_name in copied:
        return

    copied.add(normalized_name)
    print(f'Copying distribution "{distribution_name}" to "{destination}"')
    package_root = Path(str(distribution.locate_file(""))).resolve()
    for relative_path in distribution.files or ():
        source = Path(str(distribution.locate_file(relative_path))).resolve()
        if not source.is_file() or not source.is_relative_to(package_root):
            continue

        target = Path(destination, source.relative_to(package_root))
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)

    for requirement in distribution.requires or ():
        requirement_spec, separator, marker = requirement.partition(";")
        if separator and "extra" in marker:
            continue

        dependency_match = re.match(r"\s*([A-Za-z0-9_.-]+)", requirement_spec)
        if dependency_match:
            copy_distribution(dependency_match.group(1), destination, copied)


def copy_module(name, destination, copied):
    """Copy the importable module 'name' to the 'destination' directory"""
    package_name = name.partition(".")[0]
    distributions = importlib.metadata.packages_distributions().get(package_name, ())
    if distributions:
        for distribution in distributions:
            copy_distribution(distribution, destination, copied)
        return

    spec = importlib.util.find_spec(name)
    if spec is None or spec.loader is None:
        sys.exit(f"Module {name} could not be found")

    loader = spec.loader
    if not isinstance(loader, importlib.abc.FileLoader):
        sys.exit(f"Loader for module {name} is not handled")

    print(f'Copying "{name}" to "{destination}"')
    filename = loader.get_filename(name)
    if loader.is_package(name):
        pkgdir, _ = os.path.split(filename)
        shutil.copytree(pkgdir, os.path.join(destination, name))
    else:
        shutil.copy2(filename, destination)


def main():
    """CLI entry point"""
    cli = argparse.ArgumentParser()
    cli.add_argument("pymodule_name", nargs="+", help=("list of all modules that shall be copied"))
    cli.add_argument("dest_dir", help=("destination directory where modules will be copied"))
    args = cli.parse_args()
    copied = set()
    for module in args.pymodule_name:
        copy_module(module, args.dest_dir, copied)


if __name__ == "__main__":
    main()
