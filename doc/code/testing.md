# FVD tests

The game, relay and engine tests run **natively in C++**. The old `run.py`/`bin/run` and Cython dispatcher have been removed.

## Native developer tests

On Windows first configure the native targets with `.\\build-fvd.cmd build`, then run `.\\build-fvd.cmd test`. On Linux use `bash build-fvd.sh build` followed by `bash build-fvd.sh test`. Both use CTest and `openage_native_cpp_tests` (developer-only, never packaged for players).

```sh
bash build-fvd.sh test --list
bash build-fvd.sh test --exact openage::network::tests::lobby_capacity
bash build-fvd.sh test --exact openage::network::tests::relay_multi_host_lobby
bash build-fvd.sh test --exact openage::network::tests::fvd_multi_session
bash build-fvd.sh test --exact openage::audio::tests::fvd_audio_bank
bash build-fvd.sh test --prefix openage::network::tests::
```

On Windows, replace `bash build-fvd.sh` with `.\\build-fvd.cmd`. The registry supports `--list`, `--exact NAME`, `--prefix PREFIX`, `--all` and `--all --have-assets`. Process workers, demos and benchmarks remain opt-in; tests requiring assets need checked-in FVD resources and the tracked engine API. New C++ tests must be registered in `libopenage/testing/native_test_runner.cpp`.

## Optional pure Python regressions

```sh
bash build-fvd.sh python-test
# Windows: .\build-fvd.cmd python-test
```

These `unittest` cases verify network proxy/bandwidth-shaping helpers and use only Python 3.12+ stdlib. Python is **not** part of CMake, native gameplay, or packaged binaries. The `fvd-native-game-args` CI workflow runs this suite separately.

## Native release checks

`build-fvd` with no arguments creates a 7Z archive and runs packaged-file audits. Those checks include native libraries, versioned nyan API, the SHA3 engine manifest, SHA-256 content hashes and platform-specific library closure. They **do not** replace actual two-client visual/audio/multiplayer tests. See [release acceptance](network/FVD_RELEASE_ACCEPTANCE.md) and [network impairment testing](network/FVD_SIMULATED_NETWORK_TESTING.md).

## Changed-file checks

The `fvd-quality` workflow checks changed header guards, C++ style, legal notices, whitespace and file modes against `FVD`. Developers can run `python3 -m buildsystem.codecompliance --only-changed-files origin/FVD --headerguards --cppstyle --legal --filemodes --textfiles` after fetching the `FVD` remote branch. This optional code-quality Python is never a native runtime dependency.
