add_sources(libopenage
	testing.cpp
	testing_test.cpp
	benchmark_test.cpp
)

# Keep native C++ tests and their dispatcher in the same DLL as the engine.
# This enables private symbols on Windows without shipping the tests.
if(BUILD_TESTING AND NOT FVD_NATIVE_RELEASE)
	add_sources(libopenage native_test_runner.cpp)
endif()

pxdgen(
	testlist.h
)
