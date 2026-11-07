# Visual Studio Code — FVD

Open the FVD checkout in VS Code and install the C++/CMake tooling appropriate for your system. See [build requirements](../building.md).

The checked-in [VS Code tasks](configs/vscode/tasks.json) run the native FVD build, package, C++ tests and optional pure-Python regression tests. The **solo, game and relay launch tasks build incrementally first**, so they run the latest local code without packaging or extraction.

Windows: `.\\build-fvd.cmd solo` (or `game`, `relay`, `test`); Linux: `bash build-fvd.sh solo` (or `game`, `relay`, `test`). Only run `package` when producing a release archive.

For a cleaner editor, exclude build directories (`**/.bin/**`, `**/bin/**`, `**/build/**`, `**/dist/**`) from file watching. FVD uses the checked-in API in `assets/fvd/engine_api`; `assets/converted` is **not** a generated build cache. The old Python/Cython and converter tasks have been removed.
