// Copyright 2026 the openage authors. See copying.md for legal info.
// Minimal executable; the C++-owned registry and its tests live together
// inside the developer libopenage build to avoid Windows DLL export issues.
#include "testing/native_test_runner.h"

int main(int argc, char **argv) {
	return openage::testing::native_test_main(argc, argv);
}
