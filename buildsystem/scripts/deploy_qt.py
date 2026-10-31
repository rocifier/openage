# Copyright 2026 the openage authors. See copying.md for legal info.

"""Deploy Qt plugins and the QML modules used by installed assets."""

import argparse
import json
import shutil
import subprocess
from pathlib import Path


def copy_files(source, destination):
    """Copy runtime files while excluding development-only symbols."""
    for source_file in source.rglob("*"):
        if not source_file.is_file() or source_file.suffix.lower() == ".pdb":
            continue

        target = destination / source_file.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_file, target)


def copy_qml_modules(modules, destination):
    """Copy selected QML modules without pulling in sibling modules."""
    module_paths = {
        Path(module["path"]).resolve() for module in modules if "path" in module and "relativePath" in module
    }
    for module in modules:
        if "path" not in module or "relativePath" not in module:
            continue

        source = Path(module["path"]).resolve()
        target_root = destination / module["relativePath"]
        for source_file in source.rglob("*"):
            if not source_file.is_file() or source_file.suffix.lower() == ".pdb":
                continue

            relative_path = source_file.relative_to(source)
            parent_paths = [source / parent for parent in relative_path.parents if parent != Path(".")]
            if any(
                parent != source and (parent / "qmldir").is_file() and parent not in module_paths
                for parent in parent_paths
            ):
                continue

            target = target_root / relative_path
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, target)


def main():
    """Deploy the Qt runtime selected by qmlimportscanner."""
    cli = argparse.ArgumentParser()
    cli.add_argument("qmlimportscanner", type=Path)
    cli.add_argument("qml_root", type=Path)
    cli.add_argument("qt_root", type=Path)
    cli.add_argument("destination", type=Path)
    args = cli.parse_args()

    scan = subprocess.run(
        [
            args.qmlimportscanner,
            "-rootPath",
            args.qml_root,
            "-importPath",
            args.qt_root / "qml",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    copy_qml_modules(json.loads(scan.stdout), args.destination / "qml")
    copy_files(args.qt_root / "plugins", args.destination / "plugins")


if __name__ == "__main__":
    main()
