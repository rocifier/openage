# Testing FVD

FVD's primary engine, gameplay and network regression tests are C++ functions registered in `libopenage/testing/native_test_runner.cpp`. No Python/Cython dispatcher is used.

## Native C++ tests

Run `bash build-fvd.sh test` (Linux) or `.\\build-fvd.cmd test` (Windows) to rebuild and execute the CTest suite.

The resulting `openage_native_cpp_tests` executable supports:

```sh
openage_native_cpp_tests --list
openage_native_cpp_tests --exact openage::network::tests::messages
openage_native_cpp_tests --prefix openage::network::tests::
openage_native_cpp_tests --all --have-assets
```

`--all` runs the default test set, and `--have-assets` enables asset-dependent checks. Opt-in demonstrations and benchmarks are not run by default. Failures report the C++ test name, exception and location.

Add new tests to the C++-owned registry alongside the implementation. There are no generated testlists or Cython module declarations.

## Optional Python tests

The retained network-shaping regressions require only the standard library:

```sh
bash build-fvd.sh python-test
python3 -m unittest -v openage.testing.test_fvd_network_benchmark
```

They run separately from native configuration; Python is not a game, relay or player archive dependency. The optional [network benchmark](network/FVD_SIMULATED_NETWORK_TESTING.md) invokes the *native C++ test runner*, not `bin/run`.
