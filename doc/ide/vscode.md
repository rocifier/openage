# Visual Studio Code — FVD

Open the FVD checkout in VS Code and install the C++/CMake tooling appropriate for your system. See [build requirements](../building.md).

The checked-in [VS Code tasks](configs/vscode/tasks.json) run the native FVD build, package, C++ tests and optional pure-Python regression tests. Use the **build/test task before launching a developer executable** to ensure it contains your latest code. Packaged `solo` and `relay` commands always use the latest archive, not uncommitted source edits.

Windows: `.\\build-fvd.cmd build`, `.\\build-fvd.cmd test`, then `.\\build-fvd.cmd` to package. Linux: `bash build-fvd.sh build`, `bash build-fvd.sh test`, then `bash build-fvd.sh` to package.

For a cleaner editor, exclude build directories (`**/.bin/**`, `**/bin/**`, `**/build/**`, `**/dist/run/**`) from file watching. FVD uses the checked-in API in `assets/fvd/engine_api`; `assets/converted` is **not** a generated build cache. The old Python/Cython and converter tasks have been removed.
